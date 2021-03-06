/*
 *  @(#) $Id$
 *  Copyright (C) 2004 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define FFTF_1D_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    GWY_FFTF_1D_SUPPRESS_NULL         = 0,
    GWY_FFTF_1D_SUPPRESS_NEIGBOURHOOD = 1
} GwyFftf1dSuppressType;

typedef enum {
    GWY_FFTF_1D_VIEW_MARKED   = 0,
    GWY_FFTF_1D_VIEW_UNMARKED = 1
} GwyFftf1dViewType;

typedef struct {
    GwyFftf1dSuppressType suppress;
    GwyFftf1dViewType view_type;
    GwyInterpolationType interpolation;
    GwyOrientation direction;
    gboolean update;
} Fftf1dArgs;

typedef struct {
    GtkWidget *dialog;
    GtkWidget *view_original;
    GtkWidget *view_result;
    GtkWidget *type;
    GtkWidget *update;
    GtkWidget *menu_direction;
    GtkWidget *menu_interpolation;
    GtkWidget *menu_suppress;
    GtkWidget *menu_view_type;
    GtkWidget *graph;
    GwyDataLine *weights;
    GwyGraphModel *gmodel;
    GwyContainer *original_data;
    GwyContainer *result_data;
    GwyDataField *original_field;
    Fftf1dArgs *args;
} Fftf1dControls;

static gboolean   module_register         (void);
static void       fftf_1d                 (GwyContainer *data,
                                           GwyRunType run);
static void       fftf_1d_dialog          (Fftf1dArgs *args,
                                           GwyContainer *data,
                                           GwyDataField *dfield,
                                           gint id);
static void       restore_ps              (Fftf1dControls *controls,
                                           Fftf1dArgs *args);
static void       fftf_1d_load_args       (GwyContainer *container,
                                           Fftf1dArgs *args);
static void       fftf_1d_save_args       (GwyContainer *container,
                                           Fftf1dArgs *args);
static void       fftf_1d_sanitize_args   (Fftf1dArgs *args);
static void       fftf_1d_run             (Fftf1dControls *controls,
                                           Fftf1dArgs *args);
static void       fftf_1d_do              (Fftf1dControls *controls,
                                           gint id);
static void       fftf_1d_dialog_abandon  (Fftf1dControls *controls,
                                           Fftf1dArgs *args);
static void       suppress_changed_cb     (GtkWidget *combo,
                                           Fftf1dControls *controls);
static void       view_type_changed_cb    (GtkWidget *combo,
                                           Fftf1dControls *controls);
static void       direction_changed_cb    (GtkWidget *combo,
                                           Fftf1dControls *controls);
static void       interpolation_changed_cb(GtkWidget *combo,
                                           Fftf1dControls *controls);
static void       update_changed_cb       (GtkToggleButton *button,
                                           Fftf1dControls *controls);
static void       update_view             (Fftf1dControls *controls,
                                           Fftf1dArgs *args);
static void       graph_selected          (GwySelection* selection,
                                           gint i,
                                           Fftf1dControls *controls);


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("FFT filtering"),
    "Petr Klapetek <petr@klapetek.cz>",
    "2.8",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_process_func_register("fft_filter_1d",
                              (GwyProcessFunc)&fftf_1d,
                              N_("/_Correct Data/1D _FFT Filtering..."),
                              NULL,
                              FFTF_1D_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("1D FFT Filtering"));
    return TRUE;
}

static void
fftf_1d(GwyContainer *data, GwyRunType run)
{
    Fftf1dArgs args;
    GwyDataField *dfield;
    gint id;

    g_return_if_fail(run & FFTF_1D_RUN_MODES);

    fftf_1d_load_args(gwy_app_settings_get(), &args);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield);

    fftf_1d_dialog(&args, data, dfield, id);
}

static void
fftf_1d_dialog(Fftf1dArgs *args,
               GwyContainer *data,
               GwyDataField *dfield,
               gint id)
{
    static const GwyEnum view_types[] = {
        { N_("Marked"),    GWY_FFTF_1D_VIEW_MARKED,    },
        { N_("Unmarked"),  GWY_FFTF_1D_VIEW_UNMARKED,  },
    };
    static const GwyEnum suppress_types[] = {
        { N_("Null"),      GWY_FFTF_1D_SUPPRESS_NULL,         },
        { N_("Suppress"),  GWY_FFTF_1D_SUPPRESS_NEIGBOURHOOD, },
    };

    GtkWidget *dialog, *table, *hbox, *align;
    Fftf1dControls controls;
    GwyDataField *result_field;
    GwyGraphArea *area;
    GwySelection *selection;
    gint response, row;

    dialog = gtk_dialog_new_with_buttons(_("1D FFT filter"), NULL, 0, NULL);
    controls.dialog = dialog;
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Update"),
                                                           GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), RESPONSE_PREVIEW,
                                      !args->update);
    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CLEAR, RESPONSE_CLEAR);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    /* store pointer to data container */
    controls.args = args;
    controls.original_data = data;
    controls.original_field = dfield;

    /*setup result container*/
    result_field = gwy_data_field_new_alike(dfield, TRUE);
    controls.result_data = gwy_container_new();
    gwy_container_set_object_by_name(controls.result_data, "/0/data",
                                     result_field);
    gwy_app_sync_data_items(data, controls.result_data, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    g_object_unref(result_field);

    controls.weights = NULL;

    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    /*set up rescaled image of the surface*/
    controls.view_original = create_preview(controls.original_data,
                                            id, PREVIEW_SMALL_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.view_original, FALSE, FALSE, 4);

    /*set up rescaled image of the result*/
    controls.view_result = create_preview(controls.result_data,
                                          0, PREVIEW_SMALL_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.view_result, FALSE, FALSE, 4);

    /*settings*/
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       TRUE, TRUE, 4);

    /* `select areas with mouse' should be a tooltip or something...
    gtk_label_set_markup(GTK_LABEL(label),
                         _("<b>Power spectrum (select areas by mouse):</b>"));
                         */
    controls.gmodel = gwy_graph_model_new();
    controls.graph = gwy_graph_new(controls.gmodel);
    gwy_graph_set_status(GWY_GRAPH(controls.graph), GWY_GRAPH_STATUS_XSEL);
    gtk_widget_set_size_request(controls.graph, -1, PREVIEW_HALF_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.graph, TRUE, TRUE, 4);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls.graph)));
    selection = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);

    gwy_selection_set_max_objects(selection, 20);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(graph_selected), &controls);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    table = gtk_table_new(6, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(align), table);
    row = 0;

    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Options")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.menu_direction
        = gwy_enum_combo_box_new(gwy_orientation_get_enum(), -1,
                                 G_CALLBACK(direction_changed_cb), &controls,
                                 args->direction, TRUE);
    gwy_table_attach_row(table, row, _("_Direction:"), NULL,
                         controls.menu_direction);
    row++;

    controls.menu_suppress
        = gwy_enum_combo_box_new(suppress_types, G_N_ELEMENTS(suppress_types),
                                 G_CALLBACK(suppress_changed_cb), &controls,
                                 args->suppress, TRUE);
    gwy_table_attach_row(table, row, _("_Suppress type:"), NULL,
                         controls.menu_suppress);
    row++;

    controls.menu_view_type
        = gwy_enum_combo_box_new(view_types, G_N_ELEMENTS(view_types),
                                 G_CALLBACK(view_type_changed_cb), &controls,
                                 args->view_type, TRUE);
    gwy_table_attach_row(table, row, _("_Filter type:"), NULL,
                         controls.menu_view_type);
    row++;

    controls.menu_interpolation
        = gwy_enum_combo_box_new(gwy_interpolation_type_get_enum(), -1,
                                 G_CALLBACK(interpolation_changed_cb),
                                 &controls,
                                 args->interpolation, TRUE);
    gwy_table_attach_row(table, row, _("_Interpolation type:"), NULL,
                         controls.menu_interpolation);
    gtk_table_set_row_spacing(GTK_TABLE(table), row, 8);
    row++;

    controls.update = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_table_attach(GTK_TABLE(table), controls.update,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                 args->update);
    g_signal_connect(controls.update, "toggled",
                     G_CALLBACK(update_changed_cb), &controls);
    row++;

    restore_ps(&controls, args);
    update_view(&controls, args);

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            fftf_1d_save_args(gwy_app_settings_get(), args);
            return;
            break;

            case GTK_RESPONSE_OK:
            fftf_1d_save_args(gwy_app_settings_get(), args);
            fftf_1d_do(&controls, id);
            break;

            case RESPONSE_PREVIEW:
            fftf_1d_run(&controls, args);
            break;

            case RESPONSE_CLEAR:
            restore_ps(&controls, args);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    fftf_1d_dialog_abandon(&controls, args);
}

static void
fftf_1d_dialog_abandon(Fftf1dControls *controls,
                       G_GNUC_UNUSED Fftf1dArgs *args)
{
    g_object_unref(controls->result_data);
}

/*update preview depending on user's wishes*/
static void
update_view(Fftf1dControls *controls,
            Fftf1dArgs *args)
{
    GwyDataField *rfield;

    gwy_debug("args->update = %d", args->update);
    rfield = gwy_container_get_object_by_name(controls->result_data, "/0/data");
    gwy_data_field_fft_filter_1d(controls->original_field, rfield,
                                 controls->weights, args->direction,
                                 args->interpolation);

    gwy_data_field_data_changed(rfield);
}

static void
restore_ps(Fftf1dControls *controls, Fftf1dArgs *args)
{
    GwyDataField *dfield;
    GwyDataLine *dline;
    GwyGraphCurveModel *cmodel;
    GwyGraphArea *area;
    GwySelection *selection;
    gint nofselection, i, res;
    gdouble m;
    gdouble *d;

    dfield = controls->original_field;
    res = gwy_data_field_get_xres(dfield);
    dline = gwy_data_line_new(res, res, FALSE);

    gwy_data_field_psdf(dfield, dline, args->direction, args->interpolation,
                        GWY_WINDOWING_RECT, 0);
    if (!controls->weights)
        controls->weights = gwy_data_line_new(dline->res, dline->real, FALSE);
    gwy_data_line_fill(controls->weights, 1.0);

    /* use magnitude instead of power so smaller components become visible */
    m = gwy_data_line_get_max(dline);
    d = gwy_data_line_get_data(dline);
    res = gwy_data_line_get_res(dline);
    for (i = 0; i < res; i++)
        d[i] = d[i] > 0.0 ? sqrt(d[i]/m) : 0.0;

    gwy_graph_model_remove_all_curves(controls->gmodel);

    cmodel = gwy_graph_curve_model_new();
    gwy_graph_curve_model_set_data_from_dataline(cmodel, dline, 0, 0);
    g_object_set(cmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "description", "Fourier Modulus Density",
                  NULL);
    g_object_set(controls->gmodel,
                 "si-unit-x", gwy_data_line_get_si_unit_x(dline),
                 "axis-label-bottom", "k",
                 "axis-label-left", "",
                 NULL);

    gwy_graph_model_add_curve(controls->gmodel, cmodel);
    g_object_unref(dline);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls->graph)));
    selection = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);
    nofselection = gwy_selection_get_data(selection, NULL);

    if (nofselection != 0)
        gwy_selection_clear(selection);

    if (args->update)
        update_view(controls, args);
}

static void
graph_selected(GwySelection* selection,
               G_GNUC_UNUSED gint i,
               Fftf1dControls *controls)
{
    gint k, nofselection;
    gdouble beg, end;
    gdouble *selection_data;
    gint fill_from, fill_to;

    nofselection = gwy_selection_get_data(selection, NULL);
    if (nofselection == 0) {
        restore_ps(controls, controls->args);
    }
    else {
        selection_data = g_new(gdouble, 2*nofselection);
        gwy_selection_get_data(selection, selection_data);

        /*setup weights for inverse FFT computation*/
        if (!controls->weights) {
            gint res = gwy_data_field_get_xres(controls->original_field);
            controls->weights = gwy_data_line_new(res, res, FALSE);
        }

        if (controls->args->view_type == GWY_FFTF_1D_VIEW_UNMARKED) {
            gwy_data_line_fill(controls->weights, 1.0);

            for (k = 0; k < nofselection; k++) {
                beg = selection_data[2*k];
                end = selection_data[2*k+1];
                fill_from = MAX(0, gwy_data_line_rtoi(controls->weights, beg));
                fill_from = MIN(controls->weights->res, fill_from);
                fill_to = MIN(controls->weights->res,
                              gwy_data_line_rtoi(controls->weights, end));

                if (controls->args->suppress == GWY_FFTF_1D_SUPPRESS_NULL)
                    gwy_data_line_part_fill(controls->weights,
                                            fill_from, fill_to, 0);
                else /*TODO put here at least some linear interpolation*/
                    gwy_data_line_part_fill(controls->weights,
                                            fill_from, fill_to, 0.3);
            }
            if (controls->args->update)
                update_view(controls, controls->args);
        }
        if (controls->args->view_type == GWY_FFTF_1D_VIEW_MARKED) {
            gwy_data_line_fill(controls->weights, 0.0);

            for (k = 0; k < nofselection; k++) {
                beg = selection_data[2*k];
                end = selection_data[2*k+1];

                fill_from = MAX(0, gwy_data_line_rtoi(controls->weights, beg));
                fill_from = MIN(controls->weights->res, fill_from);
                fill_to = MIN(controls->weights->res,
                              gwy_data_line_rtoi(controls->weights, end));

                gwy_data_line_part_fill(controls->weights, fill_from, fill_to,
                                        1.0);
            }

            if (controls->args->update)
                update_view(controls, controls->args);
        }
        g_free(selection_data);
    }
}

/*fit data*/
static void
fftf_1d_run(Fftf1dControls *controls,
            Fftf1dArgs *args)
{
    update_view(controls, args);
}

/*dialog finished, export result data*/
static void
fftf_1d_do(Fftf1dControls *controls, gint id)
{
    GwyDataField *rfield;
    gint newid;

    rfield = gwy_container_get_object_by_name(controls->result_data, "/0/data");
    newid = gwy_app_data_browser_add_data_field(rfield, controls->original_data,
                                                TRUE);
    gwy_app_set_data_field_title(controls->original_data, newid,
                                 _("1D FFT Filtered Data"));
    gwy_app_channel_log_add_proc(controls->original_data, id, newid);
}

static void
update_changed_cb(GtkToggleButton *button, Fftf1dControls *controls)
{
    controls->args->update = gtk_toggle_button_get_active(button);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_PREVIEW,
                                      !controls->args->update);
    if (controls->args->update)
        update_view(controls, controls->args);
}

static void
suppress_changed_cb(GtkWidget *combo,
                    Fftf1dControls *controls)
{
    GwyGraphArea *area;
    GwySelection *selection;

    controls->args->suppress
        = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    if (controls->args->suppress == GWY_FFTF_1D_SUPPRESS_NEIGBOURHOOD) {
        controls->args->view_type = GWY_FFTF_1D_VIEW_UNMARKED;
        gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->menu_view_type),
                                      controls->args->view_type);
        gtk_widget_set_sensitive(controls->menu_view_type, FALSE);
    }
    else
        gtk_widget_set_sensitive(controls->menu_view_type, TRUE);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls->graph)));
    selection = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);

    graph_selected(selection, 0, controls);
    update_view(controls, controls->args);
}

static void
view_type_changed_cb(GtkWidget *combo,
                     Fftf1dControls *controls)
{
    GwyGraphArea *area;
    GwySelection *selection;

    controls->args->view_type
        = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls->graph)));
    selection = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);

    graph_selected(selection, 0, controls);
    update_view(controls, controls->args);
}

static void
direction_changed_cb(GtkWidget *combo,
                     Fftf1dControls *controls)
{
    controls->args->direction
        = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    restore_ps(controls, controls->args);
}

static void
interpolation_changed_cb(GtkWidget *combo,
                         Fftf1dControls *controls)
{
    controls->args->interpolation
        = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    update_view(controls, controls->args);
}

static const gchar suppress_key[]      = "/module/fft_filter_1d/suppress";
static const gchar update_key[]        = "/module/fft_filter_1d/update";
static const gchar view_key[]          = "/module/fft_filter_1d/view";
static const gchar direction_key[]     = "/module/fft_filter_1d/direction";
static const gchar interpolation_key[] = "/module/fft_filter_1d/interpolation";

static void
fftf_1d_sanitize_args(Fftf1dArgs *args)
{
    args->suppress = MIN(args->suppress, GWY_FFTF_1D_SUPPRESS_NEIGBOURHOOD);
    args->view_type = MIN(args->view_type, GWY_FFTF_1D_VIEW_UNMARKED);
    args->direction = gwy_enum_sanitize_value(args->direction,
                                              GWY_TYPE_ORIENTATION);
    args->interpolation = gwy_enum_sanitize_value(args->interpolation,
                                                  GWY_TYPE_INTERPOLATION_TYPE);
    args->update = !!args->update;

    if (args->suppress == GWY_FFTF_1D_SUPPRESS_NEIGBOURHOOD)
        args->view_type = GWY_FFTF_1D_VIEW_UNMARKED;
}

static void
fftf_1d_load_args(GwyContainer *container,
                  Fftf1dArgs *args)
{
    args->interpolation = GWY_INTERPOLATION_LINEAR;
    args->direction = GWY_ORIENTATION_HORIZONTAL;
    args->suppress = GWY_FFTF_1D_SUPPRESS_NULL;

    gwy_container_gis_enum_by_name(container, suppress_key, &args->suppress);
    gwy_container_gis_enum_by_name(container, view_key, &args->view_type);
    gwy_container_gis_enum_by_name(container, direction_key, &args->direction);
    gwy_container_gis_enum_by_name(container, interpolation_key,
                                   &args->interpolation);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);

    fftf_1d_sanitize_args(args);
}

static void
fftf_1d_save_args(GwyContainer *container,
                    Fftf1dArgs *args)
{
    gwy_container_set_enum_by_name(container, suppress_key, args->suppress);
    gwy_container_set_enum_by_name(container, view_key, args->view_type);
    gwy_container_set_enum_by_name(container, direction_key, args->direction);
    gwy_container_set_enum_by_name(container, interpolation_key,
                                   args->interpolation);
    gwy_container_set_boolean_by_name(container, update_key, args->update);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
