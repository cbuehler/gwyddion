/*
 *  @(#) $Id$
 *  Copyright (C) 2016 David Necas (Yeti).
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/spline.h>

#define PointXY GwyTriangulationPointXY

typedef enum {
    CURVE_RECURSE_OUTPUT_X_Y,
    CURVE_RECURSE_OUTPUT_T_L,
} GwySplineRecurseOutputType;

typedef struct {
    gdouble ux;
    gdouble uy;
    gdouble vx;
    gdouble vy;
} ControlPoint;

typedef struct {
    const PointXY *pt0;
    const PointXY *pt1;
    const ControlPoint *uv;
    gdouble max_dev;
    gdouble max_vrdev;
    /* These either (x,y) pairs or (t,l) pairs, depending on the output type. */
    GArray *points;
    GwySplineRecurseOutputType otype;
    gint depth;
} GwySplineSampleParams;

typedef struct {
    PointXY z;
    PointXY v;
    gdouble t;
    gdouble vl;
} GwySplineSampleItem;

struct _GwySpline {
    /* Properties set from outside. */
    GArray *points;
    gdouble slackness;
    gboolean closed;

    /* Cached data.  These change whenever anything above changes.  */
    gboolean natural_sampling_valid;
    GArray *control_points;
    GArray *natural_points;
    gdouble length;

    /* These cache the last result of gwy_spline_sample() and become invalid
     * whenever anything above changes or gwy_spline_sample() is called for
     * a different number of points.  */
    gboolean fixed_sampling_valid;
    guint nfixed;
    GArray *fixed_samples;
};

static void gwy_spline_invalidate (GwySpline *spline);
static void sample_curve_uniformly(GwySpline *spline,
                                   guint nsamples,
                                   PointXY *coords,
                                   PointXY *velocities);

/**
 * gwy_spline_new:
 *
 * Creates a new empty spline curve.
 *
 * You need to set the curve points using gwy_spline_set_points() before any
 * sampling along the curve.  Alternatively, use gwy_spline_from_points()
 * to construct the spline already with some points.
 *
 * Returns: A newly created spline curve.
 *
 * Since: 2.45
 **/
GwySpline*
gwy_spline_new(void)
{
    GwySpline *spline = g_slice_new0(GwySpline);

    spline->points = g_array_new(FALSE, FALSE, sizeof(PointXY));
    spline->slackness = 1.0;
    spline->control_points = g_array_new(FALSE, FALSE, sizeof(ControlPoint));
    spline->natural_points = g_array_new(FALSE, FALSE, sizeof(PointXY));
    spline->fixed_samples = g_array_new(FALSE, FALSE, sizeof(PointXY));

    return spline;
}

/**
 * gwy_spline_free:
 * @spline: A spline curve.
 *
 * Frees a spline curve and all associated resources.
 *
 * Since: 2.45
 **/
void
gwy_spline_free(GwySpline *spline)
{
    g_return_if_fail(spline);
    g_array_free(spline->fixed_samples, TRUE);
    g_array_free(spline->natural_points, TRUE);
    g_array_free(spline->control_points, TRUE);
    g_array_free(spline->points, TRUE);
    g_slice_free(GwySpline, spline);
}

/**
 * gwy_spline_from_points:
 * @xy: Array of points in plane the curve will pass through.
 * @n: Number of points in @xy[].
 *
 * Creates a new spline curve passing through given points.
 *
 * Returns: A newly created spline curve.
 *
 * Since: 2.45
 **/
GwySpline*
gwy_spline_from_points(const PointXY *xy,
                       guint n)
{
    GwySpline *spline = gwy_spline_new();
    gwy_spline_set_points(spline, xy, n);
    return spline;
}

/**
 * gwy_spline_get_npoints:
 * @spline: A spline curve.
 *
 * Gets the number of points of a spline curve.
 *
 * Returns: The number of XY points defining the curve.
 *
 * Since: 2.45
 **/
guint
gwy_spline_get_npoints(GwySpline *spline)
{
    return spline->points->len;
}

/**
 * gwy_spline_get_points:
 * @spline: A spline curve.
 *
 * Gets the coordinates of spline curve points.
 *
 * Returns: Coordinates of the XY points defining the curve.  The returned
 *          array is owned by @spline, must not be modified and is only
 *          guaranteed to exist so long as the spline is not modified nor
 *          destroyed.
 *
 * Since: 2.45
 **/
const PointXY*
gwy_spline_get_points(GwySpline *spline)
{
    return (PointXY*)spline->points->data;
}

/**
 * gwy_spline_get_slackness:
 * @spline: A spline curve.
 *
 * Gets the slackness parameter of a spline curve.
 *
 * See gwy_spline_set_slackness() for discussion.
 *
 * Returns: The slackness parameter value.
 *
 * Since: 2.45
 **/
gdouble
gwy_spline_get_slackness(GwySpline *spline)
{
    return spline->slackness;
}

/**
 * gwy_spline_get_closed:
 * @spline: A spline curve.
 *
 * Reports whether a spline curve is closed or not.
 *
 * See gwy_spline_set_closed() for discussion.
 *
 * Returns: %TRUE if @spline is closed, %FALSE if it is open-ended.
 *
 * Since: 2.45
 **/
gboolean
gwy_spline_get_closed(GwySpline *spline)
{
    return spline->closed;
}

/**
 * gwy_spline_set_points:
 * @xy: Array of points in plane the curve will pass through.
 * @n: Number of points in @xy[].
 *
 * Sets the coordinates of XY points a spline curve should pass through.
 *
 * It is possible to pass @n=0 to make the spline empty (@xy can be NULL then)
 * but such spline may not be sampled.
 *
 * Since: 2.45
 **/
void
gwy_spline_set_points(GwySpline *spline,
                      const PointXY *xy,
                      guint n)
{
    GArray *points = spline->points;

    if (points->len == n
        && memcmp(xy, points->data, n*sizeof(PointXY)) == 0)
        return;

    g_array_set_size(spline->points, 0);
    g_array_append_vals(spline->points, xy, n);
    gwy_spline_invalidate(spline);
}

/**
 * gwy_spline_set_slackness:
 * @spline: A spline curve.
 * @slackness: New slackness parameter value from the range [0, 1].
 *
 * Sets the slackness parameter of a spline curve.
 *
 * The slackness parameter determines how taut or slack the curve is.
 *
 * The curve always passes through the given XY points.  For zero slackness
 * the curve is maximally taut, i.e. the shortest possible passing
 * through the points.  Such curve is formed by straight segments.  For
 * slackness of 1 the curve is a ‘free’ spline.  This is also the default.
 *
 * Since: 2.45
 **/
void
gwy_spline_set_slackness(GwySpline *spline,
                         gdouble slackness)
{
    if (spline->slackness == slackness)
        return;

    /* XXX: We may permit slackness > 1 for some interesting and possibly still
     * useful curves.  Up to approximately sqrt(2) seems reasonable. */
    if (!(slackness >= 0.0 && slackness <= 1.0)) {
        g_warning("Slackness parameter %g is out of bounds.", slackness);
        return;
    }
    spline->slackness = slackness;
    gwy_spline_invalidate(spline);
}

/**
 * gwy_spline_set_closed:
 * @spline: A spline curve.
 * @closed: %TRUE to make @spline closed, %FALSE to make it open-ended.
 *
 * Sets whether a spline curve is closed or open.
 *
 * In closed curve the last point is connected smoothly with the first point,
 * forming a cycle.  Note you should not repeat the point in the @xy array.
 * When a closed curve is sampled, the sampling starts from the first point
 * and continues beyond the last point until it gets close to the first point
 * again.
 *
 * An open curve begins with the first point and ends with the last point.  It
 * has zero curvature at these two points.
 *
 * Since: 2.45
 **/
void
gwy_spline_set_closed(GwySpline *spline,
                      gboolean closed)
{
    if (!spline->closed == !closed)
        return;

    spline->closed = !!closed;
    gwy_spline_invalidate(spline);
}

/**
 * gwy_spline_length:
 * @spline: A spline curve.
 *
 * Calculates the length of a spline curve.
 *
 * This is useful when you want to sample the curve with a specific step
 * (at least approximately).
 *
 * Note gwy_spline_sample() also returns the length.
 *
 * Returns: The curve length in whatever units the XY coordinates are expressed
 *          in.
 *
 * Since: 2.45
 **/
gdouble
gwy_spline_length(GwySpline *spline)
{
    if (spline->natural_sampling_valid)
        return spline->length;

    /* TODO */
    /* Here we set natural_sampling_valid, but not fixed_sampling_valid. */
    return 0.0;
}

/**
 * gwy_spline_sample:
 * @spline: A spline curve.
 * @xy: Array where the sampled point coordinates should be stored in.
 * @n: The number of samples to take.
 *
 * Samples uniformly a spline curve.
 *
 * This function calculates coordinates of points that lie on the spline curve
 * and are equidistant along it.  For open curves the first sampled point
 * coincides with the first given XY point and, similar, the last with the last.
 * For closed curves the first point again coincides with the first given XY
 * point but the last lies one sampling distance before the curve gets back
 * again to the first point.
 *
 * If you want to specify the sampling step instead of the number of samples
 * use gwy_spline_length() first to obtain the curve length and calculate @n
 * accordingly.
 *
 * A single-point curve always consists of a single point.  Hence all samples
 * lie in this point.  A two-point curve is always formed by straight segments,
 * in the case of a closed curve one going forward and the other back.  A
 * meaningful sampling requires @n at least 2, nevertheless, the function
 * permits also @n of one or zero.
 *
 * Returns: The curve length in whatever units the XY coordinates are expressed
 *          in.
 *
 * Since: 2.45
 **/
gdouble
gwy_spline_sample(GwySpline *spline,
                  GwyTriangulationPointXY *xy,
                  guint n)
{
    if (spline->fixed_sampling_valid && spline->nfixed == n) {
        memcpy(xy, spline->fixed_samples->data, n*sizeof(PointXY));
        return spline->length;
    }

    /* TODO */
    /* Here we ensure natural_sampling_valid, and set fixed_sampling_valid. */
    return 0.0;
}

static void
gwy_spline_invalidate(GwySpline *spline)
{
    spline->natural_sampling_valid = FALSE;
    spline->fixed_sampling_valid = FALSE;
}

/**
 * division_time:
 * @v0: The velocity of entering the line in its first endpoint.
 * @v1: The velocity of leaving the line in its second endpoint.
 * @x: Requested fraction of distance in the line (to total line length).
 *
 * Assuming a point moving with a constant acceleration along a straight line
 * calculate fraction of time corresponding to given fraction of distance.
 *
 * Returns: Fraction of time (to total travel time) corresponding to fraction
 *          of distance @x.
 **/
static inline gdouble
division_time(gdouble v0, gdouble v1, gdouble x)
{
    gdouble eps, eps1;

    /* This includes v0 == v1 == 0. */
    if (v0 == v1)
        return x;

    eps = (v1 - v0)/(v1 + v0);
    if (eps < 1e-6)
        return x*(1.0 + eps*(1.0 - x));

    eps1 = 1.0 - eps;
    return (sqrt(4*x*eps + eps1*eps1) - eps1)/(2.0*eps);
}

/**
 * interpolate_z:
 * @pt0: Coordinates of previous point.
 * @pt1: Coordinates of next point.
 * @uv: Coordinates of control points.
 * @t: Time to get position at, in range [0, 1].
 * @z: Location to store x and y coordinates at time @t.
 *
 * Interpolate position in one spline segment.
 **/
static inline void
interpolate_z(const PointXY *pt0,
              const PointXY *pt1,
              const ControlPoint *uv,
              gdouble t,
              PointXY *z)
{
    gdouble s = 1.0 - t;
    gdouble s2 = s*s, s3 = s2*s;
    gdouble t2 = t*t, t3 = t2*t;

    z->x = (s3*pt0->x + 3.0*(s2*t*uv->ux + s*t2*uv->vx) + t3*pt1->x);
    z->y = (s3*pt0->y + 3.0*(s2*t*uv->uy + s*t2*uv->vy) + t3*pt1->y);
}

/**
 * interpolate_v:
 * @pt0: Coordinates of previous point.
 * @pt1: Coordinates of next point.
 * @uv: Coordinates of control points.
 * @t: Time to get velocity at, in range [0, 1].
 * @z: Location to store x and y velocity components at time @t.
 *
 * Interpolate velocity in one spline segment.
 **/
static inline void
interpolate_v(const PointXY *pt0,
              const PointXY *pt1,
              const ControlPoint *uv,
              gdouble t,
              PointXY *v)
{
    gdouble s = 1.0 - t;
    gdouble s2 = s*s;
    gdouble t2 = t*t;
    gdouble std = 2.0*s*t;

    v->x = 3.0*(-s2*pt0->x + (s2 - std)*uv->ux + (std - t2)*uv->vx + t2*pt1->x);
    v->y = 3.0*(-s2*pt0->y + (s2 - std)*uv->uy + (std - t2)*uv->vy + t2*pt1->y);
}

static inline void
interpolate_straight_line(const gdouble *xyp, const gdouble *xyn,
                          ControlPoint *uv)
{
    uv->ux = (2.0*xyp[0] + xyn[0])/3.0;
    uv->uy = (2.0*xyp[1] + xyn[1])/3.0;
    uv->vx = (xyp[0] + 2.0*xyn[0])/3.0;
    uv->vy = (xyp[1] + 2.0*xyn[1])/3.0;
}

/* Interpolate the next control point u. */
static inline void
interpolate_cu_next(const gdouble *xyp, const PointXY *cp, const PointXY *cn,
                    gdouble kq,
                    ControlPoint *uv)
{
    uv->ux = xyp[0] + kq*(cn->x - cp->x);
    uv->uy = xyp[1] + kq*(cn->y - cp->y);
}

/* Interpolate the previous control point v. */
static inline void
interpolate_cv_prev(const gdouble *xyp, const PointXY *cp, const PointXY *cn,
                    gdouble kq,
                    ControlPoint *uv)
{
    uv->vx = xyp[0] + kq*(cp->x - cn->x);
    uv->vy = xyp[1] + kq*(cp->y - cn->y);
}

/**
 * calculate_control_points:
 * @n: The number of segments.
 * @xy: Array of points coordinates stored as x0, y0, x1, y1, ..., xn, yn.
 * @slackness: Curve slackness (tightening factor).
 * @closed: %TRUE to closed curves, %FALSE for curves with open ends.
 * @uv: Array to store control point coordinates from uv0 to uv{n-1} (for
 *      non-closed) or uv{n} (for closed).
 *
 * Calculates spline control points from points and tensions.
 **/
static void
calculate_control_points(gint n,
                         const gdouble *xy,
                         gdouble slackness,
                         gboolean closed,
                         ControlPoint *uv)
{
    const gdouble *xyp, *xyn;
    PointXY cp, cn;
    gdouble lenp, lenn, q;
    gint i, to;

    g_return_if_fail(n >= 1);
    g_return_if_fail(xy);
    g_return_if_fail(slackness >= 0.0 && slackness <= 1.0);
    if (!uv)
        return;

    /* Straight lines.  There are other cases when straight lines can occur,
     * but the cost of detection probably overweights the savings. */
    if (n == 1 || slackness == 0.0) {
        xyn = xy;
        to = (closed ? n+1 : n);
        for (i = 0; i < to; i++) {
            xyp = xyn;
            xyn = (i == n) ? xy : xyn+2;
            interpolate_straight_line(xyp, xyn, uv + i);
        }
        return;
    }

    to = (closed ? n+2 : n);
    cn.x = (xy[0] + xy[2])/2.0;
    cn.y = (xy[1] + xy[3])/2.0;
    lenn = hypot(xy[0] - xy[2], xy[1] - xy[3]);
    xyn = xy+2;

    /* Inner u and v.  For closed curves it means all u and v. */
    for (i = 1; i < to; i++) {
        xyp = xyn;
        xyn = (i == n) ? xy : xyn+2;
        cp = cn;
        cn.x = (xyp[0] + xyn[0])/2.0;
        cn.y = (xyp[1] + xyn[1])/2.0;

        lenp = lenn;
        lenn = hypot(xyp[0] - xyn[0], xyp[1] - xyn[1]);

        if (lenp + lenn == 0.0)
            q = 0.5;
        else
            q = lenn/(lenp + lenn);

        interpolate_cv_prev(xyp, &cp, &cn, slackness*(1.0 - q), uv + (i-1));
        interpolate_cu_next(xyp, &cp, &cn, slackness*q, uv + (i == n+1 ? 0 : i));
    }
    if (closed)
        return;

    /* First u */
    uv[0].ux = ((2.0 - slackness)*xy[0] + slackness*uv[0].vx)/2.0;
    uv[0].uy = ((2.0 - slackness)*xy[1] + slackness*uv[0].vy)/2.0;
    /* Last v */
    uv[n-1].vx = ((2.0 - slackness)*xy[2*n + 0] + slackness*uv[n-1].ux)/2.0;
    uv[n-1].vy = ((2.0 - slackness)*xy[2*n + 1] + slackness*uv[n-1].uy)/2.0;
}

static void
sample_curve_recurse(GwySplineSampleParams *cparam,
                     const GwySplineSampleItem *c0,
                     const GwySplineSampleItem *c1)
{
    gdouble q, t, eps;
    PointXY z, v;
    GwySplineSampleItem cc;

    z.x = (c0->z.x + c1->z.x)/2.0;
    z.y = (c0->z.y + c1->z.y)/2.0;
    q = division_time(c0->vl, c1->vl, 0.5);
    t = cc.t = c0->t*(1.0 - q) + c1->t*q;
    v.x = c0->v.x*(1.0 - q) + c1->v.x*q;
    v.y = c0->v.y*(1.0 - q) + c1->v.y*q;
    interpolate_z(cparam->pt0, cparam->pt1, cparam->uv, t, &cc.z);
    interpolate_v(cparam->pt0, cparam->pt1, cparam->uv, t, &cc.v);
    cc.vl = hypot(cc.v.x, cc.v.y);
    eps = hypot(cc.v.x - v.x, cc.v.y - v.y);
    if (eps)
        eps /= (c0->vl + c1->vl)/2.0;

    if (cparam->depth
        && hypot(cc.z.x - z.x, cc.z.y - z.y) <= cparam->max_dev
        && eps <= cparam->max_vrdev) {
        switch (cparam->otype) {
            case CURVE_RECURSE_OUTPUT_X_Y:
            g_array_append_val(cparam->points, c1->z);
            break;

            case CURVE_RECURSE_OUTPUT_T_L:
            {
                PointXY tl;
                tl.x = c1->t;
                tl.y = (hypot(c0->z.x - cc.z.x, c0->z.y - cc.z.y)
                        + hypot(cc.z.x - c1->z.x, cc.z.y - c1->z.y));
                g_array_append_val(cparam->points, tl);
            }
            break;
        }
        return;
    }

    cparam->depth++;
    sample_curve_recurse(cparam, c0, &cc);
    sample_curve_recurse(cparam, &cc, c1);
    cparam->depth--;
}

static PointXY*
sample_curve(const GwySpline *spline,
             gdouble max_dev,
             gdouble max_vrdev,
             GwySplineRecurseOutputType otype,
             guint *nsamples)
{
    GwySplineSampleParams cparam;
    const PointXY *pt, *ptm;
    PointXY *xpt;
    GArray *points;
    guint start, i, j, nseg;

    if (!spline->points->len) {
        *nsamples = 0;
        return NULL;
    }

    if (spline->points->len == 1) {
        xpt = g_new(PointXY, 1);
        xpt->x = g_array_index(spline->points, gdouble, 0);
        xpt->y = g_array_index(spline->points, gdouble, 1);
        *nsamples = 1;
        return xpt;
    }

    nseg = spline->points->len - (spline->closed ? 0 : 1);
    g_array_set_size(spline->control_points, nseg);
    calculate_control_points(spline->points->len - 1,
                             (const gdouble*)spline->points->data,
                             spline->slackness, spline->closed,
                             &g_array_index(spline->control_points,
                                            ControlPoint, 0));

    points = g_array_new(FALSE, FALSE, sizeof(PointXY));

    pt = &g_array_index(spline->points, PointXY, 0);

    cparam.max_dev = max_dev;
    cparam.max_vrdev = max_vrdev;
    cparam.otype = otype;
    cparam.points = points;

    switch (cparam.otype) {
        case CURVE_RECURSE_OUTPUT_X_Y:
        g_array_append_vals(cparam.points, pt, 1);
        break;

        case CURVE_RECURSE_OUTPUT_T_L:
        {
            PointXY zero = { 0.0, 0.0 };
            g_array_append_val(cparam.points, zero);
        }
        break;
    }

    for (i = 1; i <= nseg; i++) {
        GwySplineSampleItem c0, c1;
        const ControlPoint *uv;

        start = points->len;
        ptm = pt;
        if (i == spline->points->len)
            pt = &g_array_index(spline->points, PointXY, 0);
        else
            pt = &g_array_index(spline->points, PointXY, i);
        uv = &g_array_index(spline->control_points, ControlPoint, i - 1);

        c0.t = 0.0;
        c0.z = *ptm;
        c0.v.x = 3.0*(uv->ux - ptm->x);
        c0.v.y = 3.0*(uv->uy - ptm->y);
        c0.vl = hypot(c0.v.x, c0.v.y);

        c1.t = 1.0;
        c1.z = *pt;
        c1.v.x = 3.0*(pt->x - uv->vx);
        c1.v.y = 3.0*(pt->y - uv->vy);
        c1.vl = hypot(c1.v.x, c1.v.y);

        cparam.pt0 = ptm;
        cparam.pt1 = pt;
        cparam.uv = uv;
        cparam.depth = 0;

        sample_curve_recurse(&cparam, &c0, &c1);

        if (cparam.otype == CURVE_RECURSE_OUTPUT_T_L) {
            for (j = start; j < points->len; j++) {
                xpt = &g_array_index(points, PointXY, j);
                xpt->x += i - 1.0;
                xpt->y += g_array_index(points, PointXY, j-1).y;
            }
        }
    }

    *nsamples = points->len;
    xpt = (PointXY*)g_array_free(points, FALSE);

    return xpt;
}

/* FIXME: We can also sample velocities (derivatives).  The public API should
 * probably always offer them because they permit constructing normals to the
 * curve easily. */
static void
sample_curve_uniformly(GwySpline *spline,
                       guint nsamples,
                       PointXY *coords,
                       PointXY *velocities)
{
    guint i, j, k, nseg, npts;
    gdouble pos, t, q, v0l, v1l, t0, t1, l0, l1;
    gboolean closed = spline->closed;
    const PointXY *pt0, *pt1;
    ControlPoint *uv;
    PointXY v0, v1;
    PointXY *p;

    /* Handle miscellaneous degenerate cases.  We could also handle npts == 2
     * directly but this one should be fine for sample_curve() so let it deal
     * with the straight line. */
    npts = spline->points->len;
    if (!nsamples)
        return;

    g_return_if_fail(npts);
    if (npts == 1) {
        PointXY singlept = g_array_index(spline->points, PointXY, 0);
        PointXY zero = { 0.0, 0.0 };
        for (i = 0; i < nsamples; i++) {
            if (coords)
                coords[i] = singlept;
            if (velocities)
                velocities[i] = zero;
        }
        return;
    }

    p = sample_curve(spline, G_MAXDOUBLE, 0.01,
                     CURVE_RECURSE_OUTPUT_T_L, &nseg);

    spline->length = p[nseg-1].y;

    /* XXX XXX XXX: Most of the code above does not depend on sampling and
     * should be cached with the natural_sampling_valid flag (storing @p
     * in spline->natural_points).
     * The code below depends on nsamples and can only be cached with
     * fixed_sampling_valid flag. */

    j = 1;
    for (i = 0; i < nsamples; i++) {
        if (closed)
            pos = i*spline->length/nsamples;
        else if (G_LIKELY(nsamples > 1))
            pos = i*spline->length/(nsamples - 1.0);
        else
            pos = 0.5*spline->length;

        while (p[j].y < pos)
            j++;

        g_assert(j < nseg);

        k = (guint)floor(p[j].x);
        if (k == npts)
            k--;

        t0 = p[j-1].x;
        l0 = p[j-1].y;
        t1 = p[j].x;
        l1 = p[j].y;

        pt0 = &g_array_index(spline->points, PointXY, k);
        pt1 = &g_array_index(spline->points, PointXY, (k+1) % npts);
        uv = &g_array_index(spline->control_points, ControlPoint, k);
        interpolate_v(pt0, pt1, uv, t0, &v0);
        v0l = hypot(v0.x, v0.y);
        interpolate_v(pt0, pt1, uv, t1, &v1);
        v1l = hypot(v1.x, v1.y);

        q = (l0 == l1) ? 0.5 : (pos - l0)/(l1 - l0);
        q = division_time(v0l, v1l, q);
        t = q*t1 + (1.0 - q)*t0;

        if ((guint)floor(t) != k) {
            k = (guint)floor(t);
            if (k == npts)
                k--;

            pt0 = &g_array_index(spline->points, PointXY, k);
            pt1 = &g_array_index(spline->points, PointXY, (k+1) % npts);
            uv = &g_array_index(spline->control_points, ControlPoint, k);
        }
        if (coords)
            interpolate_z(pt0, pt1, uv, t - k, coords + i);
        if (velocities)
            interpolate_v(pt0, pt1, uv, t - k, velocities + i);
    }

    g_free(p);
}

/************************** Documentation ****************************/

/**
 * SECTION:spline
 * @title: GwySpline
 * @short_description: Sampling curves in plane
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
