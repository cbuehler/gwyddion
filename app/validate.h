/*
 *  @(#) $Id$
 *  Copyright (C) 2007 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

#ifndef __GWY_APP_VALIDATE_H__
#define __GWY_APP_VALIDATE_H__

#include <libgwyddion/gwycontainer.h>

G_BEGIN_DECLS

typedef enum {
    GWY_DATA_ERROR_KEY_FORMAT,
    GWY_DATA_ERROR_KEY_CHARACTERS,
    GWY_DATA_ERROR_KEY_UNKNOWN,
    GWY_DATA_ERROR_KEY_ID,
    GWY_DATA_ERROR_UNEXPECTED_TYPE,
    GWY_DATA_ERROR_NON_UTF8_STRING,
} GwyDataError;

typedef enum {
    GWY_DATA_VALIDATE_REF_COUNT = 1 << 0,
} GwyDataValidateFlags;

typedef struct {
    GwyDataError error;
    GQuark key;
    gchar *msg;
} GwyDataValidationFailure;

GSList*
gwy_data_validate(GwyContainer *data,
                  GwyDataValidateFlags flags);

void
gwy_data_validation_failure_list_free(GSList *list);

G_END_DECLS

#endif /* __GWY_APP_VALIDATE_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

