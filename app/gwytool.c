/*
 *  @(#) $Id$
 *  Copyright (C) 2006 David Necas (Yeti), Petr Klapetek.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 */
#define DEBUG 1
#include "config.h"
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/app.h>
#include <app/gwytool.h>

static void     gwy_tool_class_init    (GwyToolClass *klass);
static void     gwy_tool_init          (GwyTool *tool,
                                        gpointer g_class);
static void     gwy_tool_finalize      (GObject *object);
static void     gwy_tool_response      (GwyTool *tool,
                                        gint response);
static void     gwy_tool_show_real     (GwyTool *tool);
static void     gwy_tool_hide_real     (GwyTool *tool);

static gpointer gwy_tool_parent_class = NULL;

/* Note: We cannot use the G_DEFINE_TYPE machinery, because we have a
 * two-argument instance init function which would cause declaration
 * conflict. */
GType
gwy_tool_get_type (void)
{
    static GType gwy_tool_type = 0;

    if (G_UNLIKELY(gwy_tool_type == 0)) {
        static const GTypeInfo gwy_tool_type_info = {
            sizeof(GwyToolClass),
            NULL,
            NULL,
            (GClassInitFunc)gwy_tool_class_init,
            NULL,
            NULL,
            sizeof(GwyTool),
            0,
            (GInstanceInitFunc)gwy_tool_init,
            NULL,
        };

        gwy_tool_type = g_type_register_static(G_TYPE_OBJECT, "GwyTool",
                                               &gwy_tool_type_info,
                                               G_TYPE_FLAG_ABSTRACT);
    }

    return gwy_tool_type;
}


static void
gwy_tool_class_init(GwyToolClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gwy_tool_parent_class = g_type_class_peek_parent(klass);

    gobject_class->finalize = gwy_tool_finalize;

    klass->hide = gwy_tool_hide_real;
    klass->show = gwy_tool_show_real;
}

static void
gwy_tool_finalize(GObject *object)
{
    GwyTool *tool;

    tool = GWY_TOOL(object);
    gtk_widget_destroy(tool->dialog);

    G_OBJECT_CLASS(gwy_tool_parent_class)->finalize(object);
}

static void
gwy_tool_init(GwyTool *tool,
              gpointer g_class)
{
    GwyToolClass *klass;

    klass = GWY_TOOL_CLASS(g_class);
    gwy_debug("%s", klass->title);
    tool->dialog = gtk_dialog_new();
    gtk_dialog_set_has_separator(GTK_DIALOG(tool->dialog), FALSE);
    gtk_window_set_title(GTK_WINDOW(tool->dialog), gettext(klass->title));
    gwy_app_add_main_accel_group(GTK_WINDOW(tool->dialog));
    /* Prevent too smart window managers from making big mistakes */
    gtk_window_set_position(GTK_WINDOW(tool->dialog), GTK_WIN_POS_NONE);
    gtk_window_set_type_hint(GTK_WINDOW(tool->dialog),
                             GDK_WINDOW_TYPE_HINT_NORMAL);

    g_signal_connect(tool->dialog, "delete-event",
                     G_CALLBACK(gwy_dialog_prevent_delete_cb), NULL);
    g_signal_connect_swapped(tool->dialog, "response",
                             G_CALLBACK(gwy_tool_response), tool);

    tool->update_on_show = TRUE;
}

static void
gwy_tool_response(GwyTool *tool,
                  gint response)
{
    static guint response_id = 0;

    if (!response_id)
        response_id = g_signal_lookup("response", GTK_TYPE_DIALOG);

    switch (response) {
        case GTK_RESPONSE_CLOSE:
        case GTK_RESPONSE_DELETE_EVENT:
        g_signal_stop_emission(tool->dialog, response_id, 0);
        gwy_tool_hide(tool);
        break;

        case GTK_RESPONSE_NONE:
        g_warning("Tool dialog destroyed.");
        g_signal_stop_emission(tool->dialog, response_id, 0);
        g_object_unref(tool);
        break;

        default:
        {
            GwyToolClass *klass;

            klass = GWY_TOOL_GET_CLASS(tool);
            if (klass->response)
                klass->response(tool, response);
        }
        break;
    }
}

static void
gwy_tool_show_real(GwyTool *tool)
{
    gwy_debug("");
    tool->is_visible = TRUE;
    gtk_window_present(GTK_WINDOW(tool->dialog));
}

static void
gwy_tool_hide_real(GwyTool *tool)
{
    tool->is_visible = FALSE;
    gtk_widget_hide(tool->dialog);
}

void
gwy_tool_add_hide_button(GwyTool *tool,
                         gboolean set_default)
{
    GtkWidget *button;

    g_return_if_fail(GWY_IS_TOOL(tool));

    button = gtk_dialog_add_button(GTK_DIALOG(tool->dialog), _("_Hide"),
                                   GTK_RESPONSE_DELETE_EVENT);
    if (set_default)
        gtk_dialog_set_default_response(GTK_DIALOG(tool->dialog),
                                        GTK_RESPONSE_DELETE_EVENT);
}

/**
 * gwy_tool_show:
 * @tool: A tool.
 *
 * Shows a tool dialog.
 **/
void
gwy_tool_show(GwyTool *tool)
{
    GwyToolClass *klass;

    g_return_if_fail(GWY_IS_TOOL(tool));
    klass = GWY_TOOL_GET_CLASS(tool);
    gwy_debug("%s", klass->title);
    if (klass->show)
        klass->show(tool);
}

/**
 * gwy_tool_show:
 * @tool: A tool.
 *
 * Hides a tool dialog.
 **/
void
gwy_tool_hide(GwyTool *tool)
{
    GwyToolClass *klass;

    g_return_if_fail(GWY_IS_TOOL(tool));
    klass = GWY_TOOL_GET_CLASS(tool);
    gwy_debug("%s", klass->title);
    if (klass->hide)
        klass->hide(tool);
}

/**
 * gwy_tool_is_visible:
 * @tool: A tool.
 *
 * Checks whether a tool dialog is visible.
 *
 * Returns: %TRUE if tool dialog is visible, %FALSE if it is hidden.
 **/
gboolean
gwy_tool_is_visible(GwyTool *tool)
{
    g_return_val_if_fail(GWY_IS_TOOL(tool), FALSE);
    return tool->is_visible;
}

void
gwy_tool_data_switched(GwyTool *tool,
                       GwyDataView *data_view)
{
    GwyToolClass *klass;

    g_return_if_fail(GWY_IS_TOOL(tool));
    klass = GWY_TOOL_GET_CLASS(tool);
    gwy_debug("%s", klass->title);
    if (klass->data_switched)
        klass->data_switched(tool, data_view);
}

/**
 * gwy_tool_class_get_title:
 * @klass: A tool class.
 *
 * Gets the title of a tool class (this is a class method).
 *
 * The title is normally used as a tool dialog title.
 *
 * Returns: The title as a string owned by the tool class, untranslated.
 **/
const gchar*
gwy_tool_class_get_title(GwyToolClass *klass)
{
    g_return_val_if_fail(GWY_IS_TOOL_CLASS(klass), NULL);
    return klass->title;
}

/**
 * gwy_tool_class_get_stock_id:
 * @klass: A tool class.
 *
 * Gets the icon stock id of a tool class (this is a class method).
 *
 * Returns: The stock id as a string owned by the tool class.
 **/
const gchar*
gwy_tool_class_get_stock_id(GwyToolClass *klass)
{
    g_return_val_if_fail(GWY_IS_TOOL_CLASS(klass), NULL);
    return klass->stock_id;
}

/**
 * gwy_tool_class_get_tooltip:
 * @klass: A tool class.
 *
 * Gets the title of a tool class (this is a class method).
 *
 * Returns: The tooltip as a string owned by the tool class, untranslated.
 **/
const gchar*
gwy_tool_class_get_tooltip(GwyToolClass *klass)
{
    g_return_val_if_fail(GWY_IS_TOOL_CLASS(klass), NULL);
    return klass->tooltip;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwytool
 * @title: GwyTool
 * @short_description: Base class for tools
 **/

/**
 * GwyToolResponseType:
 * @GWY_TOOL_RESPONSE_CLEAR: Clear selection response.
 *
 * Common tool dialog responses.
 *
 * They do not have any special meaning for #GwyTool (yet?), nonetheless you
 * are encouraged to use them for consistency.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
