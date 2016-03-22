/*
 *  $Id: xyz_raster.c 18372 2016-03-01 08:51:53Z yeti-dn $
 *  Copyright (C) 2016 David Necas (Yeti).
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwyddion.h>
#include <libprocess/stats.h>
#include <libprocess/grains.h>
#include <libprocess/triangulation.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwyapp.h>

#define XYZDRIFT_RUN_MODES GWY_RUN_INTERACTIVE

#define EPSREL 1e-8

/* Use smaller cell sides than the triangulation algorithm as we only need them
 * for identical point detection and border extension. */
#define CELL_SIDE 1.6

enum {
    PREVIEW_SIZE = 400,
    UNDEF = G_MAXUINT
};

enum {
    GWY_INTERPOLATION_FIELD = -1,
    GWY_INTERPOLATION_AVERAGE = -2,
};

typedef enum {
    GWY_XYZDRIFT_METHOD_POLYNOM   = 0,
    GWY_XYZDRIFT_METHOD_EXPONENTIAL   = 1
} GwyXYZDriftXYType;

typedef enum {
    GWY_XYZDRIFT_ZMETHOD_POLYNOM   = 0,
    GWY_XYZDRIFT_ZMETHOD_EXPONENTIAL   = 1,
    GWY_XYZDRIFT_ZMETHOD_AVERAGE  = 2
} GwyXYZDriftZType;

typedef enum {
    GWY_XYZDRIFT_GRAPH_X   = 0,
    GWY_XYZDRIFT_GRAPH_Y   = 1,
    GWY_XYZDRIFT_GRAPH_Z  = 2
} GwyXYZDriftGraphType;





typedef struct {
    /* XXX: Not all values of interpolation and exterior are possible. */
    gint xres;
    gint yres;

    gdouble xdrift_a;
    gdouble xdrift_b;
    gdouble xdrift_c;
    gdouble ydrift_a;
    gdouble ydrift_b;
    gdouble ydrift_c;
    gdouble zdrift_a;
    gdouble zdrift_b;
    gdouble zdrift_c;
    gint zdrift_average;
  
    gint fit_xdrift;
    gint fit_ydrift;
    gint fit_zdrift;
    GwyXYZDriftZType zdrift_type;
    GwyXYZDriftXYType xdrift_type;
    GwyXYZDriftXYType ydrift_type;
    GwyXYZDriftGraphType graph_type;

    gdouble threshold_time;
    gdouble threshold_length;
    gdouble neighbors;    

    /* Interface only. */
    gdouble xmin;
    gdouble xmax;
    gdouble ymin;
    gdouble ymax;

    

} XYZDriftArgs;

typedef struct {
    GwySurface *surface;
    GwyXYZ *points;
    GwySurface *timesurface;
    GwyXYZ *timepoints;
    GwyXYZ *corpoints;
    guint npoints;
    guint ntimepoints;
    gdouble step;
    gdouble xymag;
    gdouble *xdrift;
    gdouble *ydrift;
    gdouble *zdrift;
    gdouble *time;
} XYZDriftData;

typedef struct {
    XYZDriftArgs *args;
    XYZDriftData *rdata;
    GwyContainer *mydata;
    GtkWidget *dialog;
    GtkWidget *xmin;
    GtkWidget *xmax;
    GtkWidget *ymin;
    GtkWidget *ymax;
    GtkObject *xres;
    GtkObject *yres;
    GtkWidget *xdrift_a;
    GtkWidget *xdrift_b;
    GtkWidget *xdrift_c;
    GtkWidget *xdrift_type;

    GtkWidget *ydrift_a;
    GtkWidget *ydrift_b;
    GtkWidget *ydrift_c;
    GtkWidget *ydrift_type;

    GtkWidget *zdrift_a;
    GtkWidget *zdrift_b;
    GtkWidget *zdrift_c;
    GtkObject *zdrift_average;
    GtkWidget *zdrift_average_spin;
    GtkWidget *zdrift_type;

    GtkWidget *result_x;
    GtkWidget *result_y;
    GtkWidget *result_z;
    GtkWidget *graph_type;

    GtkWidget *fit_xdrift;
    GtkWidget *fit_ydrift;
    GtkWidget *fit_zdrift;

    GtkObject *threshold_time;
    GtkObject *threshold_length;
    GtkObject *neighbors;

    GtkWidget *view;
    GtkWidget *do_preview;
    GtkWidget *error;
    GwyGraphModel *gmodel;
    GtkWidget *graph;
    gboolean in_update;
} XYZDriftControls;


static gboolean      module_register        (void);
static void          xyzdrift                 (GwyContainer *data,
                                             GwyRunType run);
static gboolean      xyzdrift_dialog          (XYZDriftArgs *arg,
                                             XYZDriftData *rdata,
                                             GwyContainer *data,
                                             gint id);
static gint          construct_resolutions  (XYZDriftControls *controls,
                                             GtkTable *table,
                                             gint row);
static gint          construct_physical_dims(XYZDriftControls *controls,
                                             GtkTable *table,
                                             gint row);
static gint          construct_options      (XYZDriftControls *controls,
                                             GtkTable *table,
                                             gint row);
static void          xres_changed           (XYZDriftControls *controls,
                                             GtkAdjustment *adj);
static void          yres_changed           (XYZDriftControls *controls,
                                             GtkAdjustment *adj);
static void          xmin_changed           (XYZDriftControls *controls,
                                             GtkEntry *entry);
static void          xmax_changed           (XYZDriftControls *controls,
                                             GtkEntry *entry);
static void          ymin_changed           (XYZDriftControls *controls,
                                             GtkEntry *entry);
static void          ymax_changed           (XYZDriftControls *controls,
                                             GtkEntry *entry);
static void          xdrift_changed         (XYZDriftControls *controls,
                                             GtkAdjustment *adj);
static void          ydrift_changed         (XYZDriftControls *controls,
                                             GtkAdjustment *adj);
static void          zdrift_changed         (XYZDriftControls *controls,
                                             GtkAdjustment *adj);
static void          neighbors_changed      (XYZDriftControls *controls,
                                             GtkAdjustment *adj);
static void          threshold_changed      (XYZDriftControls *controls,
                                             GtkAdjustment *adj);
static void          zdrift_type_changed    (GtkWidget *combo, 
                                             XYZDriftControls *controls);
static void          graph_changed          (GtkWidget *combo, 
                                             XYZDriftControls *controls);
static void          reset_ranges           (XYZDriftControls *controls);
static void          preview                (XYZDriftControls *controls);
static GwyDataField* xyzdrift_do              (XYZDriftData *rdata,
                                             const XYZDriftArgs *args,
                                             GtkWindow *dialog,
                                             gchar **error);
static void          xyzdrift_free            (XYZDriftData *rdata);
static void          initialize_ranges      (const XYZDriftData *rdata,
                                             XYZDriftArgs *args);
static void          xyzdrift_load_args       (GwyContainer *container,
                                             XYZDriftArgs *args);
static void          xyzdrift_save_args       (GwyContainer *container,
                                             XYZDriftArgs *args);

static void          correct_drift          (GwyXYZ *points, 
                                             gint npoints, 
                                             gdouble *xdrift, 
                                             gdouble *ydrift, 
                                             gdouble *zdrift,
                                             GwyXYZ *corpoints, 
                                             gboolean correctz);

static const XYZDriftArgs xyzdrift_defaults = {
    512, 512,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0, 0, 0, 0, 0, 0, 2,
    1, 10, 100,
    /* Interface only. */
    0.0, 0.0, 0.0, 0.0
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Analyze drift in XYZ data."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek",
    "2016",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_drift",
                          (GwyXYZFunc)&xyzdrift,
                          N_("/Analyze _drift..."),
                          NULL,
                          XYZDRIFT_RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Analyze and/or remove the drift"));

    return TRUE;
}

static void
xyzdrift(GwyContainer *data, GwyRunType run)
{
    XYZDriftArgs args;
    XYZDriftData rdata;
    GQuark key;
    gchar *title;
    int i, tsfound;

    GwyContainer *settings;
    GwySurface *surface = NULL;
    GwyDataField *dfield;
    gboolean ok = TRUE;
    gint id, newid;

    g_return_if_fail(run & XYZDRIFT_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(surface));

    settings = gwy_app_settings_get();
    xyzdrift_load_args(settings, &args);
    gwy_clear(&rdata, 1);

    //link to original points
    rdata.surface = surface;
    rdata.points = surface->data;
    rdata.npoints = surface->n;

    //find timestamp
    tsfound = 0;
    for (i=0; i<10; i++) {
       key = gwy_app_get_surface_key_for_id(i);
       if (!key) continue;

       title = gwy_app_get_surface_title(data, i);
       printf("title %d = %s, key \"%s\", \"%s\" %d\n", i, title, g_quark_to_string(key), "Timestamp", g_strcmp0(g_quark_to_string(key), "Timestamp"));

       if (g_strcmp0(title, "Timestamp")==0) {
          rdata.timesurface = gwy_container_get_object(data, key);
          rdata.timepoints = rdata.timesurface->data;
          rdata.ntimepoints = rdata.timesurface->n;
          tsfound = 1;
          printf("Timestamp found in channel %d\n", i);
          break;
       }
    }
    if (!tsfound) printf("Error, no ts found!\n");

    initialize_ranges(&rdata, &args);

    /*analyse drift*/
    rdata.xdrift = g_new0(gdouble, rdata.npoints);
    rdata.ydrift = g_new0(gdouble, rdata.npoints);
    rdata.zdrift = g_new0(gdouble, rdata.npoints);
    rdata.corpoints = g_new(GwyXYZ, rdata.npoints);
    rdata.time = g_new0(gdouble, rdata.npoints);

    ok = xyzdrift_dialog(&args, &rdata, data, id);

    xyzdrift_save_args(settings, &args);

    if (ok) {
        gchar *error = NULL;
        dfield = xyzdrift_do(&rdata, &args, NULL, &error);
        if (dfield) {
            newid = gwy_app_data_browser_add_data_field(dfield, data, TRUE);
            gwy_app_channel_log_add(data, -1, newid, "xyz::xyz_raster", NULL);
        }
        else {
            /* TODO */
            g_free(error);
        }
    }

    xyzdrift_free(&rdata);
}

static void
upload_values(XYZDriftControls *controls, gboolean x, gboolean y, gboolean z)
{
    gchar buffer[20];

    if (x) {
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->xdrift_a);
        gtk_entry_set_text(GTK_ENTRY(controls->xdrift_a), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->xdrift_b);
        gtk_entry_set_text(GTK_ENTRY(controls->xdrift_b), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->xdrift_c);
        gtk_entry_set_text(GTK_ENTRY(controls->xdrift_c), buffer);
    }
    if (y) {
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->ydrift_a);
        gtk_entry_set_text(GTK_ENTRY(controls->ydrift_a), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->ydrift_b);
        gtk_entry_set_text(GTK_ENTRY(controls->ydrift_b), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->ydrift_c);
        gtk_entry_set_text(GTK_ENTRY(controls->ydrift_c), buffer);
    }

    if (z) {
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->zdrift_a);
        gtk_entry_set_text(GTK_ENTRY(controls->zdrift_a), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->zdrift_b);
        gtk_entry_set_text(GTK_ENTRY(controls->zdrift_b), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.4g", controls->args->zdrift_c);
        gtk_entry_set_text(GTK_ENTRY(controls->zdrift_c), buffer);
    }
}

static void
x_to_inits_cb(G_GNUC_UNUSED GtkButton *button, XYZDriftControls *controls)
{
    upload_values(controls, TRUE, FALSE, FALSE);
}

static void
y_to_inits_cb(G_GNUC_UNUSED GtkButton *button, XYZDriftControls *controls)
{
    upload_values(controls, FALSE, TRUE, FALSE);
}

static void
z_to_inits_cb(G_GNUC_UNUSED GtkButton *button, XYZDriftControls *controls)
{
    upload_values(controls, FALSE, FALSE, TRUE);
}




static gboolean
xyzdrift_dialog(XYZDriftArgs *args,
              XYZDriftData *rdata,
              GwyContainer *data,
              gint id)
{
    GtkWidget *dialog, *vbox, *align, *label, *hbox, *button;
    GwyPixmapLayer *layer;
    GwyDataField *dfield;
    GtkTable *table;
    XYZDriftControls controls;
    gint row, response;
    const guchar *gradient;
    GQuark quark;

    controls.args = args;
    controls.rdata = rdata;

    controls.mydata = gwy_container_new();

    dialog = gtk_dialog_new_with_buttons(_("Rasterize XYZ Data"), NULL, 0,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_xyz_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 20);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 0);

    /* Left column */
    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(10, 5, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_container_add(GTK_CONTAINER(align), GTK_WIDGET(table));
    row = 0;

    row = construct_resolutions(&controls, table, row);
    row = construct_physical_dims(&controls, table, row);

    button = gtk_button_new_with_mnemonic(_("Reset Ran_ges"));
    gtk_table_attach(table, button, 1, 4, row, row+1,
                     GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(reset_ranges), &controls);
    gtk_table_set_row_spacing(table, row, 8);
    row++;

    row = construct_options(&controls, table, row);

    /* Right column */
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("Preview"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    quark = gwy_app_get_surface_palette_key_for_id(id);
    if (gwy_container_gis_string(data, quark, &gradient)) {
        gwy_container_set_const_string_by_name(controls.mydata,
                                               "/0/base/palette", gradient);
    }
    dfield = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE, 1.0, 1.0, TRUE);
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    g_object_unref(dfield);

    controls.view = gwy_data_view_new(controls.mydata);
    gtk_box_pack_start(GTK_BOX(vbox), controls.view, FALSE, FALSE, 0);

    layer = gwy_layer_basic_new();
    g_object_set(layer,
                 "data-key", "/0/data",
                 "gradient-key", "/0/base/palette",
                 NULL);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(controls.view), "/0/data");
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(controls.view), layer);


    controls.gmodel = gwy_graph_model_new();
    controls.graph = gwy_graph_new(controls.gmodel);
    g_object_unref(controls.gmodel);
    gtk_widget_set_size_request(controls.graph, 300, 200);

    gtk_box_pack_start(GTK_BOX(vbox), controls.graph, TRUE, TRUE, 4);
    gwy_graph_enable_user_input(GWY_GRAPH(controls.graph), FALSE);





    controls.do_preview = gtk_button_new_with_mnemonic(_("_Update"));
    gtk_box_pack_start(GTK_BOX(vbox), controls.do_preview, FALSE, FALSE, 4);

    controls.error = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.error), 0.0, 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(controls.error), TRUE);
    gtk_widget_set_size_request(controls.error, PREVIEW_SIZE, -1);
    gtk_box_pack_start(GTK_BOX(vbox), controls.error, FALSE, FALSE, 0);

    g_signal_connect_swapped(controls.do_preview, "clicked",
                             G_CALLBACK(preview), &controls);
    g_signal_connect_swapped(controls.xres, "value-changed",
                             G_CALLBACK(xres_changed), &controls);
    g_signal_connect_swapped(controls.yres, "value-changed",
                             G_CALLBACK(yres_changed), &controls);
    g_signal_connect_swapped(controls.xmin, "activate",
                             G_CALLBACK(xmin_changed), &controls);
    g_signal_connect_swapped(controls.xmax, "activate",
                             G_CALLBACK(xmax_changed), &controls);
    g_signal_connect_swapped(controls.ymin, "activate",
                             G_CALLBACK(ymin_changed), &controls);
    g_signal_connect_swapped(controls.ymax, "activate",
                             G_CALLBACK(ymax_changed), &controls);

    g_signal_connect_swapped(controls.xdrift_a, "activate",
                     G_CALLBACK(xdrift_changed), &controls);
    g_signal_connect_swapped(controls.xdrift_b, "activate",
                     G_CALLBACK(xdrift_changed), &controls);
    g_signal_connect_swapped(controls.xdrift_c, "activate",
                     G_CALLBACK(xdrift_changed), &controls);

    g_signal_connect_swapped(controls.ydrift_a, "activate",
                     G_CALLBACK(ydrift_changed), &controls);
    g_signal_connect_swapped(controls.ydrift_b, "activate",
                     G_CALLBACK(ydrift_changed), &controls);
    g_signal_connect_swapped(controls.ydrift_c, "activate",
                     G_CALLBACK(ydrift_changed), &controls);

    g_signal_connect_swapped(controls.zdrift_a, "activate",
                     G_CALLBACK(zdrift_changed), &controls);
    g_signal_connect_swapped(controls.zdrift_b, "activate",
                     G_CALLBACK(zdrift_changed), &controls);
    g_signal_connect_swapped(controls.zdrift_c, "activate",
                     G_CALLBACK(zdrift_changed), &controls);

    //g_signal_connect_swapped(controls.zdrift_average, "value-changed",
    //                         G_CALLBACK(zdrift_changed), &controls);

    g_signal_connect_swapped(controls.neighbors, "value-changed",
                             G_CALLBACK(neighbors_changed), &controls);

    g_signal_connect_swapped(controls.threshold_time, "value-changed",
                             G_CALLBACK(threshold_changed), &controls);

    g_signal_connect_swapped(controls.threshold_length, "value-changed",
                             G_CALLBACK(threshold_changed), &controls);






    controls.in_update = FALSE;

    reset_ranges(&controls);
    zdrift_type_changed(controls.zdrift_type, &controls);
    graph_changed(controls.graph_type, &controls);
    upload_values(&controls, TRUE, TRUE, TRUE);

    gtk_widget_show_all(dialog);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            g_object_unref(controls.mydata);
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    g_object_unref(controls.mydata);

    return TRUE;
}

static gint
construct_resolutions(XYZDriftControls *controls,
                      GtkTable *table,
                      gint row)
{
    XYZDriftArgs *args = controls->args;
    GtkWidget *spin, *label;

    gtk_table_attach(table, gwy_label_new_header(_("Resolution")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Horizontal size:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->xres = gtk_adjustment_new(args->xres, 2, 16384, 1, 100, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->xres), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new("px");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Vertical size:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->yres = gtk_adjustment_new(args->yres, 2, 16384, 1, 100, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->yres), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new("px");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    return row;
}

static gint
construct_physical_dims(XYZDriftControls *controls,
                        GtkTable *table,
                        gint row)
{
    GwySurface *surface = controls->rdata->surface;
    GwySIValueFormat *vf;
    GtkWidget *label;

    vf = gwy_surface_get_value_format_xy(surface, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                         NULL);

    gtk_table_attach(table, gwy_label_new_header(_("Physical Dimensions")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_X-range:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->xmin = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->xmin), 7);
    gwy_widget_set_activate_on_unfocus(controls->xmin, TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->xmin);
    gtk_table_attach(table, controls->xmin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gtk_label_new("–"), 2, 3, row, row+1, 0, 0, 0, 0);
    controls->xmax = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->xmax), 7);
    gwy_widget_set_activate_on_unfocus(controls->xmax, TRUE);
    gtk_table_attach(table, controls->xmax, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(label), vf->units);
    gtk_table_attach(table, label, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Y-range:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->ymin = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->ymin), 7);
    gwy_widget_set_activate_on_unfocus(controls->ymin, TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->ymin);
    gtk_table_attach(table, controls->ymin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gtk_label_new("–"), 2, 3, row, row+1, 0, 0, 0, 0);
    controls->ymax = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->ymax), 7);
    gwy_widget_set_activate_on_unfocus(controls->ymax, TRUE);
    gtk_table_attach(table, controls->ymax, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(label), vf->units);
    gtk_table_attach(table, label, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->rdata->xymag = vf->magnitude;
    gwy_si_unit_value_format_free(vf);

    return row;
}

static gint
construct_options(XYZDriftControls *controls,
                  GtkTable *table,
                  gint row)
{
    XYZDriftArgs *args = controls->args;
    GtkWidget *label, *spin, *button;
    static const GwyEnum zdrifts[] = {
        { N_("2nd order polynom"),  GWY_XYZDRIFT_ZMETHOD_POLYNOM,  },
        { N_("Exponential"),  GWY_XYZDRIFT_ZMETHOD_EXPONENTIAL,  },
       // { N_("Moving average"),   GWY_XYZDRIFT_ZMETHOD_AVERAGE, },
    };
    static const GwyEnum drifts[] = {
        { N_("2nd order polynom"),  GWY_XYZDRIFT_METHOD_POLYNOM,  },
        { N_("Exponential"),   GWY_XYZDRIFT_METHOD_EXPONENTIAL, },
    };
   static const GwyEnum graphs[] = {
        { N_("X drift"),  GWY_XYZDRIFT_GRAPH_X,  },
        { N_("Y drift"),   GWY_XYZDRIFT_GRAPH_Y, },
        { N_("Z drift"),   GWY_XYZDRIFT_GRAPH_Z, },

    };





    gtk_table_attach(table, gwy_label_new_header(_("Initial values")),
                     0, 5, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    controls->xdrift_type
        = gwy_enum_combo_box_new(drifts, G_N_ELEMENTS(drifts),
                                 G_CALLBACK(gwy_enum_combo_box_update_int),
                                 &args->xdrift_type, args->xdrift_type, TRUE);
    gwy_table_attach_hscale(GTK_WIDGET(table), row, _("_X drift:"), NULL,
                            GTK_OBJECT(controls->xdrift_type),
                            GWY_HSCALE_WIDGET);
    row++;

    label = gtk_label_new("a = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);


    controls->xdrift_a = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->xdrift_a, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->xdrift_a), 12);
    gtk_table_attach(table, controls->xdrift_a, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new("b = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->xdrift_b = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->xdrift_b, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->xdrift_b), 12);
    gtk_table_attach(table, controls->xdrift_b, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(" c = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 4, 5, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->xdrift_c = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->xdrift_c, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->xdrift_c), 12);
    gtk_table_attach(table, controls->xdrift_c, 5, 6, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);


    controls->fit_xdrift = gtk_check_button_new_with_mnemonic(_("_fit"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->fit_xdrift),
                                 args->fit_xdrift);
    gtk_table_attach(GTK_TABLE(table), controls->fit_xdrift,
                     6, 7, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;


    controls->ydrift_type
        = gwy_enum_combo_box_new(drifts, G_N_ELEMENTS(drifts),
                                 G_CALLBACK(gwy_enum_combo_box_update_int),
                                 &args->ydrift_type, args->ydrift_type, TRUE);
    gwy_table_attach_hscale(GTK_WIDGET(table), row, _("_Y drift:"), NULL,
                            GTK_OBJECT(controls->ydrift_type),
                            GWY_HSCALE_WIDGET);
    row++;

    label = gtk_label_new("a = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->ydrift_a = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->ydrift_a, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->ydrift_a), 12);
    gtk_table_attach(table, controls->ydrift_a, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new("b = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->ydrift_b = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->ydrift_b, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->ydrift_b), 12);
    gtk_table_attach(table, controls->ydrift_b, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(" c = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 4, 5, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->ydrift_c = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->ydrift_c, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->ydrift_c), 12);
    gtk_table_attach(table, controls->ydrift_c, 5, 6, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);





    controls->fit_ydrift = gtk_check_button_new_with_mnemonic(_("_fit"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->fit_ydrift),
                                 args->fit_ydrift);
    gtk_table_attach(GTK_TABLE(table), controls->fit_ydrift,
                     6, 7, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;




    controls->zdrift_type
        = gwy_enum_combo_box_new(zdrifts, G_N_ELEMENTS(zdrifts),
                                 G_CALLBACK(zdrift_type_changed), controls,
                                 args->zdrift_type, TRUE);
    gwy_table_attach_hscale(GTK_WIDGET(table), row, _("Z fit _type:"), NULL,
                            GTK_OBJECT(controls->zdrift_type),
                            GWY_HSCALE_WIDGET);
    row++;


    label = gtk_label_new("a = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->zdrift_a = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->zdrift_a, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->zdrift_a), 12);
    gtk_table_attach(table, controls->zdrift_a, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new("b = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->zdrift_b = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->zdrift_b, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->zdrift_b), 12);
    gtk_table_attach(table, controls->zdrift_b, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(" c = ");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 4, 5, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->zdrift_c = gtk_entry_new();
    gwy_widget_set_activate_on_unfocus(controls->zdrift_c, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(controls->zdrift_c), 12);
    gtk_table_attach(table, controls->zdrift_c, 5, 6, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);




    controls->fit_zdrift = gtk_check_button_new_with_mnemonic(_("_fit"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->fit_zdrift),
                                 args->fit_zdrift);
    gtk_table_attach(GTK_TABLE(table), controls->fit_zdrift,
                     6, 7, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    /*
    label = gtk_label_new_with_mnemonic(_("_Moving average size:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->zdrift_average = gtk_adjustment_new(args->zdrift_average, 0, 10000, 1, 10, 0);
    controls->zdrift_average_spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->zdrift_average), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->zdrift_average_spin);
    gtk_table_attach(table, controls->zdrift_average_spin, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new("s");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    row++;
    */

    gtk_table_attach(table, gwy_label_new_header(_("Search parameters")),
                     0, 5, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;


    label = gtk_label_new_with_mnemonic(_("_Neighbors:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->neighbors = gtk_adjustment_new(args->neighbors, -1000, 1000, 1, 100, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->neighbors), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new("\%");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    row++;

    label = gtk_label_new_with_mnemonic(_("_Length threshold:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->threshold_length = gtk_adjustment_new(args->threshold_length, 0, 1000, 1, 100, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->threshold_length), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new("px");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    row++;

    label = gtk_label_new_with_mnemonic(_("_Time threshold:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->threshold_time = gtk_adjustment_new(args->threshold_time, 0, 1000, 1, 100, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->threshold_time), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new("s");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);


    row++;

    gtk_table_attach(table, gwy_label_new_header(_("Results")),
                     0, 5, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;


    controls->graph_type
        = gwy_enum_combo_box_new(graphs, G_N_ELEMENTS(graphs),
                                 G_CALLBACK(graph_changed), controls,
                                 args->graph_type, TRUE);
    gwy_table_attach_hscale(GTK_WIDGET(table), row, _("_Graph:"), NULL,
                            GTK_OBJECT(controls->graph_type),
                            GWY_HSCALE_WIDGET);
    row++;

    label = gtk_label_new(_("X drift:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->result_x = gtk_label_new(_("N.A."));
    gtk_misc_set_alignment(GTK_MISC(controls->result_x), 0.0, 0.5);
    gtk_table_attach(table, controls->result_x, 1, 5, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    button = gtk_button_new_with_label(_("to inits"));
    gtk_table_attach(table, button, 5, 6, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(button, "clicked",
                     G_CALLBACK(x_to_inits_cb), controls);


 
    row++;

    label = gtk_label_new(_("Y drift:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->result_y = gtk_label_new(_("N.A."));
    gtk_misc_set_alignment(GTK_MISC(controls->result_y), 0.0, 0.5);
    gtk_table_attach(table, controls->result_y, 1, 5, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    button = gtk_button_new_with_label(_("to inits"));
    gtk_table_attach(table, button, 5, 6, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(button, "clicked",
                     G_CALLBACK(y_to_inits_cb), controls);


    row++;

    label = gtk_label_new(_("Z drift:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->result_z = gtk_label_new(_("N.A."));
    gtk_misc_set_alignment(GTK_MISC(controls->result_z), 0.0, 0.5);
    gtk_table_attach(table, controls->result_z, 1, 5, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    button = gtk_button_new_with_label(_("to inits"));
    gtk_table_attach(table, button, 5, 6, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(button, "clicked",
                     G_CALLBACK(z_to_inits_cb), controls);

 

    return row;
}

static void
set_adjustment_in_update(XYZDriftControls *controls,
                         GtkAdjustment *adj,
                         gdouble value)
{
    controls->in_update = TRUE;
    gtk_adjustment_set_value(adj, value);
    controls->in_update = FALSE;
}

static void
set_physical_dimension(XYZDriftControls *controls,
                       GtkEntry *entry,
                       gdouble value,
                       gboolean in_update)
{
    gchar buf[24];

    if (in_update) {
        g_assert(!controls->in_update);
        controls->in_update = TRUE;
    }

    g_snprintf(buf, sizeof(buf), "%g", value/controls->rdata->xymag);
    gtk_entry_set_text(entry, buf);

    if (in_update)
        controls->in_update = FALSE;
}

static void
recalculate_xres(XYZDriftControls *controls)
{
    XYZDriftArgs *args = controls->args;
    gint xres;

    if (controls->in_update)
        return;

    xres = GWY_ROUND((args->xmax - args->xmin)/(args->ymax - args->ymin)
                     *args->yres);
    xres = CLAMP(xres, 2, 16384);
    set_adjustment_in_update(controls, GTK_ADJUSTMENT(controls->xres), xres);
}

static void
recalculate_yres(XYZDriftControls *controls)
{
    XYZDriftArgs *args = controls->args;
    gint yres;

    if (controls->in_update)
        return;

    yres = GWY_ROUND((args->ymax - args->ymin)/(args->xmax - args->xmin)
                     *args->xres);
    yres = CLAMP(yres, 2, 16384);
    set_adjustment_in_update(controls, GTK_ADJUSTMENT(controls->yres), yres);
}

static void
xres_changed(XYZDriftControls *controls,
             GtkAdjustment *adj)
{
    XYZDriftArgs *args = controls->args;

    args->xres = gwy_adjustment_get_int(adj);
    recalculate_yres(controls);
}

static void
yres_changed(XYZDriftControls *controls,
             GtkAdjustment *adj)
{
    XYZDriftArgs *args = controls->args;

    args->yres = gwy_adjustment_get_int(adj);
    recalculate_xres(controls);
}

static void
xmin_changed(XYZDriftControls *controls,
             GtkEntry *entry)
{
    XYZDriftArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    args->xmin = val * controls->rdata->xymag;
    if (!controls->in_update) {
        args->xmax = args->xmin + (args->ymax - args->ymin);
        set_physical_dimension(controls, GTK_ENTRY(controls->xmax),
                               args->xmax, TRUE);
    }
    recalculate_xres(controls);
}

static void
xmax_changed(XYZDriftControls *controls,
             GtkEntry *entry)
{
    XYZDriftArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    args->xmax = val * controls->rdata->xymag;
    if (!controls->in_update) {
        args->ymax = args->ymin + (args->xmax - args->xmin);
        set_physical_dimension(controls, GTK_ENTRY(controls->ymax),
                               args->ymax, TRUE);
    }
    recalculate_xres(controls);
}

static void
ymin_changed(XYZDriftControls *controls,
             GtkEntry *entry)
{
    XYZDriftArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    args->ymin = val * controls->rdata->xymag;
    if (!controls->in_update) {
        args->ymax = args->ymin + (args->xmax - args->xmin);
        set_physical_dimension(controls, GTK_ENTRY(controls->ymax),
                               args->ymax, TRUE);
    }
    recalculate_yres(controls);
}

static void
ymax_changed(XYZDriftControls *controls,
             GtkEntry *entry)
{
    XYZDriftArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    args->ymax = val * controls->rdata->xymag;
    if (!controls->in_update) {
        args->xmax = args->xmin + (args->ymax - args->ymin);
        set_physical_dimension(controls, GTK_ENTRY(controls->xmax),
                               args->xmax, TRUE);
    }
    recalculate_xres(controls);
}

static void
xdrift_changed(XYZDriftControls *controls,
             G_GNUC_UNUSED GtkAdjustment *adj)
{
    XYZDriftArgs *args = controls->args;

    args->xdrift_a = atof(gtk_entry_get_text(GTK_ENTRY(controls->xdrift_a)));
    args->xdrift_b = atof(gtk_entry_get_text(GTK_ENTRY(controls->xdrift_b)));
    args->xdrift_c = atof(gtk_entry_get_text(GTK_ENTRY(controls->xdrift_c)));
}

static void
ydrift_changed(XYZDriftControls *controls,
             G_GNUC_UNUSED GtkAdjustment *adj)
{
    XYZDriftArgs *args = controls->args;

    args->ydrift_a = atof(gtk_entry_get_text(GTK_ENTRY(controls->ydrift_a)));
    args->ydrift_b = atof(gtk_entry_get_text(GTK_ENTRY(controls->ydrift_b)));
    args->ydrift_c = atof(gtk_entry_get_text(GTK_ENTRY(controls->ydrift_c)));
}

static void
zdrift_changed(XYZDriftControls *controls,
             G_GNUC_UNUSED GtkAdjustment *adj)
{
    XYZDriftArgs *args = controls->args;

    args->zdrift_a = atof(gtk_entry_get_text(GTK_ENTRY(controls->zdrift_a)));
    args->zdrift_b = atof(gtk_entry_get_text(GTK_ENTRY(controls->zdrift_b)));
    args->zdrift_c = atof(gtk_entry_get_text(GTK_ENTRY(controls->zdrift_c)));
    //args->zdrift_average = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->zdrift_average));
}

static void          
zdrift_type_changed(GtkWidget *combo, XYZDriftControls *controls)
{

    controls->args->zdrift_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->zdrift_type));

    if (controls->in_update)
        return;


    if (controls->args->zdrift_type == GWY_XYZDRIFT_ZMETHOD_AVERAGE) {
        gtk_widget_set_sensitive(controls->zdrift_a, FALSE);
        gtk_widget_set_sensitive(controls->zdrift_b, FALSE);
        gtk_widget_set_sensitive(controls->zdrift_c, FALSE);
        //gtk_widget_set_sensitive(controls->zdrift_average_spin, TRUE);
    } else {
        gtk_widget_set_sensitive(controls->zdrift_a, TRUE);
        gtk_widget_set_sensitive(controls->zdrift_b, TRUE);
        gtk_widget_set_sensitive(controls->zdrift_c, TRUE);
        //gtk_widget_set_sensitive(controls->zdrift_average_spin, FALSE);
    }

}

static void          
graph_changed(GtkWidget *combo, XYZDriftControls *controls)
{
    GwyGraphCurveModel *gcmodel;
    XYZDriftArgs *args = controls->args;

    controls->args->graph_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->graph_type));

    if (controls->in_update)
        return;

    gwy_graph_model_remove_all_curves(controls->gmodel);
    gcmodel = gwy_graph_curve_model_new();

    if (args->graph_type == GWY_XYZDRIFT_GRAPH_X) gwy_graph_curve_model_set_data(gcmodel, controls->rdata->time, controls->rdata->xdrift, controls->rdata->npoints);
    else if (args->graph_type == GWY_XYZDRIFT_GRAPH_Y) gwy_graph_curve_model_set_data(gcmodel, controls->rdata->time, controls->rdata->ydrift, controls->rdata->npoints);
    else gwy_graph_curve_model_set_data(gcmodel, controls->rdata->time, controls->rdata->zdrift, controls->rdata->npoints);

    gwy_graph_model_add_curve(controls->gmodel, gcmodel);

}

static void
threshold_changed(XYZDriftControls *controls,
             G_GNUC_UNUSED GtkAdjustment *adj)
{
    XYZDriftArgs *args = controls->args;

    args->threshold_length = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->threshold_length));
    args->threshold_time = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->threshold_time));
}

static void
neighbors_changed(XYZDriftControls *controls,
             GtkAdjustment *adj)
{
    XYZDriftArgs *args = controls->args;

    args->neighbors = gtk_adjustment_get_value(adj);
}


static void
reset_ranges(XYZDriftControls *controls)
{
    XYZDriftArgs myargs = *controls->args;

    initialize_ranges(controls->rdata, &myargs);
    set_physical_dimension(controls, GTK_ENTRY(controls->ymin), myargs.ymin,
                           TRUE);
    set_physical_dimension(controls, GTK_ENTRY(controls->ymax), myargs.ymax,
                           TRUE);
    set_physical_dimension(controls, GTK_ENTRY(controls->xmin), myargs.xmin,
                           TRUE);
    set_physical_dimension(controls, GTK_ENTRY(controls->xmax), myargs.xmax,
                           TRUE);
}

static gint
find_closest_point(GwyXYZ *points, GwyXYZ *timepoints, gint npoints, gdouble tt, gdouble pt, gint index, gdouble *xdrift, gdouble *ydrift)
{
    int i, closest = -1;
    double mindist = G_MAXDOUBLE;
    double sdist;

    for (i=0; i<index; i++) {
        if ((timepoints[index].z - timepoints[i].z)>tt) {

            sdist = (((timepoints[index].x + xdrift[index]) - (timepoints[i].x + xdrift[i]))*((timepoints[index].x + xdrift[index]) - (timepoints[i].x + xdrift[i])) +
                ((timepoints[index].y + ydrift[index]) - (timepoints[i].y + ydrift[i]))*((timepoints[index].y + ydrift[index]) - (timepoints[i].y + ydrift[i])));

            if (sdist<(pt*pt)) {
                if (sdist<mindist) {
                    mindist = sdist;
                    closest = i;
                }
            }
        } 
    }

    return closest;
}
static gdouble
get_error(GwyXYZ *points, gint npoints, gint *nbfrom, gint *nbto, gint nnbs, gdouble *xdrift, gdouble *ydrift, gdouble *zdrift)
{
    gint i;
    gdouble sum = 0;

    /*for each neighbor, sum the squared difference after drift correction*/
    for (i=0; i<nnbs; i++) {
        sum += ((points[nbfrom[i]].z + zdrift[nbfrom[i]]) - (points[nbto[i]].z + zdrift[nbto[i]]))
              *((points[nbfrom[i]].z + zdrift[nbfrom[i]]) - (points[nbto[i]].z + zdrift[nbto[i]]));

    }
    return sum/nnbs;

}

// add thresholds to gui, add some suggestion as well.  print out the results, do not follow if they are not enough or too many.

//we search for places that are closed by one pixel and that are far from each other by enough long time.
//we might reduce their number by some skipping factor


#define NBIN 10.0

static void
get_bin(gdouble x, gdouble y, gint *bi, gint *bj, gdouble xreal, gdouble yreal, gdouble xoffset, gdouble yoffset)
{
    gint i, j;
    i = NBIN*(x-xoffset)/xreal;
    j = NBIN*(y-yoffset)/yreal;

    *bi = CLAMP(i, 0, NBIN-1);
    *bj = CLAMP(j, 0, NBIN-1);

}


static void
get_bining(GwyXYZ *points, gint npoints, gdouble *xdrift, gdouble *ydrift, gint ***bin, gint **nbin, gdouble xreal, gdouble yreal, gdouble xoffset, gdouble yoffset)
{
    gint i, j, k;
    gint bi, bj;

    //clear nbins
    for (i=0; i<NBIN; i++) {
        for (j=0; j<NBIN; j++) {
            nbin[i][j] = 0;
        }
    }


    //eval how much space to allocate
    for (k=0; k<npoints; k++) {
        get_bin(points[k].x, points[k].y, &bi, &bj, xreal, yreal, xoffset, yoffset);
        nbin[bi][bj] = nbin[bi][bj]+1;
    }

    //allocate the bins
    for (i=0; i<NBIN; i++) {
        for (j=0; j<NBIN; j++) {
            bin[i][j] = (gint *)g_malloc(nbin[i][j]*sizeof(gint));
        }
    }

    //clear nbins
    for (i=0; i<NBIN; i++) {
        for (j=0; j<NBIN; j++) {
            nbin[i][j] = 0;
        }
    }

    //fill bins
    for (k=0; k<npoints; k++) {
        get_bin(points[k].x, points[k].y, &bi, &bj, xreal, yreal, xoffset, yoffset);
        bin[bi][bj][nbin[bi][bj]] = k;
        nbin[bi][bj]++;
    }

}

static gint
find_closest_point_bining(GwyXYZ *points, gdouble *time, gint npoints, gdouble tt, gdouble pt, gint index, gdouble *xdrift, gdouble *ydrift, gint ***bin, gint **nbin, 
                          gdouble xreal, gdouble yreal, gdouble xoffset, gdouble yoffset)
{

    gint bi, bj, i, j, k, bindex;
    int closest = -1;
    double mindist = G_MAXDOUBLE;
    double sdist;


    //get actual bin
    get_bin(points[index].x, points[index].y, &bi, &bj, xreal, yreal, xoffset, yoffset);

    //search in surrounding bins
    for (i=MAX(0, bi-1); i<MIN(bi+1, NBIN); i++)
    {
        for (j=MAX(0, bj-1); j<MIN(bj+1, NBIN); j++)
        {
            for (k=0; k<nbin[i][j]; k++) //go through bin
            {
                  bindex = bin[i][j][k];

                  if ((time[index] - time[bindex])>tt) {

                     sdist = (((points[index].x + xdrift[index]) - (points[bindex].x + xdrift[bindex]))*((points[index].x + xdrift[index]) - (points[bindex].x + xdrift[bindex])) +
                         ((points[index].y + ydrift[index]) - (points[bindex].y + ydrift[bindex]))*((points[index].y + ydrift[index]) - (points[bindex].y + ydrift[bindex])));

                     if (sdist<(pt*pt)) {
                        if (sdist<mindist) {
                           mindist = sdist;
                            closest = bindex;
                        }
                     }
                  }  

            }
        }
    }
    

    return closest;

}


static gint 
find_neighbors(gint *nbfrom, gint *nbto, GwyXYZ *points, gdouble *time, gint npoints, gdouble timethreshold, gdouble posthreshold, gdouble *xdrift, gdouble *ydrift,
               gdouble xreal, gdouble yreal, gdouble xoffset, gdouble yoffset)
{
    gint i, j, k, nnbs = 0, closest;
    gint ***bin, **nbin;
    FILE *fw = fopen("nbs.txt", "w");




    bin = (gint ***)g_malloc(NBIN*sizeof(gint **));
    for (i=0; i<NBIN; i++) bin[i] = (gint **) g_malloc(NBIN*sizeof(gint *));

    nbin = (gint **)g_malloc(NBIN*sizeof(gint *));
    for (i=0; i<NBIN; i++) nbin[i] = (gint *) g_malloc(NBIN*sizeof(gint));

    printf("bining\n");
    get_bining(points, npoints, xdrift, ydrift, bin, nbin, xreal, yreal, xoffset, yoffset);
    printf("done\n");

    printf("neighbors\n");
    nnbs = 0;
    fprintf(fw, "# index closest ix iy cx cy iz cz it ct tdiff\n");

    for (i=0; i<npoints; i++) {
        closest = find_closest_point_bining(points, time, npoints, timethreshold, posthreshold, i, xdrift, ydrift, bin, nbin, xreal, yreal, xoffset, yoffset);

        if (closest>=0) {
            fprintf(fw, "closest %d %d    %g %g    %g %g   %g %g   %g %g    %g\n", i, closest, points[i].x, points[i].y, points[closest].x, points[closest].y, points[i].z, points[closest].z, time[i], time[closest], time[i]-time[closest]);
            nbfrom[nnbs] = closest;
            nbto[nnbs] = i;
            nnbs++;
        }
     }

    printf("done\n");


    fclose(fw);


/*
    nnbs = 0;
    fprintf(fw, "# index closest ix iy cx cy iz cz it ct tdiff\n");
    for (i=0; i<npoints; i++) {
        closest = find_closest_point(points, timepoints, npoints, timethreshold, posthreshold, i, xdrift, ydrift);
        if (closest>=0) {
            fprintf(fw, "closest %d %d    %g %g    %g %g   %g %g   %g %g    %g\n", i, closest, points[i].x, points[i].y, points[closest].x, points[closest].y, points[i].z, points[closest].z, timepoints[i].z, timepoints[closest].z, timepoints[i].z-timepoints[closest].z);
            nbfrom[nnbs] = closest;
            nbto[nnbs] = i;
            nnbs++;
        }
    }
    fclose(fw);
*/

    return nnbs;
}


/* If you pass non-NULL fixed[] then params[] should already contain initial
 * estimates for the fixed params. */
static gboolean
fit_func_to_curve(GwyGraphCurveModel *gcmodel, const gchar *name,
                  gdouble *params, gdouble *errors, const gboolean *fixed)
{
    GwyNLFitPreset *preset = gwy_inventory_get_item(gwy_nlfit_presets(), name);
    const gdouble *xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    const gdouble *ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    gint ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    GwyNLFitter *fitter;
    gdouble *origparams;
    gboolean ok = FALSE;
    guint i, n;

    g_return_val_if_fail(preset, FALSE);

    n = gwy_nlfit_preset_get_nparams(preset);
    origparams = g_memdup(params, n*sizeof(gdouble));
    gwy_nlfit_preset_guess(preset, ndata, xdata, ydata, params, &ok);
    printf("guess: %g %g %g  ok %d\n", params[0], params[1], params[2], ok);
    if (!ok) {
        g_free(origparams);
        return FALSE;
    }

    if (fixed) {
        for (i = 0; i < n; i++) {
            if (fixed[i])
                params[i] = origparams[i];
        }
    }

    fitter = gwy_nlfit_preset_fit(preset, NULL, n, xdata, ydata,
                                  params, errors, fixed);
    ok = gwy_math_nlfit_succeeded(fitter);
    gwy_math_nlfit_free(fitter);
    g_free(origparams);

    return ok;
}

static double
get_xydrift_val(gint type, gdouble a, gdouble b, gdouble c, gdouble time)
{
    gdouble rtime = time;

    if (type==0) //polynom
       return a + b*rtime + c*rtime*rtime;
    else if (type==1) //exponential
       return a + b*exp(rtime/c);
    else return 0;
}

static double
get_zdrift_val(gint type, gdouble a, gdouble b, gdouble c, gdouble time)
{
    gdouble rtime = time;

    if (type==0) //polynom
       return a + b*rtime + c*rtime*rtime;
    else if (type==1) //exponential
       return a + b*exp(rtime/c);
    else return 0; //ignore moving average drift type
}



//evaluate zdrift from actual corrected positions and previously found neighbors
static void
get_zdrift(XYZDriftControls *controls, GwyXYZ *points, gint npoints, gdouble *time, gdouble *zdrift, gint *nbfrom, gint *nbto, gint nnbs) 
{
    gint i;
    gdouble *dtime, *drift;
    gdouble params[3];
    gdouble errors[3];
    gboolean fixed[3];
    gchar buffer[100];
    GwyGraphCurveModel *gcmodel;
    gboolean ok;

    FILE *fw = fopen("driftdata.txt", "w");


    /*re-read for sure*/
    zdrift_changed(controls, NULL);

    dtime = (gdouble *)malloc(nnbs*sizeof(gdouble));
    drift = (gdouble *)malloc(nnbs*sizeof(gdouble));

    for (i=0; i<nnbs; i++) {
        dtime[i] = (time[nbfrom[i]]+time[nbto[i]])/2;
        drift[i] = (points[nbto[i]].z - points[nbfrom[i]].z)/2;
    }    

    params[0] = controls->args->zdrift_a;
    params[1] = controls->args->zdrift_b;
    params[2] = controls->args->zdrift_c;
    fixed[0] = 0;
    fixed[1] = 0; 
    fixed[2] = 0;

    gcmodel = gwy_graph_curve_model_new();
    gwy_graph_curve_model_set_data(gcmodel, dtime, drift, nnbs);

    if (controls->args->zdrift_type==GWY_XYZDRIFT_ZMETHOD_POLYNOM)
        ok = fit_func_to_curve(gcmodel, "Polynomial (order 2)", params, errors, fixed);
    else if (controls->args->zdrift_type==GWY_XYZDRIFT_ZMETHOD_EXPONENTIAL)
        ok = fit_func_to_curve(gcmodel, "Exponential", params, errors, fixed);


    printf("Fitting completed with %d: %g %g %g\n", ok, params[0], params[1], params[2]);  
    g_snprintf(buffer, sizeof(buffer), "a = %g +- %g,  b = %g +- %g,  c = %g +- %g", params[0], errors[0], params[1], errors[1], params[2], errors[2]);
    gtk_label_set_text(controls->result_z, buffer); 

    controls->args->zdrift_a = params[0];
    controls->args->zdrift_b = params[1];
    controls->args->zdrift_c = params[2];

    for (i=0; i<nnbs; i++) {
        fprintf(fw, "%g %g %g\n", dtime[i], drift[i], params[0] + params[1]*dtime[i] + params[2]*dtime[i]*dtime[i]);
    }
    fclose(fw);

    g_free(dtime);
    g_free(drift);
    //g_free(gcmodel);
}

static void
set_drift(XYZDriftControls *controls, GwyXYZ *points, GwyXYZ *timepoints, gint npoints, gdouble *time, gdouble *xdrift, gdouble *ydrift, gdouble *zdrift,
          gdouble ax, gdouble bx, gdouble cx, gdouble ay, gdouble by, gdouble cy, gdouble az, gdouble bz, gdouble cz)
{
    gint i;
    XYZDriftArgs *args = controls->args;

    for (i=0; i<npoints; i++) {
       xdrift[i] = get_xydrift_val(args->xdrift_type, ax, bx, cx, time[i]);
       ydrift[i] = get_xydrift_val(args->ydrift_type, ay, by, cy, time[i]);
       zdrift[i] = get_zdrift_val(args->zdrift_type, az, bz, cz, time[i]);
    }
}

static void
estimate_drift(XYZDriftControls *controls, GwyXYZ *points, GwyXYZ *corpoints, GwyXYZ *timepoints, gint npoints, gdouble *time, gdouble *xdrift, gdouble *ydrift, gdouble *zdrift)
{
    gint i, closest;
    gint *nbfrom, *nbto, nnbs;
    gdouble timethreshold = 1e3, posthreshold = 1e-6;
    gdouble ax, ay, az, bx, by, bz, cx, cy, cz;
    gdouble minxdrift, minxdrifts, minydrift, minydrifts;
    gdouble err, minerr, minzdrift, minzdrifts;


    ax = controls->args->xdrift_a;
    bx = controls->args->xdrift_b;
    cx = controls->args->xdrift_c;

    ay = controls->args->ydrift_a;
    by = controls->args->ydrift_b;
    cy = controls->args->ydrift_c;

    az = controls->args->zdrift_a;
    bz = controls->args->zdrift_b;
    cz = controls->args->zdrift_c;


    printf("estimate drift called\n");


    nbfrom = (gint *)malloc(npoints*sizeof(gint));
    nbto = (gint *)malloc(npoints*sizeof(gint));

    minerr = G_MAXDOUBLE;    

    /*iterate through x and y*/ 
    

    /*correct data for actual drift in xy*/
    set_drift(controls, points, timepoints, npoints, time, xdrift, ydrift, zdrift, 
              ax, bx, cx, ay, by, cy, az, bz, cz);
    correct_drift(points, npoints, xdrift, ydrift, zdrift,
                  corpoints, FALSE);

    //err = get_error(corpoints, npoints, nbfrom, nbto, nnbs, xdrift, ydrift, zdrift);


    nnbs = find_neighbors(nbfrom, nbto, corpoints, time, npoints, timethreshold, posthreshold, xdrift, ydrift,
                          controls->args->xmax - controls->args->xmin, controls->args->ymax - controls->args->ymin, controls->args->xmin, controls->args->ymin);

/*
    for (bz = (0.5*controls->args->zdrift_b); bz <= (2*controls->args->zdrift_b); bz += (0.1*controls->args->zdrift_b))
    {
        //correct data for actual drift in xyz
        set_drift(controls, points, timepoints, npoints, time, xdrift, ydrift, zdrift, 
                  ax, bx, cx, ay, by, cy, az, bz, cz);

        err = get_error(corpoints, npoints, nbfrom, nbto, nnbs, xdrift, ydrift, zdrift);

        if (err<minerr) {
            minerr = err;
            printf("error %g     params %g %g %g    %g %g %g    %g %g %g\n", err, ax, bx, cx, ay, by, cy, az, bz, cz);
        }
    }
*/

//    get z drift directly
    get_zdrift(controls, points, npoints, time, zdrift, nbfrom, nbto, nnbs);





 
//    FILE *fw = fopen("data.txt", "w");
 //   for (i=0; i<npoints; i++)
 //       fprintf(fw, "%g %g\n", timepoints[i].z, points[i].z);
 //   fclose(fw);


   return;

    //xy loop, drift must be taken into accound while searching for neighbors
    //ifor (xmdrift = (xsdrift-0); xmdrift<(xsdrift+1); xmdrift+=1)
//    {
     //for (xmmdrift = 300000000; xmmdrift<400000000; xmmdrift+=100000000)
//     {
      //for (ymdrift = (ysdrift-2); ymdrift<(ysdrift+2); ymdrift+=1)
//      {
       //for (ymmdrift = -1000000000; ymmdrift<1000000000; ymmdrift+=500000000)
//       {
//        ymdrift = xmdrift;
//        ymmdrift = xmmdrift;

//        for (i=0; i<npoints; i++) {
//            xdrift[i] = (time[i]-time[0])*xsdrift/1e15 + (time[i]-time[0])*(time[i]-time[0])*xmmdrift/1e15/1e15;
//            ydrift[i] = (time[i]-time[0])*ysdrift/1e15 + (time[i]-time[0])*(time[i]-time[0])*ymmdrift/1e15/1e15;
//        }


//TODO use corpoints to fill the data after drift in x y, to speedup the process. Corpoints finally should be ready for final calculations


//        nnbs = find_neighbors(nbfrom, nbto, points, timepoints, npoints, timethreshold, posthreshold, xdrift, ydrift);
 
        //z loop, only height changes here
//        minerr = G_MAXDOUBLE;
/*        for (zmdrift = (zsdrift-0.5); zmdrift<=(zsdrift+0.5); zmdrift+=0.2)
        {
            for (zmmdrift = -3500; zmmdrift <= (-2500); zmmdrift+=500)
            {
                for (i=0; i<npoints; i++) {
                    zdrift[i] = (time[i]-time[0])*zmdrift/1e9 + (time[i]-time[0])*(time[i]-time[0])*zmmdrift/1e9/1e9;
                }
*/
                /*evaluate drift*/
 /*               err = get_error(points, npoints, nbfrom, nbto, nnbs, xdrift, ydrift, zdrift);
                printf("error for drift ( %g %g ) ( %g %g ) ( %g %g ): %g  evaluated from %d neighbors\n", xmdrift, xmmdrift, ymdrift, ymmdrift, zmdrift, zmmdrift, err, nnbs);
 
                if (err<minerr) {
                    minzdrift = zmdrift;
                    minzdrifts = zmmdrift;
                    minxdrift = xmdrift;
                    minxdrifts = xmmdrift;
                    minydrift = ymdrift;
                    minydrifts = ymmdrift;
                    minerr = err;
                }
            }
            printf("\n");
         }
       }
      }
     }
    }
   */ 

    /*interpolate drift*/
/*    for (i=0; i<npoints; i++) {
       xdrift[i] = (time[i]-time[0])*minxdrift/1e15 + (time[i]-time[0])*(time[i]-time[0])*minxdrifts/1e15/1e15;
       ydrift[i] = (time[i]-time[0])*minydrift/1e15 + (time[i]-time[0])*(time[i]-time[0])*minydrifts/1e15/1e15;
       zdrift[i] = (time[i]-time[0])*minzdrift/1e9 + (time[i]-time[0])*(time[i]-time[0])*minzdrifts/1e9/1e9;
    }

    printf("Resulting drifts: x %g %g   y %g %g  z %g %g\n", minxdrift, minxdrifts, minydrift, minydrifts, minzdrift, minzdrifts);

    free(nbfrom);
    free(nbto);

*/

}


static void
init_drift(XYZDriftControls *controls, GwyXYZ *points, GwyXYZ *timepoints, gint npoints, gdouble *time, gdouble *xdrift, gdouble *ydrift, gdouble *zdrift)
{
    gint i;
    XYZDriftArgs *args = controls->args;
    gdouble div = 1e9;

    for (i=0; i<npoints; i++) {
       time[i] = (timepoints[i].z - timepoints[0].z)/1e3; //FIXME remove this

       xdrift[i] = get_xydrift_val(args->xdrift_type, args->xdrift_a, args->xdrift_b, args->xdrift_c, time[i]);
       ydrift[i] = get_xydrift_val(args->ydrift_type, args->ydrift_a, args->ydrift_b, args->ydrift_c, time[i]);
       zdrift[i] = get_zdrift_val(args->zdrift_type, args->zdrift_a, args->zdrift_b, args->zdrift_c, time[i]);
    }
}


static void
correct_drift(GwyXYZ *points, gint npoints, gdouble *xdrift, gdouble *ydrift, gdouble *zdrift,
               GwyXYZ *corpoints, gboolean correctz)
{
    gint i;

    for (i=0; i<npoints; i++) {
       corpoints[i].x = points[i].x + xdrift[i];
       corpoints[i].y = points[i].y + ydrift[i];
       if (correctz) corpoints[i].z = points[i].z - zdrift[i];
    }
}

 

static void
preview(XYZDriftControls *controls)
{
    XYZDriftArgs *args = controls->args;
    XYZDriftData *rdata = controls->rdata;
    GwyGraphCurveModel *gcmodel;
    GwyDataField *dfield;
    GtkWidget *entry;
    gint xres, yres;
    gchar *error = NULL;

    entry = gtk_window_get_focus(GTK_WINDOW(controls->dialog));
    if (entry && GTK_IS_ENTRY(entry))
        gtk_widget_activate(entry);

    xres = PREVIEW_SIZE*args->xres/MAX(args->xres, args->yres);
    yres = PREVIEW_SIZE*args->yres/MAX(args->xres, args->yres);

    args->fit_xdrift = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->fit_xdrift));
    args->fit_ydrift = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->fit_ydrift));
    args->fit_zdrift = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->fit_zdrift));


    printf("todo: x (%g %g %g %d), y (%g %g %g %d), z %d (%g %g %g %d) (%d)   nbs %g   thresholds %g %g\n", 
                args->xdrift_a, args->xdrift_b, args->xdrift_c, args->fit_xdrift,
                args->ydrift_a, args->ydrift_b, args->ydrift_c, args->fit_ydrift,
                args->zdrift_type, args->zdrift_a, args->zdrift_b, args->zdrift_c, args->fit_zdrift, args->zdrift_average,
                args->neighbors, args->threshold_length, args->threshold_time);


    /*fill xdrift, ydrift and zdrift arrays from user/default values*/
    init_drift(controls, rdata->points, rdata->timepoints, rdata->npoints, rdata->time, rdata->xdrift, rdata->ydrift, rdata->zdrift);

    /*estimate the drift using some fitting routine, returning filled xdrift, ydrift and zdrift arrays*/
    if (args->fit_xdrift || args->fit_ydrift || args->fit_zdrift) 
        estimate_drift(controls, rdata->points, rdata->corpoints, rdata->timepoints, rdata->npoints, rdata->time, rdata->xdrift, rdata->ydrift, rdata->zdrift);

    /*correct data for drift, creating corpoints from points*/
    correct_drift(rdata->points, rdata->npoints, rdata->xdrift, rdata->ydrift, rdata->zdrift,
                  rdata->corpoints, TRUE);


    /*render preview*/
    dfield = xyzdrift_do(controls->rdata, args,
                         GTK_WINDOW(controls->dialog), &error);

    /*resample preview*/
    gwy_data_field_resample(dfield, xres, yres, GWY_INTERPOLATION_ROUND);

    /*fill drift graph*/
    gwy_graph_model_remove_all_curves(controls->gmodel);
    gcmodel = gwy_graph_curve_model_new();
    if (args->graph_type == GWY_XYZDRIFT_GRAPH_X) gwy_graph_curve_model_set_data(gcmodel, rdata->time, rdata->xdrift, rdata->npoints);
    else if (args->graph_type == GWY_XYZDRIFT_GRAPH_Y) gwy_graph_curve_model_set_data(gcmodel, rdata->time, rdata->ydrift, rdata->npoints);
    else gwy_graph_curve_model_set_data(gcmodel, rdata->time, rdata->zdrift, rdata->npoints);
    gwy_graph_model_add_curve(controls->gmodel, gcmodel);

    if (!dfield) {
        gtk_label_set_text(GTK_LABEL(controls->error), error);
        g_free(error);
        dfield = gwy_data_field_new(args->xres, args->yres,
                                    args->xres, args->yres, TRUE);
    }

    gwy_container_set_object_by_name(controls->mydata, "/0/data", dfield);
    g_object_unref(dfield);

 

}


static GwyDataField*
xyzdrift_do(XYZDriftData *rdata,
          const XYZDriftArgs *args,
          GtkWindow *window,
          gchar **error)
{
    GwyDataField *dfield;
    GwySurface *surface = rdata->surface;

    gwy_debug("%g %g :: %g %g", args->xmin, args->xmax, args->ymin, args->ymax);
    if (!(args->xmax > args->xmin) || !(args->ymax > args->ymin)) {
        *error = g_strdup(_("Physical dimensions are invalid."));
        return NULL;
    }
    dfield = gwy_data_field_new(args->xres, args->yres,
                                args->xmax - args->xmin,
                                args->ymax - args->ymin,
                                FALSE);
    gwy_data_field_set_xoffset(dfield, args->xmin);
    gwy_data_field_set_yoffset(dfield, args->ymin);
    gwy_serializable_clone(G_OBJECT(gwy_surface_get_si_unit_xy(surface)),
                           G_OBJECT(gwy_data_field_get_si_unit_xy(dfield)));
    gwy_serializable_clone(G_OBJECT(gwy_surface_get_si_unit_z(surface)),
                           G_OBJECT(gwy_data_field_get_si_unit_z(dfield)));


    gwy_data_field_average_xyz(dfield, NULL,
                          rdata->corpoints, rdata->npoints);

    printf("interpolated through %d points to %d x %d\n", rdata->npoints, args->xres, args->yres);

    return dfield;
}


static void
xyzdrift_free(XYZDriftData *rdata)
{
}

static gdouble
round_with_base(gdouble x, gdouble base)
{
    gint s;

    s = (x < 0) ? -1 : 1;
    x = fabs(x)/base;
    if (x <= 1.0)
        return GWY_ROUND(10.0*x)/10.0*s*base;
    else if (x <= 2.0)
        return GWY_ROUND(5.0*x)/5.0*s*base;
    else if (x <= 5.0)
        return GWY_ROUND(2.0*x)/2.0*s*base;
    else
        return GWY_ROUND(x)*s*base;
}

static void
round_to_nice(gdouble *minval, gdouble *maxval)
{
    gdouble range = *maxval - *minval;
    gdouble base = pow10(floor(log10(range) - 2.0));

    *minval = round_with_base(*minval, base);
    *maxval = round_with_base(*maxval, base);
}

static void
initialize_ranges(const XYZDriftData *rdata,
                  XYZDriftArgs *args)
{
    GwySurface *surface = rdata->surface;

    gwy_surface_get_xrange(surface, &args->xmin, &args->xmax);
    gwy_surface_get_yrange(surface, &args->ymin, &args->ymax);

    round_to_nice(&args->xmin, &args->xmax);
    round_to_nice(&args->ymin, &args->ymax);

    gwy_debug("%g %g :: %g %g", args->xmin, args->xmax, args->ymin, args->ymax);
}



static const gchar xres_key[]          = "/module/xyz_drift/xres";
static const gchar yres_key[]          = "/module/xyz_drift/yres";

static void
xyzdrift_sanitize_args(XYZDriftArgs *args)
{
    args->xres = CLAMP(args->xres, 2, 16384);
    args->yres = CLAMP(args->yres, 2, 16384);
}

static void
xyzdrift_load_args(GwyContainer *container,
                 XYZDriftArgs *args)
{
    *args = xyzdrift_defaults;

    gwy_container_gis_int32_by_name(container, xres_key, &args->xres);
    gwy_container_gis_int32_by_name(container, yres_key, &args->yres);

    xyzdrift_sanitize_args(args);
}

static void
xyzdrift_save_args(GwyContainer *container,
                 XYZDriftArgs *args)
{
    gwy_container_set_int32_by_name(container, xres_key, args->xres);
    gwy_container_set_int32_by_name(container, yres_key, args->yres);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
