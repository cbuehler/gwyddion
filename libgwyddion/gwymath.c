/*
 *  @(#) $Id$
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek.
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

#include <math.h>

#include <libgwyddion/gwymacros.h>
#include "gwymath.h"

/**
 * gwy_math_SI_prefix:
 * @magnitude: A power of 1000.
 *
 * Finds SI prefix corresponding to a given power of 1000.
 *
 * In fact, @magnitude doesn't have to be power of 1000, but then the result
 * is mostly meaningless.
 *
 * Returns: The SI unit prefix corresponding to @magnitude, "?" if @magnitude
 *          is outside of the SI prefix range.  The returned value must be
 *          considered constant and never modified or freed.
 **/
const gchar*
gwy_math_SI_prefix(gdouble magnitude)
{
    static const gchar *positive[] = {
        "", "k", "M", "G", "T", "P", "E", "Z", "Y"
    };
    static const gchar *negative[] = {
        "", "m", "µ", "n", "p", "f", "a", "z", "y"
    };
    static const gchar *unknown = "?";
    gint i;

    i = ROUND(log10(magnitude)/3.0);
    if (i >= 0 && i < (gint)G_N_ELEMENTS(positive))
        return positive[i];
    if (i <= 0 && -i < (gint)G_N_ELEMENTS(negative))
        return negative[-i];
    /* FIXME: the vertical ruler text placing routine can't reasonably
     * break things like 10<sup>-36</sup> to lines */
    g_warning("magnitude %g outside of prefix range.  FIXME!", magnitude);

    return unknown;
}

/**
 * gwy_math_humanize_numbers:
 * @unit: The smallest possible step.
 * @maximum: The maximum possible value.
 * @precision: A location to store printf() precession, if not %NULL.
 *
 * Find a human readable representation for a range of numbers.
 *
 * Returns: The magnitude i.e., a power of 1000.
 **/
gdouble
gwy_math_humanize_numbers(gdouble unit,
                          gdouble maximum,
                          gint *precision)
{
    gdouble lm, lu, mag, range, min;

    lm = log10(maximum);
    lu = log10(unit);
    mag = 3.0*floor((lm + lu)/6.0);
    if (precision) {
        range = lm - lu;
        if (range > 3.0)
            range = (range + 3.0)/2;
        min = lm - range;
        *precision = (min < mag) ? (gint)ceil(mag - min) : 0;
    }

    return exp(G_LN10*mag);
}

/**
 * gwy_math_find_nearest_line:
 * @x: X-coordinate of the point to search.
 * @y: Y-coordinate of the point to search.
 * @d2min: Where to store the squared minimal distance, or %NULL.
 * @n: The number of lines (i.e. @coords has 4@n items).
 * @coords: Line coordinates stored as x00, y00, x01, y01, x10, y10, etc.
 *
 * Find the line from @coords nearest to the point (@x, @y).
 *
 * Returns: The line number. It may return -1 if (@x, @y) doesn't lie
 *          in the orthogonal stripe of any of the lines.
 **/
gint
gwy_math_find_nearest_line(gdouble x, gdouble y,
                           gdouble *d2min,
                           gint n, gdouble *coords)
{
    gint i, m;
    gdouble d2m = G_MAXDOUBLE;

    g_return_val_if_fail(n > 0, d2m);
    g_return_val_if_fail(coords, d2m);

    m = -1;
    for (i = 0; i < n; i++) {
        gdouble xl0 = *(coords++);
        gdouble yl0 = *(coords++);
        gdouble xl1 = *(coords++);
        gdouble yl1 = *(coords++);
        gdouble vx, vy, d;

        vx = yl1 - yl0;
        vy = xl0 - xl1;
        if (vx*(y - yl0) < vy*(x - xl0))
            continue;
        if (vx*(yl1 - y) < vy*(xl1 - x))
            continue;
        if (vx == 0.0 && vy == 0.0)
            continue;
        d = vx*(x - xl0) + vy*(y - yl0);
        d *= d/(vx*vx + vy*vy);
        if (d < d2m) {
            d2m = d;
            m = i;
        }
    }
    if (d2min)
      *d2min = d2m;

    return m;
}

/**
 * gwy_math_find_nearest_point:
 * @x: X-coordinate of the point to search.
 * @y: Y-coordinate of the point to search.
 * @d2min: Where to store the squared minimal distance, or %NULL.
 * @n: The number of points (i.e. @coords has 2@n items).
 * @coords: Point coordinates stored as x0, y0, x1, y1, x2, y2, etc.
 *
 * Find the point from @coords nearest to the point (@x, @y).
 *
 * Returns: The point number.
 **/
gint
gwy_math_find_nearest_point(gdouble x, gdouble y,
                            gdouble *d2min,
                            gint n, gdouble *coords)
{
    gint i, m;
    gdouble d2m = G_MAXDOUBLE;

    g_return_val_if_fail(n > 0, d2m);
    g_return_val_if_fail(coords, d2m);

    m = 0;
    for (i = 0; i < n; i++) {
        gdouble xx = *(coords++);
        gdouble yy = *(coords++);
        gdouble d;

        d = (xx - x)*(xx - x) + (yy - y)*(yy - y);
        if (d < d2m) {
            d2m = d;
            m = i;
        }
    }
    if (d2min)
      *d2min = d2m;

    return m;
}

/**
 * gwy_math_lin_solve:
 * @n: The size of the system.
 * @matrix: The matrix of the system (@n times @n), ordered by row, then
 *          column.
 * @rhs: The right hand side of the sytem.
 * @result: Where the result should be stored.  May be %NULL to allocate
 *          a fresh array for the result.
 *
 * Solve a regular system of linear equations.
 *
 * Returns: The solution (@result if it wasn't %NULL), may be %NULL if the
 *          matrix is singular.
 **/
gdouble*
gwy_math_lin_solve(gint n, const gdouble *matrix,
                   const gdouble *rhs,
                   gdouble *result)
{
    gdouble *m, *r;

    g_return_val_if_fail(n > 0, NULL);
    g_return_val_if_fail(matrix && rhs, NULL);

    m = (gdouble*)g_memdup(matrix, n*n*sizeof(gdouble));
    r = (gdouble*)g_memdup(rhs, n*sizeof(gdouble));
    result = gwy_math_lin_solve_rewrite(n, m, r, result);
    g_free(r);
    g_free(m);

    return result;
}

/**
 * gwy_math_lin_solve_rewrite:
 * @n: The size of the system.
 * @matrix: The matrix of the system (@n times @n), ordered by row, then
 *          column.
 * @rhs: The right hand side of the sytem.
 * @result: Where the result should be stored.  May be %NULL to allocate
 *          a fresh array for the result.
 *
 * Solve a regular system of linear equations.
 *
 * This is a memory-conservative version of gwy_math_lin_solve() overwriting
 * @matrix and @rhs with intermediate results.
 *
 * Returns: The solution (@result if it wasn't %NULL), may be %NULL if the
 *          matrix is singular.
 **/
gdouble*
gwy_math_lin_solve_rewrite(gint n, gdouble *matrix,
                           gdouble *rhs,
                           gdouble *result)
{
    gint *perm;
    gint i, j, jj;

    g_return_val_if_fail(n > 0, NULL);
    g_return_val_if_fail(matrix && rhs, NULL);

    perm = g_new(gint, n);

    /* elimination */
    for (i = 0; i < n; i++) {
        gdouble *row = matrix + i*n;
        gdouble piv = 0;
        gint pivj = 0;

        /* find pivot */
        for (j = 0; j < n; j++) {
            if (fabs(row[j]) > piv) {
                pivj = j;
                piv = fabs(row[j]);
            }
        }
        if (piv == 0.0) {
            g_warning("Singluar matrix");
            g_free(perm);
            return NULL;
        }
        piv = row[pivj];
        perm[i] = pivj;

        /* substract */
        for (j = i+1; j < n; j++) {
            gdouble *jrow = matrix + j*n;
            gdouble q = jrow[pivj]/piv;

            for (jj = 0; jj < n; jj++)
                jrow[jj] -= q*row[jj];

            jrow[pivj] = 0.0;
            rhs[j] -= q*rhs[i];
        }
    }

    /* back substitute */
    if (!result)
        result = g_new(gdouble, n);
    for (i = n-1; i >= 0; i--) {
        gdouble *row = matrix + i*n;
        gdouble x = rhs[i];

        for (j = n-1; j > i; j--)
            x -= result[perm[j]]*row[perm[j]];

        result[perm[i]] = x/row[perm[i]];
    }
    g_free(perm);

    return result;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
