/*
 *  @(#) $Id$
 *  Copyright (C) 2007 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_MODULEUTILS_FILE_H__
#define __GWY_MODULEUTILS_FILE_H__

#include <libgwyddion/gwycontainer.h>

G_BEGIN_DECLS

#define GWY_DEFINE_BUFFER_GETTER(lt, ut, mt, lb, ub) \
    static inline lt \
    gwy_get_ ## lt ## _ ## lb(const guchar **ppv) \
    { \
        const mt *pv = *(const mt**)ppv; \
        mt v = ut ## _FROM_ ## ub(*pv); \
        *ppv += sizeof(mt); \
        return *(lt*)&v; \
    }

GWY_DEFINE_BUFFER_GETTER(gint16,  GINT16,  gint16,  le, LE)
GWY_DEFINE_BUFFER_GETTER(gint16,  GINT16,  gint16,  be, BE)
GWY_DEFINE_BUFFER_GETTER(guint16, GUINT16, guint16, le, LE)
GWY_DEFINE_BUFFER_GETTER(guint16, GUINT16, guint16, be, BE)
GWY_DEFINE_BUFFER_GETTER(gint32,  GINT32,  gint32,  le, LE)
GWY_DEFINE_BUFFER_GETTER(gint32,  GINT32,  gint32,  be, BE)
GWY_DEFINE_BUFFER_GETTER(guint32, GUINT32, guint32, le, LE)
GWY_DEFINE_BUFFER_GETTER(guint32, GUINT32, guint32, be, BE)
GWY_DEFINE_BUFFER_GETTER(gint64,  GINT64,  gint64,  le, LE)
GWY_DEFINE_BUFFER_GETTER(gint64,  GINT64,  gint64,  be, BE)
GWY_DEFINE_BUFFER_GETTER(guint64, GUINT64, guint64, le, LE)
GWY_DEFINE_BUFFER_GETTER(guint64, GUINT64, guint64, be, BE)
GWY_DEFINE_BUFFER_GETTER(gfloat,  GUINT32, guint32, le, LE)
GWY_DEFINE_BUFFER_GETTER(gfloat,  GUINT32, guint32, be, BE)
GWY_DEFINE_BUFFER_GETTER(gdouble, GUINT64, guint64, le, LE)
GWY_DEFINE_BUFFER_GETTER(gdouble, GUINT64, guint64, be, BE)

#undef GWY_DEFINE_BUFFER_GETTER

gboolean gwy_app_channel_check_nonsquare(GwyContainer *data,
                                         gint id);
gboolean gwy_app_channel_title_fall_back(GwyContainer *data,
                                         gint id);

G_END_DECLS

#endif /* __GWY_MODULEUTILS_FILE_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
