/*
 *  @(#) $Id$
 *  Copyright (C) 2003,2004 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@physics.muni.cz, klapetek@physics.muni.cz.
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

#ifndef __GWY_LAYER_POINTS_H__
#define __GWY_LAYER_POINTS_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>

#ifndef GWY_TYPE_DATA_VIEW_LAYER
#  include <libgwydgets/gwyvectorlayer.h>
#endif /* no GWY_TYPE_DATA_VIEW_LAYER */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GWY_TYPE_LAYER_POINTS            (gwy_layer_points_get_type())
#define GWY_LAYER_POINTS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_LAYER_POINTS, GwyLayerPoints))
#define GWY_LAYER_POINTS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_LAYER_POINTS, GwyLayerPointsClass))
#define GWY_IS_LAYER_POINTS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_LAYER_POINTS))
#define GWY_IS_LAYER_POINTS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_LAYER_POINTS))
#define GWY_LAYER_POINTS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_LAYER_POINTS, GwyLayerPointsClass))

typedef struct _GwyLayerPoints      GwyLayerPoints;
typedef struct _GwyLayerPointsClass GwyLayerPointsClass;

struct _GwyLayerPoints {
    GwyVectorLayer parent_instance;

    gint npoints;
    gint nselected;
    gint inear;
    guint button;
    gdouble *points;
};

struct _GwyLayerPointsClass {
    GwyVectorLayerClass parent_class;

    GdkCursor *near_cursor;
    GdkCursor *move_cursor;
};

GType            gwy_layer_points_get_type        (void) G_GNUC_CONST;
GtkObject*       gwy_layer_points_new             (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GWY_LAYER_POINTS_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

