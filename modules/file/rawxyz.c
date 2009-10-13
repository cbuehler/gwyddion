/*
 *  $Id$
 *  Copyright (C) 2009 David Necas (Yeti).
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 */

/**
 * [FILE-MAGIC-USERGUIDE]
 * Raw XYZ data
 * .xyz .dat
 * Read[1]
 * [1] XYZ data are interpolated to a regular grid upon import.
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libprocess/delaunay.h>
#include <libdraw/gwypixfield.h>
#include <libdraw/gwygradient.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include <app/settings.h>

#include "err.h"

#define EPSREL 1e-8

/* Use smaller cell sides than the triangulation algorithm as we only need them
 * for identical point detection and border extension. */
#define CELL_SIDE 1.6

enum {
    PREVIEW_SIZE = 240,
    UNDEF = G_MAXUINT
};

typedef struct {
    /* XXX: Not all values of interpolation and exterior are possible. */
    GwyInterpolationType interpolation;
    GwyExteriorType exterior;
    gchar *xy_units;
    gchar *z_units;
    gint xres;
    gint yres;
    gboolean xydimeq;
    gboolean xymeasureeq;
    /* Interface only */
    gdouble xmin;
    gdouble xmax;
    gdouble ymin;
    gdouble ymax;
} RawXYZArgs;

typedef struct {
    GArray *points;
    guint norigpoints;
    gdouble xmin;
    gdouble xmax;
    gdouble ymin;
    gdouble ymax;
    gdouble zmin;
    gdouble zmax;
} RawXYZFile;

typedef struct {
    RawXYZArgs *args;
    RawXYZFile *rfile;
    GtkWidget *dialog;
    GwyGradient *gradient;
    GtkObject *xmin;
    GtkObject *xmax;
    GtkObject *ymin;
    GtkObject *ymax;
    GtkWidget *xydimeq;
    GtkWidget *xymeasureeq;
    GtkObject *xres;
    GtkObject *yres;
    GtkWidget *xy_units;
    GtkWidget *z_units;
    GtkWidget *interpolation;
    GtkWidget *exterior;
    GtkWidget *preview;
    GtkWidget *do_preview;
    GtkWidget *error;
} RawXYZControls;

typedef struct {
    guint *id;
    guint pos;
    guint len;
    guint size;
} WorkQueue;

static gboolean      module_register  (void);
static GwyContainer* rawxyz_load      (const gchar *filename,
                                       GwyRunType mode,
                                       GError **error);
static gboolean      rawxyz_dialog    (RawXYZArgs *arg,
                                       RawXYZFile *rfile);
static void change_units(RawXYZControls *controls,
             GtkWidget *button);
static void          preview          (RawXYZControls *controls);
static void          rawxyz_free      (RawXYZFile *rfile);
static GArray*       read_points      (gchar *p);
static void          initialize_ranges(const RawXYZFile *rfile,
                                       RawXYZArgs *args);
static void          analyse_points   (RawXYZFile *rfile,
                                       double epsrel);
static void          rawxyz_load_args (GwyContainer *container,
                                       RawXYZArgs *args);
static void          rawxyz_save_args (GwyContainer *container,
                                       RawXYZArgs *args);

static const RawXYZArgs rawxyz_defaults = {
    GWY_INTERPOLATION_LINEAR, GWY_EXTERIOR_MIRROR_EXTEND,
    NULL, NULL,
    500, 500,
    TRUE, TRUE,
    /* Interface only */
    0.0, 0.0, 0.0, 0.0
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports raw XYZ files."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2009",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_file_func_register("rawxyz",
                           N_("Raw XYZ data"),
                           NULL,
                           (GwyFileLoadFunc)&rawxyz_load,
                           NULL,
                           NULL);

    return TRUE;
}

static GwyContainer*
rawxyz_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *settings, *container = NULL;
    RawXYZArgs args;
    RawXYZFile rfile;
    gchar *buffer = NULL;
    gsize size;
    GError *err = NULL;
    gboolean ok;

    /* Someday we can load pixmaps with default settings */
    if (mode != GWY_RUN_INTERACTIVE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_INTERACTIVE,
                    _("Raw XYZ data import must be run as interactive."));
        return NULL;
    }

    gwy_clear(&rfile, 1);

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    rfile.points = read_points(buffer);
    g_free(buffer);
    if (!rfile.points->len) {
        err_NO_DATA(error);
        goto fail;
    }

    settings = gwy_app_settings_get();
    rawxyz_load_args(settings, &args);
    analyse_points(&rfile, EPSREL);
    initialize_ranges(&rfile, &args);
    ok = rawxyz_dialog(&args, &rfile);
    rawxyz_save_args(settings, &args);
    if (!ok) {
        err_CANCELLED(error);
        goto fail;
    }

fail:
    rawxyz_free(&rfile);

    return container;
}

static gboolean
rawxyz_dialog(RawXYZArgs *args,
              RawXYZFile *rfile)
{
    GtkWidget *dialog, *vbox, *align, *label, *spin, *hbox, *button;
    GtkTable *table;
    RawXYZControls controls;
    GdkPixbuf *pixbuf;
    gint row, response;

    controls.args = args;
    controls.rfile = rfile;
    controls.gradient = gwy_gradients_get_gradient(NULL);
    gwy_resource_use(GWY_RESOURCE(controls.gradient));

    dialog = gtk_dialog_new_with_buttons(_("Import XYZ Data"), NULL, 0,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_has_separator(GTK_DIALOG(dialog), FALSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 20);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 0);

    /* Left column */
    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(12, 4, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_container_add(GTK_CONTAINER(align), GTK_WIDGET(table));
    row = 0;

    /* Resolution */
    gtk_table_attach(table, gwy_label_new_header(_("Resolution")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Horizontal size:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls.xres = gtk_adjustment_new(args->xres, 2, 16384, 1, 100, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.xres), 0, 0);
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
    controls.yres = gtk_adjustment_new(args->yres, 2, 16384, 1, 100, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.yres), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new("px");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    button = gtk_check_button_new_with_mnemonic(_("Identical _measures"));
    controls.xymeasureeq = button;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), args->xymeasureeq);
    gtk_table_attach(table, button, 0, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_set_row_spacing(table, row, 8);
    row++;

    /* Resolution */
    gtk_table_attach(table, gwy_label_new_header(_("Physical Dimensions")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_X-range:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls.xmin = gtk_adjustment_new(args->xmin, -1000.0, 1000.0, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.xmin), 0, 3);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gtk_label_new("–"), 2, 3, row, row+1, 0, 0, 0, 0);
    controls.xmax = gtk_adjustment_new(args->xmax, -1000.0, 1000.0, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.xmax), 0, 3);
    gtk_table_attach(table, spin, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Y-range:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls.ymin = gtk_adjustment_new(args->ymin, -1000.0, 1000.0, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.ymin), 0, 3);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gtk_label_new("–"), 2, 3, row, row+1, 0, 0, 0, 0);
    controls.ymax = gtk_adjustment_new(args->ymax, -1000.0, 1000.0, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.ymax), 0, 3);
    gtk_table_attach(table, spin, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    button = gtk_check_button_new_with_mnemonic(_("S_quare sample"));
    controls.xydimeq = button;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), args->xydimeq);
    gtk_table_attach(table, button, 0, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_set_row_spacing(table, row, 8);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Lateral units:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls.xy_units = gtk_entry_new();
    g_object_set_data(G_OBJECT(controls.xy_units), "id", (gpointer)"xy");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.xy_units);
    gtk_entry_set_text(GTK_ENTRY(controls.xy_units), args->xy_units);
    gtk_entry_set_width_chars(GTK_ENTRY(controls.xy_units), 6);
    gtk_table_attach(table, controls.xy_units, 1, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Value units:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls.z_units = gtk_entry_new();
    g_object_set_data(G_OBJECT(controls.z_units), "id", (gpointer)"z");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.z_units);
    gtk_entry_set_text(GTK_ENTRY(controls.z_units), args->z_units);
    gtk_entry_set_width_chars(GTK_ENTRY(controls.z_units), 6);
    gtk_table_attach(table, controls.z_units, 1, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_set_row_spacing(table, row, 8);
    row++;

    /* Options */
    gtk_table_attach(table, gwy_label_new_header(_("Options")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Interpolation type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls.interpolation
        = gwy_enum_combo_box_newl(NULL, NULL,
                                  args->interpolation,
                                  _("Round"), GWY_INTERPOLATION_ROUND,
                                  _("Linear"), GWY_INTERPOLATION_LINEAR,
                                  NULL);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.interpolation);
    gtk_table_attach(table, controls.interpolation, 1, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Exterior type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls.exterior
        = gwy_enum_combo_box_newl(NULL, NULL,
                                  args->exterior,
                                  _("Border"), GWY_EXTERIOR_BORDER_EXTEND,
                                  _("Mirror"), GWY_EXTERIOR_MIRROR_EXTEND,
                                  _("Periodic"), GWY_EXTERIOR_PERIODIC,
                                  NULL);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.exterior);
    gtk_table_attach(table, controls.exterior, 1, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    /* Right column */
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("Preview"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    controls.preview = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(vbox), controls.preview, FALSE, FALSE, 0);

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                            PREVIEW_SIZE, PREVIEW_SIZE);
    gdk_pixbuf_fill(pixbuf, 0);
    gtk_image_set_from_pixbuf(GTK_IMAGE(controls.preview), pixbuf);
    g_object_unref(pixbuf);

    controls.do_preview = gtk_button_new_with_mnemonic(_("_Update"));
    gtk_box_pack_start(GTK_BOX(vbox), controls.do_preview, FALSE, FALSE, 4);

    controls.error = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.error), 0.0, 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(controls.error), TRUE);
    gtk_widget_set_size_request(controls.error, PREVIEW_SIZE, -1);
    gtk_box_pack_start(GTK_BOX(vbox), controls.error, FALSE, FALSE, 0);

    /*
    g_signal_connect_swapped(controls.xy_units, "clicked",
                             G_CALLBACK(change_units), &controls);
    g_signal_connect_swapped(controls.z_units, "clicked",
                             G_CALLBACK(change_units), &controls);
    g_signal_connect_swapped(controls.do_preview, "clicked",
                             G_CALLBACK(preview), &controls);
    g_signal_connect_swapped(controls.xres, "value-changed",
                             G_CALLBACK(xyres_changed), &controls);
    g_signal_connect_swapped(controls.yres, "value-changed",
                             G_CALLBACK(xyres_changed), &controls);
    g_signal_connect_swapped(controls.xmin, "value-changed",
                             G_CALLBACK(xrange_changed), &controls);
    g_signal_connect_swapped(controls.xmax, "value-changed",
                             G_CALLBACK(yrange_changed), &controls);
    g_signal_connect_swapped(controls.ymin, "value-changed",
                             G_CALLBACK(yrange_changed), &controls);
    g_signal_connect_swapped(controls.ymax, "value-changed",
                             G_CALLBACK(xrange_changed), &controls);
    g_signal_connect_swapped(controls.xydimeq, "toggled",
                             G_CALLBACK(xydimeq_changed), &controls);
    g_signal_connect_swapped(controls.xymeasureeq, "toggled",
                             G_CALLBACK(xymeasureeq_changed), &controls);
                             */

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            gwy_resource_release(GWY_RESOURCE(controls.gradient));
            case GTK_RESPONSE_NONE:
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            /*
            update_string(controls.x_label, &args->x_label);
            update_string(controls.y_label, &args->y_label);
            */
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    gwy_resource_release(GWY_RESOURCE(controls.gradient));

    return FALSE;
}

static void
set_combo_from_unit(GtkWidget *combo,
                    const gchar *str)
{
    GwySIUnit *unit;
    gint power10;

    unit = gwy_si_unit_new_parse(str, &power10);
    gwy_combo_box_metric_unit_set_unit(GTK_COMBO_BOX(combo),
                                       power10 - 6, power10 + 6, unit);
    g_object_unref(unit);
}

static void
change_units(RawXYZControls *controls,
             GtkWidget *button)
{
    GtkWidget *dialog, *hbox, *label, *entry;
    const gchar *id, *unit;
    gint response;

    id = g_object_get_data(G_OBJECT(button), "id");
    dialog = gtk_dialog_new_with_buttons(_("Change Units"),
                                         GTK_WINDOW(controls->dialog),
                                         GTK_DIALOG_MODAL
                                         | GTK_DIALOG_NO_SEPARATOR,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("New _units:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    entry = gtk_entry_new();
    if (gwy_strequal(id, "xy"))
        gtk_entry_set_text(GTK_ENTRY(entry), controls->args->xy_units);
    else if (gwy_strequal(id, "z"))
        gtk_entry_set_text(GTK_ENTRY(entry), controls->args->z_units);
    else
        g_return_if_reached();
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response != GTK_RESPONSE_OK) {
        gtk_widget_destroy(dialog);
        return;
    }

    unit = gtk_entry_get_text(GTK_ENTRY(entry));
    if (gwy_strequal(id, "xy")) {
        //set_combo_from_unit(controls->xyexponent, unit);
        g_free(controls->args->xy_units);
        controls->args->xy_units = g_strdup(unit);
    }
    else if (gwy_strequal(id, "z")) {
        //set_combo_from_unit(controls->zexponent, unit);
        g_free(controls->args->z_units);
        controls->args->z_units = g_strdup(unit);
    }

    gtk_widget_destroy(dialog);
}

static void
preview(RawXYZControls *controls)
{
    RawXYZArgs *args = controls->args;
    GArray *points = controls->rfile->points;
    GwyDelaunayTriangulation *triangulation;
    GwySIUnit *unitxy, *unitz;
    GwyDataField *dfield;
    GdkPixbuf *pixbuf, *pixbuf2;
    GtkWidget *entry;
    gint xypow10, zpow10;
    gdouble zoom, avg, rms;

    entry = gtk_window_get_focus(GTK_WINDOW(controls->dialog));
    if (entry && GTK_IS_ENTRY(entry))
        gtk_widget_activate(entry);

    unitxy = gwy_si_unit_new_parse(args->xy_units, &xypow10);
    unitz = gwy_si_unit_new_parse(args->z_units, &zpow10);
    dfield = gwy_data_field_new(args->xres, args->yres,
                                pow10(xypow10)*(args->xmax - args->xmin),
                                pow10(xypow10)*(args->ymax - args->ymin),
                                FALSE);
    gwy_data_field_set_xoffset(dfield, pow10(xypow10)*args->xmin);
    gwy_data_field_set_yoffset(dfield, pow10(xypow10)*args->ymin);

    triangulation = gwy_delaunay_triangulate(points->len, points->data,
                                             sizeof(GwyDelaunayPointXYZ));
    gwy_delaunay_interpolate(triangulation,
                             points->data, sizeof(GwyDelaunayPointXYZ),
                             args->interpolation, dfield);
    gwy_delaunay_triangulation_free(triangulation);

    zoom = PREVIEW_SIZE/(gdouble)MAX(args->xres, args->yres);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                            args->xres, args->yres);
    avg = gwy_data_field_get_avg(dfield);
    rms = gwy_data_field_get_rms(dfield);
    gwy_pixbuf_draw_data_field(pixbuf, dfield, controls->gradient);
    pixbuf2 = gdk_pixbuf_scale_simple(pixbuf,
                                      ceil(args->xres*zoom),
                                      ceil(args->yres*zoom),
                                      GDK_INTERP_TILES);
    gtk_image_set_from_pixbuf(GTK_IMAGE(controls->preview), pixbuf2);
    g_object_unref(pixbuf2);
    g_object_unref(pixbuf);
    g_object_unref(dfield);
}

static void
rawxyz_free(RawXYZFile *rfile)
{
    g_array_free(rfile->points, TRUE);
}

static GArray*
read_points(gchar *p)
{
    GArray *points;
    gchar *line, *end;

    points = g_array_new(FALSE, FALSE, sizeof(GwyDelaunayPointXYZ));
    for (line = gwy_str_next_line(&p); line; line = gwy_str_next_line(&p)) {
        GwyDelaunayPointXYZ pt;

        if (!line[0] || line[0] == '#')
            continue;

        if (!(pt.x = g_ascii_strtod(line, &end)) && end == line)
            continue;
        line = end;
        if (!(pt.y = g_ascii_strtod(line, &end)) && end == line)
            continue;
        line = end;
        if (!(pt.z = g_ascii_strtod(line, &end)) && end == line)
            continue;

        g_array_append_val(points, pt);
    }

    return points;
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
    gdouble base = pow10(floor(log10(range)));

    *minval = round_with_base(*minval, base);
    *maxval = round_with_base(*maxval, base);
}

static void
initialize_ranges(const RawXYZFile *rfile,
                  RawXYZArgs *args)
{
    args->xmin = rfile->xmin;
    args->xmax = rfile->xmax;
    args->ymin = rfile->ymin;
    args->ymax = rfile->ymax;
    round_to_nice(&args->xmin, &args->xmax);
    round_to_nice(&args->ymin, &args->ymax);
}

static inline guint
coords_to_grid_index(guint xres,
                     guint yres,
                     gdouble step,
                     gdouble x,
                     gdouble y)
{
    guint ix, iy;

    ix = (gint)floor(x/step);
    if (G_UNLIKELY(ix == xres))
        ix--;

    iy = (gint)floor(y/step);
    if (G_UNLIKELY(iy == yres))
        iy--;

    return iy*xres + ix;
}

static inline void
index_accumulate(guint *index_array,
                 guint n)
{
    guint i;

    for (i = 1; i <= n; i++)
        index_array[i] += index_array[i-1];
}

static inline void
index_rewind(guint *index_array,
             guint n)
{
    guint i;

    for (i = n; i; i--)
        index_array[i] = index_array[i-1];
    index_array[0] = 0;
}

static void
work_queue_init(WorkQueue *queue)
{
    queue->size = 64;
    queue->len = 0;
    queue->id = g_new(guint, queue->size);
}

static void
work_queue_destroy(WorkQueue *queue)
{
    g_free(queue->id);
}

static void
work_queue_add(WorkQueue *queue,
               guint id)
{
    if (G_UNLIKELY(queue->len == queue->size)) {
        queue->size *= 2;
        queue->id = g_renew(guint, queue->id, queue->size);
    }
    queue->id[queue->len] = id;
    queue->len++;
}

static void
work_queue_ensure(WorkQueue *queue,
                  guint id)
{
    guint i;

    for (i = 0; i < queue->len; i++) {
        if (queue->id[i] == i)
            return;
    }
    work_queue_add(queue, id);
}

static inline gdouble
point_dist2(const GwyDelaunayPointXYZ *p,
            const GwyDelaunayPointXYZ *q)
{
    gdouble dx = p->x - q->x;
    gdouble dy = p->y - q->y;

    return dx*dx + dy*dy;
}

static gboolean
maybe_add_point(WorkQueue *pointqueue,
                const GwyDelaunayPointXYZ *newpoints,
                guint id,
                gdouble eps2)
{
    guint i;

    for (i = 0; i < pointqueue->pos; i++) {
        if (point_dist2(newpoints + id, newpoints + pointqueue->id[i]) < eps2) {
            GWY_SWAP(guint, pointqueue->id[i], pointqueue->id[pointqueue->pos]);
            pointqueue->pos++;
            return TRUE;
        }
    }
    return FALSE;
}

/* Calculate coordinate ranges and ensure points are more than epsrel*cellside
 * appart where cellside is the side of equivalent-area square for one point. */
static void
analyse_points(RawXYZFile *rfile,
               double epsrel)
{
    WorkQueue cellqueue, pointqueue;
    GwyDelaunayPointXYZ *points, *newpoints, *pt;
    gdouble xreal, yreal, eps, eps2, xr, yr, step;
    guint npoints, i, j, ig, xres, yres, ncells, oldpos;
    guint *cell_index;

    /* Calculate data ranges */
    npoints = rfile->points->len;
    points = (GwyDelaunayPointXYZ*)rfile->points->data;
    rfile->xmin = rfile->xmax = points[0].x;
    rfile->ymin = rfile->ymax = points[0].y;
    rfile->zmin = rfile->zmax = points[0].z;
    for (i = 0; i < npoints; i++) {
        pt = points + i;

        if (pt->x < rfile->xmin)
            rfile->xmin = pt->x;
        else if (pt->x > rfile->xmax)
            rfile->xmax = pt->x;

        if (pt->y < rfile->ymin)
            rfile->ymin = pt->y;
        else if (pt->y > rfile->ymax)
            rfile->ymax = pt->y;

        if (pt->z < rfile->zmin)
            rfile->zmin = pt->z;
        else if (pt->z > rfile->zmax)
            rfile->zmax = pt->z;
    }

    xreal = rfile->xmax - rfile->xmin;
    yreal = rfile->ymax - rfile->ymin;

    if (xreal == 0.0 || yreal == 0.0) {
        g_warning("All points lie on a line, we are going to crash.");
    }

    /* Make a virtual grid */
    xr = xreal/sqrt(npoints)*CELL_SIDE;
    yr = yreal/sqrt(npoints)*CELL_SIDE;

    if (xr <= yr) {
        xres = (guint)ceil(xreal/xr);
        step = xreal/xres;
        yres = (guint)ceil(yreal/step);
    }
    else {
        yres = (guint)ceil(yreal/yr);
        step = yreal/yres;
        xres = (guint)ceil(xreal/step);
    }
    eps = epsrel*step;
    eps2 = eps*eps;

    ncells = xres*yres;
    cell_index = g_new0(guint, ncells + 1);

    for (i = 0; i < npoints; i++) {
        pt = points + i;
        ig = coords_to_grid_index(xres, yres, step,
                                  pt->x - rfile->xmin, pt->y - rfile->ymin);
        cell_index[ig]++;
    }

    index_accumulate(cell_index, xres*yres);
    index_rewind(cell_index, xres*yres);
    newpoints = g_new(GwyDelaunayPointXYZ, npoints);

    /* Sort points by cell */
    for (i = 0; i < npoints; i++) {
        pt = points + i;
        ig = coords_to_grid_index(xres, yres, step,
                                  pt->x - rfile->xmin, pt->y - rfile->ymin);
        newpoints[cell_index[ig]] = *pt;
        cell_index[ig]++;
    }

    /* Find groups of identical (i.e. closer than epsrel) points we need to
     * merge.  We collapse all merged points to that with the lowest id.
     * Closeness must be transitive so the group must be gathered iteratively
     * until it no longer grows. */
    work_queue_init(&pointqueue);
    work_queue_init(&cellqueue);
    g_array_set_size(rfile->points, 0);
    for (i = 0; i < npoints; i++) {
        /* Ignore merged points */
        if (newpoints[i].z == G_MAXDOUBLE)
            continue;

        pointqueue.len = 0;
        cellqueue.len = 0;
        work_queue_add(&pointqueue, i);
        pointqueue.pos = 1;
        oldpos = 0;

        do {
            /* Update the list of cells to process.  Most of the time this is
             * no-op. */
            while (oldpos < pointqueue.pos) {
                gdouble x, y;
                guint ix, iy;

                pt = newpoints + pointqueue.id[oldpos];
                x = (pt->x - rfile->xmin)/step;
                ix = (guint)floor(x);
                x -= ix;
                y = (pt->y - rfile->ymin)/step;
                iy = (guint)floor(y);
                y -= iy;

                if (ix < xres && iy < yres)
                    work_queue_ensure(&cellqueue, iy*xres + ix);
                if (ix > 0 && iy < yres && x <= eps)
                    work_queue_ensure(&cellqueue, iy*xres + ix-1);
                if (ix < xres && iy > 0 && y <= eps)
                    work_queue_ensure(&cellqueue, (iy - 1)*xres + ix);
                if (ix > 0 && iy > 0 && x < eps && y <= eps)
                    work_queue_ensure(&cellqueue, (iy - 1)*xres + ix-1);
                if (ix+1 < xres && iy < xres && 1-x <= eps)
                    work_queue_ensure(&cellqueue, iy*xres + ix+1);
                if (ix < xres && iy+1 < xres && 1-y <= eps)
                    work_queue_ensure(&cellqueue, (iy + 1)*xres + ix);
                if (ix+1 < xres && iy+1 < xres && 1-x <= eps && 1-y <= eps)
                    work_queue_ensure(&cellqueue, (iy + 1)*xres + ix+1);

                oldpos++;
            }

            /* Process all points from the cells and check if they belong to
             * the currently merged group. */
            while (cellqueue.pos < cellqueue.len) {
                j = cellqueue.id[cellqueue.pos];
                for (i = cell_index[j]; i < cell_index[j+1]; i++) {
                    if (newpoints[i].z != G_MAXDOUBLE)
                        work_queue_add(&pointqueue, i);
                }
                cellqueue.pos++;
            }

            /* Compare all not-in-group points with all group points, adding
             * them to the group on success. */
            for (i = pointqueue.pos; i < pointqueue.len; i++)
                maybe_add_point(&pointqueue, newpoints, i, eps2);
        } while (oldpos != pointqueue.pos);

        /* Calculate the representant of all contributing points. */
        {
            GwyDelaunayPointXYZ avg = { 0.0, 0.0, 0.0 };

            for (i = 0; i < pointqueue.pos; i++) {
                pt = newpoints + pointqueue.id[i];
                avg.x += pt->x;
                avg.y += pt->y;
                avg.z += pt->z;
                pt->z = G_MAXDOUBLE;
            }
            avg.x /= pointqueue.pos;
            avg.y /= pointqueue.pos;
            avg.z /= pointqueue.pos;
            g_array_append_val(rfile->points, avg);
        }
    }

    work_queue_destroy(&cellqueue);
    work_queue_destroy(&pointqueue);

    g_free(cell_index);
}

static const gchar exterior_key[]      = "/module/rawxyz/exterior";
static const gchar interpolation_key[] = "/module/rawxyz/interpolation";
static const gchar xy_units_key[]      = "/module/rawxyz/xy-units";
static const gchar z_units_key[]       = "/module/rawxyz/z-units";

static void
rawxyz_sanitize_args(RawXYZArgs *args)
{
    if (args->interpolation != GWY_INTERPOLATION_ROUND)
        args->interpolation = GWY_INTERPOLATION_LINEAR;
    if (args->exterior != GWY_EXTERIOR_MIRROR_EXTEND
        && args->exterior != GWY_EXTERIOR_PERIODIC)
        args->exterior = GWY_EXTERIOR_BORDER_EXTEND;

}
static void
rawxyz_load_args(GwyContainer *container,
                 RawXYZArgs *args)
{
    *args = rawxyz_defaults;

    gwy_container_gis_enum_by_name(container, interpolation_key,
                                   &args->interpolation);
    gwy_container_gis_enum_by_name(container, exterior_key, &args->exterior);
    gwy_container_gis_string_by_name(container, xy_units_key,
                                     (const guchar**)&args->xy_units);
    gwy_container_gis_string_by_name(container, z_units_key,
                                     (const guchar**)&args->z_units);

    rawxyz_sanitize_args(args);
    args->xy_units = g_strdup(args->xy_units ? args->xy_units : "");
    args->z_units = g_strdup(args->z_units ? args->xy_units : "");
}

static void
rawxyz_save_args(GwyContainer *container,
                 RawXYZArgs *args)
{
    gwy_container_set_enum_by_name(container, interpolation_key,
                                   args->interpolation);
    gwy_container_set_enum_by_name(container, exterior_key, args->exterior);
    gwy_container_set_string_by_name(container, xy_units_key,
                                     g_strdup(args->xy_units));
    gwy_container_set_string_by_name(container, z_units_key,
                                     g_strdup(args->z_units));
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
