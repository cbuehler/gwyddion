/*
 *  $Id$
 *  Copyright (C) 2005 David Necas (Yeti), Petr Klwsxmtek.
 *  E-mail: yeti@gwyddion.net, klwsxmtek@gwyddion.net.
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

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/stats.h>

#include "err.h"
#include "get.h"

#define MAGIC "WSxM file copyright Nanotec Electronica\r\n" \
              "SxM Image file\r\n"
#define MAGIC_SIZE (sizeof(MAGIC) - 1)

typedef enum {
    WSXM_DATA_INT16,
    WSXM_DATA_DOUBLE
} WSxMDataType;

typedef struct {
    GString *str;
    GwyContainer *container;
} StoreMetaData;

static gboolean      module_register   (void);
static gint          wsxmfile_detect   (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static GwyContainer* wsxmfile_load     (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static GwyDataField* read_data_field   (const guchar *buffer,
                                        gint xres,
                                        gint yres,
                                        WSxMDataType type);
static gboolean      file_read_meta    (GHashTable *meta,
                                        gchar *buffer,
                                        GError **error);
static void          process_metadata  (GHashTable *meta,
                                        GwyContainer *container);
static void          guess_channel_type(GwyContainer *data,
                                        const gchar *key);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Nanotec WSxM data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.4",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_file_func_register("wsxmfile",
                           N_("WSXM files (.tom)"),
                           (GwyFileDetectFunc)&wsxmfile_detect,
                           (GwyFileLoadFunc)&wsxmfile_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
wsxmfile_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, ".tom") ? 20 : 0;

    if (fileinfo->buffer_len > MAGIC_SIZE
        && !memcmp(fileinfo->head, MAGIC, MAGIC_SIZE))
        score = 100;

    return score;
}

static GwyContainer*
wsxmfile_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    GHashTable *meta = NULL;
    WSxMDataType type = WSXM_DATA_INT16;
    guint header_size;
    gchar *p;
    gboolean ok;
    gint xres = 0, yres = 0;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (strncmp(buffer, MAGIC, MAGIC_SIZE)
        || sscanf(buffer + MAGIC_SIZE,
                  "Image header size: %u", &header_size) < 1) {
        err_FILE_TYPE(error, "WSXM");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    if (size < header_size) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    meta = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    p = g_strndup(buffer, header_size);
    ok = file_read_meta(meta, p, error);
    g_free(p);

    if (ok &&
        (!(p = g_hash_table_lookup(meta, "General Info::Number of columns"))
         || (xres = atol(p)) <= 0)) {
        err_INVALID(error, _("number of columns"));
        ok = FALSE;
    }

    if (ok &&
        (!(p = g_hash_table_lookup(meta, "General Info::Number of rows"))
         || (yres = atol(p)) <= 0)) {
        err_INVALID(error, _("number of rows"));
        ok = FALSE;
    }

    if (ok
        && (p = g_hash_table_lookup(meta, "General Info::Image Data Type"))) {
        if (gwy_strequal(p, "double"))
            type = WSXM_DATA_DOUBLE;
        else
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Unknown data type `%s'."), p);
    }

    if (ok && (guint)size - header_size < 2*xres*yres) {
        err_SIZE_MISMATCH(error, 2*xres*yres, (guint)size - header_size);
        ok = FALSE;
    }

    if (ok)
        dfield = read_data_field(buffer + header_size, xres, yres, type);
    gwy_file_abandon_contents(buffer, size, NULL);

    if (dfield) {
        container = gwy_container_new();
        gwy_container_set_object_by_name(container, "/0/data", dfield);
        g_object_unref(dfield);
        process_metadata(meta, container);
    }
    g_hash_table_destroy(meta);

    return container;
}

static gboolean
file_read_meta(GHashTable *meta,
               gchar *buffer,
               GError **error)
{
    gchar *p, *line, *key, *value, *section = NULL;
    guint len;

    while ((line = gwy_str_next_line(&buffer))) {
        line = g_strstrip(line);
        if (!(len = strlen(line)))
            continue;
        if (line[0] == '[' && line[len-1] == ']') {
            line[len-1] = '\0';
            section = line + 1;
            gwy_debug("Section <%s>", section);
            continue;
        }
        /* skip pre-header */
        if (!section)
            continue;

        p = strchr(line, ':');
        if (!p) {
            g_warning("Cannot parse line <%s>", line);
            continue;
        }
        *p = '\0';
        p += 2;

        value = g_convert(p, strlen(p), "UTF-8", "ISO-8859-1",
                          NULL, NULL, NULL);
        if (!value)
            continue;
        g_strstrip(value);
        if (!*value) {
            g_free(value);
            continue;
        }

        key = g_strconcat(section, "::", line, NULL);
        gwy_debug("<%s> = <%s>", key, value);
        g_hash_table_replace(meta, key, value);
    }
    if (!gwy_strequal(section, "Header end")) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Missing end of file header marker."));
        return FALSE;
    }

    return TRUE;
}

static void
store_meta(gpointer key,
           gpointer value,
           gpointer user_data)
{
    StoreMetaData *smd = (StoreMetaData*)user_data;

    g_string_truncate(smd->str, sizeof("/meta/") - 1);
    g_string_append(smd->str, key);
    gwy_container_set_string_by_name(smd->container, smd->str->str,
                                     g_strdup(value));
}

static void
process_metadata(GHashTable *meta,
                 GwyContainer *container)
{
    const gchar *nometa[] = {
        "General Info::Z Amplitude",
        "Control::X Amplitude", "Control::Y Amplitude",
        "General Info::Number of rows", "General Info::Number of columns",
    };
    StoreMetaData smd;
    GwyDataField *dfield;
    GwySIUnit *siunit;
    gdouble r;
    gchar *p, *end;
    gint power10;
    guint i;
    gdouble min, max;

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(container,
                                                             "/0/data"));

    /* Fix value scale */
    if (!(p = g_hash_table_lookup(meta, "General Info::Z Amplitude"))
        || (r = g_ascii_strtod(p, &end)) <= 0
        || end == p) {
        g_warning("Missing or invalid Z Amplitude");
        gwy_data_field_multiply(dfield, 1e-9);
    }
    else {
        /* import `arbitrary units' as unit-less */
        while (g_ascii_isspace(*end))
            end++;
        if (gwy_strequal(end, "a.u."))
            siunit = gwy_si_unit_new("");
        else {
            siunit = gwy_si_unit_new_parse(end, &power10);
            r *= pow10(power10);
        }
        gwy_data_field_set_si_unit_z(dfield, siunit);
        g_object_unref(siunit);

        gwy_data_field_get_min_max(dfield, &min, &max);
        gwy_data_field_multiply(dfield, r/(max - min));

        guess_channel_type(container, "/0/data");
    }

    /* Fix lateral scale */
    if (!(p = g_hash_table_lookup(meta, "Control::X Amplitude"))
        || (r = g_ascii_strtod(p, &end)) <= 0
        || end == p) {
        g_warning("Missing or invalid X Amplitude");
    }
    else {
        siunit = gwy_si_unit_new_parse(end, &power10);
        gwy_data_field_set_si_unit_xy(dfield, siunit);
        g_object_unref(siunit);

        gwy_data_field_set_xreal(dfield, r*pow10(power10));
    }

    if (!(p = g_hash_table_lookup(meta, "Control::Y Amplitude"))
        || (r = g_ascii_strtod(p, &end)) <= 0
        || end == p) {
        g_warning("Missing or invalid Y Amplitude");
        gwy_data_field_set_yreal(dfield, gwy_data_field_get_xreal(dfield));
    }
    else {
        siunit = gwy_si_unit_new_parse(end, &power10);
        g_object_unref(siunit);
        gwy_data_field_set_yreal(dfield, r*pow10(power10));
    }

    /* And store everything else as metadata */
    for (i = 0; i < G_N_ELEMENTS(nometa); i++)
        g_hash_table_remove(meta, nometa[i]);

    smd.str = g_string_new("/meta/");
    smd.container = container;
    g_hash_table_foreach(meta, store_meta, &smd);
    g_string_free(smd.str, TRUE);
}

static GwyDataField*
read_data_field(const guchar *buffer,
                gint xres,
                gint yres,
                WSxMDataType type)
{
    GwyDataField *dfield;
    gdouble *data;
    guint i;

    dfield = gwy_data_field_new(xres, yres, 1e-6, 1e-6, FALSE);
    data = gwy_data_field_get_data(dfield);
    switch (type) {
        case WSXM_DATA_INT16: {
            const gint16 *p = (const gint16*)buffer;

            for (i = 0; i < xres*yres; i++)
                data[i] = GINT16_FROM_LE(p[i]);
        }
        break;

        case WSXM_DATA_DOUBLE: {
            for (i = 0; i < xres*yres; i++)
                data[i] = get_DOUBLE_LE(&buffer);
        }
        break;

        default:
        g_assert_not_reached();
        break;
    }

    return dfield;
}

/**
 * guess_channel_type:
 * @data: A data container.
 * @key: Data channel key.
 *
 * Adds a channel title based on data field units.
 *
 * The guess is very simple, but probably better than `Unknown channel' in
 * most cases.  If there already is a title it is left intact, making use of
 * this function as a fallback easier.
 **/
static void
guess_channel_type(GwyContainer *data,
                   const gchar *key)
{
    GwySIUnit *siunit, *test;
    GwyDataField *dfield;
    const gchar *title;
    GQuark quark;
    gchar *s;

    s = g_strconcat(key, "/title", NULL);
    quark = g_quark_from_string(s);
    g_free(s);
    if (gwy_container_contains(data, quark))
        return;

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(data, key));
    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));
    siunit = gwy_data_field_get_si_unit_z(dfield);
    test = gwy_si_unit_new(NULL);
    title = NULL;

    if (!title) {
        gwy_si_unit_set_from_string(test, "m");
        if (gwy_si_unit_equal(siunit, test))
            title = "Topography";
    }
    if (!title) {
        gwy_si_unit_set_from_string(test, "A");
        if (gwy_si_unit_equal(siunit, test))
            title = "Current";
    }
    if (!title) {
        gwy_si_unit_set_from_string(test, "deg");
        if (gwy_si_unit_equal(siunit, test))
            title = "Phase";
    }

    g_object_unref(test);
    if (title)
        gwy_container_set_string(data, quark, g_strdup(title));
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

