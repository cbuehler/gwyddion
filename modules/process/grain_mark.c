/*
 *  @(#) $Id$
 *  Copyright (C) 2004-2015 David Necas (Yeti), Petr Klapetek.
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwylayer-mask.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define MARK_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 400
};

enum {
    MARK_HEIGHT = 0,
    MARK_SLOPE  = 1,
    MARK_LAP    = 2
};

enum {
    RESPONSE_RESET   = 1,
    RESPONSE_PREVIEW = 2
};

typedef struct {
    gboolean inverted;
    gdouble height;
    gdouble slope;
    gdouble lap;
    gboolean is_height;
    gboolean is_slope;
    gboolean is_lap;
    gboolean combine;
    gboolean update;
    GwyMergeType merge_type;
    GwyMergeType combine_type;

    /* interface only */
    gboolean computed;
} MarkArgs;

typedef struct {
    GtkWidget *dialog;
    GtkWidget *inverted;
    GtkWidget *view;
    GtkWidget *is_height;
    GtkWidget *is_slope;
    GtkWidget *is_lap;
    GtkObject *threshold_height;
    GtkWidget *value_height;
    GwySIValueFormat *format_height;
    GtkObject *threshold_slope;
    GtkObject *threshold_lap;
    GtkWidget *merge;
    GtkWidget *combine;
    GtkWidget *combine_type;
    GtkWidget *color_button;
    GtkWidget *update;
    GwyContainer *mydata;
    MarkArgs *args;
    gboolean in_init;
    gdouble min_height;
    gdouble max_height;
} MarkControls;

static gboolean    module_register            (void);
static void        grain_mark                 (GwyContainer *data,
                                               GwyRunType run);
static void        run_noninteractive         (MarkArgs *args,
                                               GwyContainer *data,
                                               GwyDataField *dfield,
                                               GwyDataField *existing_mask,
                                               GQuark mquark);
static void        mark_dialog                (MarkArgs *args,
                                               GwyContainer *data,
                                               GwyDataField *dfield,
                                               GwyDataField *existing_mask,
                                               gint id,
                                               GQuark mquark);
static void        mask_color_changed         (GtkWidget *color_button,
                                               MarkControls *controls);
static void        load_mask_color            (GtkWidget *color_button,
                                               GwyContainer *data);
static void        mark_dialog_update_controls(MarkControls *controls,
                                               MarkArgs *args);
static void        mark_dialog_update_values  (MarkControls *controls,
                                               MarkArgs *args);
static void        update_threshold_value     (MarkControls *controls);
static void        update_change_cb           (MarkControls *controls);
static void        mark_invalidate            (MarkControls *controls);
static void        mark_invalidate2           (gpointer whatever,
                                               MarkControls *controls);
static void        preview                    (MarkControls *controls,
                                               MarkArgs *args);
static void        mask_process               (GwyDataField *dfield,
                                               GwyDataField *existing_mask,
                                               GwyDataField *maskfield,
                                               MarkArgs *args);
static void        mark_load_args             (GwyContainer *container,
                                               MarkArgs *args);
static void        mark_save_args             (GwyContainer *container,
                                               MarkArgs *args);
static void        mark_sanitize_args         (MarkArgs *args);


static const MarkArgs mark_defaults = {
    FALSE,
    50,
    50,
    50,
    TRUE,
    FALSE,
    FALSE,
    FALSE,
    TRUE,
    GWY_MERGE_UNION,
    GWY_MERGE_UNION,
    FALSE,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Marks grains by thresholding (height, slope, curvature)."),
    "Petr Klapetek <petr@klapetek.cz>",
    "1.18",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_mark",
                              (GwyProcessFunc)&grain_mark,
                              N_("/_Grains/_Mark by Threshold..."),
                              GWY_STOCK_GRAINS,
                              MARK_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark grains by threshold"));

    return TRUE;
}

static void
grain_mark(GwyContainer *data, GwyRunType run)
{
    MarkArgs args;
    GwyDataField *dfield, *mask;
    GQuark mquark;
    gint id;

    g_return_if_fail(run & MARK_RUN_MODES);
    mark_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_MASK_FIELD, &mask,
                                     0);
    g_return_if_fail(dfield && mquark);

    if (run == GWY_RUN_IMMEDIATE) {
        run_noninteractive(&args, data, dfield, mask, mquark);
        gwy_app_channel_log_add_proc(data, id, id);
    }
    else {
        mark_dialog(&args, data, dfield, mask, id, mquark);
    }
}

static void
table_attach_threshold(GtkWidget *table, gint *row, const gchar *name,
                       GtkObject **adj, gdouble value,
                       GtkWidget **check,
                       gpointer data)
{
    *adj = gtk_adjustment_new(value, 0.0, 100.0, 0.1, 5, 0);
    gwy_table_attach_hscale(table, *row, name, "%", *adj, GWY_HSCALE_CHECK);
    *check = gwy_table_hscale_get_check(*adj);
    g_signal_connect_swapped(*adj, "value-changed",
                             G_CALLBACK(mark_invalidate), data);
    g_signal_connect_swapped(*check, "toggled",
                             G_CALLBACK(mark_invalidate), data);
    (*row)++;
}

static GwyDataField*
create_mask_field(GwyDataField *dfield)
{
    GwyDataField *mfield;
    GwySIUnit *siunit;

    mfield = gwy_data_field_new_alike(dfield, FALSE);
    siunit = gwy_si_unit_new(NULL);
    gwy_data_field_set_si_unit_z(mfield, siunit);
    g_object_unref(siunit);

    return mfield;
}

static void
run_noninteractive(MarkArgs *args,
                   GwyContainer *data,
                   GwyDataField *dfield,
                   GwyDataField *existing_mask,
                   GQuark mquark)
{
    GwyDataField *mfield;

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    mfield = create_mask_field(dfield);
    mask_process(dfield, existing_mask, mfield, args);
    gwy_container_set_object(data, mquark, mfield);
    g_object_unref(mfield);
}

static void
mark_dialog(MarkArgs *args,
            GwyContainer *data,
            GwyDataField *dfield,
            GwyDataField *existing_mask,
            gint id,
            GQuark mquark)
{
    GtkWidget *dialog, *table, *hbox, *label, *pivot;
    MarkControls controls;
    gint response;
    GwyPixmapLayer *layer;
    GwyDataField *mfield;
    gint row;
    gboolean temp;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.in_init = TRUE;
    gwy_data_field_get_min_max(dfield,
                               &controls.min_height, &controls.max_height);

    dialog = gtk_dialog_new_with_buttons(_("Mark Grains by Threshold"), NULL, 0,
                                         NULL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Update"),
                                                           GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    if (existing_mask)
        gwy_container_set_object_by_name(controls.mydata, "/1/mask",
                                         existing_mask);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    controls.view = gwy_data_view_new(controls.mydata);
    layer = gwy_layer_basic_new();
    g_object_set(layer,
                 "data-key", "/0/data",
                 "gradient-key", "/0/base/palette",
                 "range-type-key", "/0/base/range-type",
                 "min-max-key", "/0/base",
                 NULL);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(controls.view), "/0/data");
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(controls.view), layer);
    gwy_set_data_preview_size(GWY_DATA_VIEW(controls.view), PREVIEW_SIZE);

    gtk_box_pack_start(GTK_BOX(hbox), controls.view, FALSE, FALSE, 4);

    table = gtk_table_new(10 + 2*(!!existing_mask), 4, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 4);
    row = 0;

    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Threshold by")),
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    table_attach_threshold(table, &row, _("_Height:"),
                           &controls.threshold_height, args->height,
                           &controls.is_height, &controls);
    pivot = gwy_table_hscale_get_scale(controls.threshold_height);

    controls.value_height = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.value_height), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.value_height,
                     2, 3, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.threshold_height, "value-changed",
                             G_CALLBACK(update_threshold_value), &controls);
    gwy_widget_sync_sensitivity(pivot, controls.value_height);

    controls.format_height
        = gwy_data_field_get_value_format_z(dfield, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                            NULL);
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(label), controls.format_height->units);
    gtk_table_attach(GTK_TABLE(table), label,
                     3, 4, row, row+1, GTK_FILL, 0, 0, 0);
    gwy_widget_sync_sensitivity(pivot, label);

    gtk_table_set_row_spacing(GTK_TABLE(table), row, 8);
    row++;

    table_attach_threshold(table, &row, _("_Slope:"),
                           &controls.threshold_slope, args->slope,
                           &controls.is_slope, &controls);

    table_attach_threshold(table, &row, _("_Curvature:"),
                           &controls.threshold_lap, args->lap,
                           &controls.is_lap, &controls);
    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);

    controls.merge
        = gwy_enum_combo_box_new(gwy_merge_type_get_enum(), -1,
                                 G_CALLBACK(mark_invalidate2), &controls,
                                 args->merge_type, TRUE);
    gwy_table_attach_hscale(table, row, _("Criteria combination:"), NULL,
                            GTK_OBJECT(controls.merge), GWY_HSCALE_WIDGET);
    row++;

    controls.inverted = gtk_check_button_new_with_mnemonic(_("_Invert height"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.inverted),
                                 args->inverted);
    gtk_table_attach(GTK_TABLE(table), controls.inverted,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.inverted, "toggled",
                             G_CALLBACK(mark_invalidate), &controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Options")),
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    if (existing_mask) {
        controls.combine
            = gtk_check_button_new_with_mnemonic(_("Com_bine with "
                                                   "existing mask"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.combine),
                                     args->combine);
        gtk_table_attach(GTK_TABLE(table), controls.combine,
                         0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
        g_signal_connect_swapped(controls.combine, "toggled",
                                 G_CALLBACK(mark_invalidate), &controls);
        row++;

        controls.combine_type
            = gwy_enum_combo_box_new(gwy_merge_type_get_enum(), -1,
                                     G_CALLBACK(mark_invalidate2), &controls,
                                     args->combine_type, TRUE);
        gwy_table_attach_hscale(table, row, _("Operation:"), NULL,
                                GTK_OBJECT(controls.combine_type),
                                GWY_HSCALE_WIDGET);
        gtk_table_set_row_spacing(GTK_TABLE(table), row, 8);
        row++;
    }

    controls.color_button = gwy_color_button_new();
    gwy_color_button_set_use_alpha(GWY_COLOR_BUTTON(controls.color_button),
                                   TRUE);
    load_mask_color(controls.color_button,
                    gwy_data_view_get_data(GWY_DATA_VIEW(controls.view)));
    gwy_table_attach_hscale(table, row++, _("_Mask color:"), NULL,
                            GTK_OBJECT(controls.color_button),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    g_signal_connect(controls.color_button, "clicked",
                     G_CALLBACK(mask_color_changed), &controls);
    row++;

    controls.update = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                 args->update);
    gtk_table_attach(GTK_TABLE(table), controls.update,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.update, "toggled",
                             G_CALLBACK(update_change_cb), &controls);

    mark_invalidate(&controls);

    /* cheap sync */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.is_height),
                                 !args->is_height);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.is_height),
                                 args->is_height);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.is_slope),
                                 !args->is_slope);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.is_slope),
                                 args->is_slope);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.is_lap),
                                 !args->is_lap);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.is_lap),
                                 args->is_lap);

    /* finished initializing, allow instant updates */
    controls.in_init = FALSE;
    update_threshold_value(&controls);

    /* show initial preview if instant updates are on */
    if (args->update) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(controls.dialog),
                                          RESPONSE_PREVIEW, FALSE);
        preview(&controls, args);
    }

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            mark_dialog_update_values(&controls, args);
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            g_object_unref(controls.mydata);
            gwy_si_unit_value_format_free(controls.format_height);
            mark_save_args(gwy_app_settings_get(), args);
            return;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            temp = args->update;
            *args = mark_defaults;
            args->update = temp;
            controls.in_init = TRUE;
            mark_dialog_update_controls(&controls, args);
            preview(&controls, args);
            controls.in_init = FALSE;
            break;

            case RESPONSE_PREVIEW:
            mark_dialog_update_values(&controls, args);
            preview(&controls, args);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    mark_dialog_update_values(&controls, args);
    gwy_app_sync_data_items(controls.mydata, data, 0, id, FALSE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gtk_widget_destroy(dialog);
    gwy_si_unit_value_format_free(controls.format_height);

    if (args->computed) {
        mfield = gwy_container_get_object_by_name(controls.mydata, "/0/mask");
        gwy_app_undo_qcheckpointv(data, 1, &mquark);
        gwy_container_set_object(data, mquark, mfield);
        g_object_unref(controls.mydata);
    }
    else {
        g_object_unref(controls.mydata);
        run_noninteractive(args, data, dfield, existing_mask, mquark);
    }

    mark_save_args(gwy_app_settings_get(), args);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
mark_dialog_update_controls(MarkControls *controls,
                            MarkArgs *args)
{
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->threshold_height),
                             args->height);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->threshold_slope),
                             args->slope);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->threshold_lap),
                             args->lap);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->is_height),
                                 args->is_height);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->inverted),
                                 args->inverted);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->is_slope),
                                 args->is_slope);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->is_lap),
                                 args->is_lap);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->update),
                                 args->update);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->merge),
                                  args->merge_type);
    if (controls->combine) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->combine),
                                     args->combine);
        gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->combine_type),
                                      args->combine_type);
    }
}

static void
mark_dialog_update_values(MarkControls *controls,
                          MarkArgs *args)
{
    args->height
        = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->threshold_height));
    args->slope
        = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->threshold_slope));
    args->lap
        = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->threshold_lap));
    args->is_height
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->is_height));
    args->inverted
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->inverted));
    args->is_slope
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->is_slope));
    args->is_lap
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->is_lap));
    args->update
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->update));
    args->merge_type
        = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->merge));
    if (controls->combine) {
        args->combine = gtk_toggle_button_get_active
                                       (GTK_TOGGLE_BUTTON(controls->combine));
        args->combine_type = gwy_enum_combo_box_get_active
                                      (GTK_COMBO_BOX(controls->combine_type));
    }
}

static void
mark_invalidate(MarkControls *controls)
{
    controls->args->computed = FALSE;

    /* create preview if instant updates are on */
    if (controls->args->update && !controls->in_init) {
        mark_dialog_update_values(controls, controls->args);
        preview(controls, controls->args);
    }
}

static void
update_threshold_value(MarkControls *controls)
{
    gdouble v;
    gchar *s;

    v = GTK_ADJUSTMENT(controls->threshold_height)->value/100.0;
    v = (1.0 - v)*controls->min_height + v*controls->max_height;
    s = g_strdup_printf("%.*f",
                        controls->format_height->precision,
                        v/controls->format_height->magnitude);
    gtk_label_set_markup(GTK_LABEL(controls->value_height), s);
    g_free(s);
}

static void
update_change_cb(MarkControls *controls)
{
    controls->args->update
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->update));

    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_PREVIEW,
                                      !controls->args->update);

    if (controls->args->update)
        mark_invalidate(controls);
}

static void
mark_invalidate2(G_GNUC_UNUSED gpointer whatever, MarkControls *controls)
{
    mark_invalidate(controls);
}

static void
mask_color_changed(GtkWidget *color_button,
                   MarkControls *controls)
{
    GwyContainer *data;

    data = gwy_data_view_get_data(GWY_DATA_VIEW(controls->view));
    gwy_mask_color_selector_run(NULL, GTK_WINDOW(controls->dialog),
                                GWY_COLOR_BUTTON(color_button), data,
                                "/0/mask");
    load_mask_color(color_button, data);
}

static void
load_mask_color(GtkWidget *color_button,
                GwyContainer *data)
{
    GwyRGBA rgba;

    if (!gwy_rgba_get_from_container(&rgba, data, "/0/mask")) {
        gwy_rgba_get_from_container(&rgba, gwy_app_settings_get(), "/mask");
        gwy_rgba_store_to_container(&rgba, data, "/0/mask");
    }
    gwy_color_button_set_color(GWY_COLOR_BUTTON(color_button), &rgba);
}

static void
preview(MarkControls *controls,
        MarkArgs *args)
{
    GwyDataField *mask, *dfield, *existing_mask = NULL;
    GwyPixmapLayer *layer;

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                             "/0/data"));
    gwy_container_gis_object_by_name(controls->mydata, "/1/mask",
                                     (GObject**)&existing_mask);

    /* Set up the mask */
    if (!gwy_container_gis_object_by_name(controls->mydata, "/0/mask", &mask)) {
        mask = create_mask_field(dfield);
        gwy_container_set_object_by_name(controls->mydata, "/0/mask", mask);
        g_object_unref(mask);

        layer = gwy_layer_mask_new();
        gwy_pixmap_layer_set_data_key(layer, "/0/mask");
        gwy_layer_mask_set_color_key(GWY_LAYER_MASK(layer), "/0/mask");
        gwy_data_view_set_alpha_layer(GWY_DATA_VIEW(controls->view), layer);
    }
    gwy_data_field_copy(dfield, mask, FALSE);
    mask_process(dfield, existing_mask, mask, args);
    gwy_data_field_data_changed(mask);

    args->computed = TRUE;
}

static void
mask_process(GwyDataField *dfield,
             GwyDataField *existing_mask,
             GwyDataField *maskfield,
             MarkArgs *args)
{
    GwyDataField *output_field;
    gboolean is_field;

    is_field = FALSE;
    output_field = gwy_data_field_new_alike(dfield, FALSE);

    if (args->is_height) {
        gwy_data_field_grains_mark_height(dfield, maskfield, args->height,
                                          args->inverted);
        is_field = TRUE;
    }
    if (args->is_slope) {
        gwy_data_field_grains_mark_slope(dfield, output_field,
                                         args->slope, FALSE);
        if (is_field) {
            if (args->merge_type == GWY_MERGE_UNION)
                gwy_data_field_grains_add(maskfield, output_field);
            else if (args->merge_type == GWY_MERGE_INTERSECTION)
                gwy_data_field_grains_intersect(maskfield, output_field);
        }
        else
            gwy_data_field_copy(output_field, maskfield, FALSE);
        is_field = TRUE;
    }
    if (args->is_lap) {
        gwy_data_field_grains_mark_curvature(dfield, output_field,
                                             args->lap, FALSE);
        if (is_field) {
            if (args->merge_type == GWY_MERGE_UNION)
                gwy_data_field_grains_add(maskfield, output_field);
            else if (args->merge_type == GWY_MERGE_INTERSECTION)
                gwy_data_field_grains_intersect(maskfield, output_field);
        }
        else
            gwy_data_field_copy(output_field, maskfield, FALSE);
    }
    if (existing_mask && args->combine) {
        if (args->combine_type == GWY_MERGE_UNION)
            gwy_data_field_grains_add(maskfield, existing_mask);
        else if (args->combine_type == GWY_MERGE_INTERSECTION)
            gwy_data_field_grains_intersect(maskfield, existing_mask);
    }

    g_object_unref(output_field);
}

static const gchar combine_key[]      = "/module/grain_mark/combine";
static const gchar combine_type_key[] = "/module/grain_mark/combine_type";
static const gchar height_key[]       = "/module/grain_mark/height";
static const gchar inverted_key[]     = "/module/grain_mark/inverted";
static const gchar isheight_key[]     = "/module/grain_mark/isheight";
static const gchar islap_key[]        = "/module/grain_mark/islap";
static const gchar isslope_key[]      = "/module/grain_mark/isslope";
static const gchar lap_key[]          = "/module/grain_mark/lap";
static const gchar merge_type_key[]   = "/module/grain_mark/merge_type";
static const gchar slope_key[]        = "/module/grain_mark/slope";
static const gchar update_key[]       = "/module/grain_mark/update";

static void
mark_sanitize_args(MarkArgs *args)
{
    args->inverted = !!args->inverted;
    args->is_slope = !!args->is_slope;
    args->is_height = !!args->is_height;
    args->is_lap = !!args->is_lap;
    args->combine = !!args->combine;
    args->update = !!args->update;
    args->height = CLAMP(args->height, 0.0, 100.0);
    args->slope = CLAMP(args->slope, 0.0, 100.0);
    args->lap = CLAMP(args->lap, 0.0, 100.0);
    args->merge_type = MIN(args->merge_type, GWY_MERGE_INTERSECTION);
    args->combine_type = MIN(args->combine_type, GWY_MERGE_INTERSECTION);
}

static void
mark_load_args(GwyContainer *container,
               MarkArgs *args)
{
    *args = mark_defaults;

    gwy_container_gis_boolean_by_name(container, inverted_key, &args->inverted);
    gwy_container_gis_boolean_by_name(container, isheight_key,
                                      &args->is_height);
    gwy_container_gis_boolean_by_name(container, isslope_key, &args->is_slope);
    gwy_container_gis_boolean_by_name(container, islap_key, &args->is_lap);
    gwy_container_gis_boolean_by_name(container, combine_key, &args->combine);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);
    gwy_container_gis_double_by_name(container, height_key, &args->height);
    gwy_container_gis_double_by_name(container, slope_key, &args->slope);
    gwy_container_gis_double_by_name(container, lap_key, &args->lap);
    gwy_container_gis_enum_by_name(container, merge_type_key,
                                   &args->merge_type);
    gwy_container_gis_enum_by_name(container, combine_type_key,
                                   &args->combine_type);
    mark_sanitize_args(args);
}

static void
mark_save_args(GwyContainer *container,
               MarkArgs *args)
{
    gwy_container_set_boolean_by_name(container, inverted_key, args->inverted);
    gwy_container_set_boolean_by_name(container, isheight_key, args->is_height);
    gwy_container_set_boolean_by_name(container, isslope_key, args->is_slope);
    gwy_container_set_boolean_by_name(container, islap_key, args->is_lap);
    gwy_container_set_boolean_by_name(container, combine_key, args->combine);
    gwy_container_set_boolean_by_name(container, update_key, args->update);
    gwy_container_set_double_by_name(container, height_key, args->height);
    gwy_container_set_double_by_name(container, slope_key, args->slope);
    gwy_container_set_double_by_name(container, lap_key, args->lap);
    gwy_container_set_enum_by_name(container, merge_type_key, args->merge_type);
    gwy_container_set_enum_by_name(container, combine_type_key,
                                   args->combine_type);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
