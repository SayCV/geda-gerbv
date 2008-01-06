/*
 * gEDA - GNU Electronic Design Automation
 * This file is a part of gerbv.
 *
 *   Copyright (C) 2000-2003 Stefan Petersen (spe@stacken.kth.se)
 *
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>  /* ceil(), atan2() */

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gtk/gtk.h>

#include "draw-gdk.h"
#include "gerb_error.h"
#include "gerb_image.h"

#undef round
#define round(x) ceil((double)(x))

/*
 * Stack declarations and operations to be used by the simple engine that
 * executes the parsed aperture macros.
 */
typedef struct {
    double *stack;
    int sp;
} macro_stack_t;


static macro_stack_t *
new_stack(unsigned int nuf_push)
{
    const int extra_stack_size = 10;
    macro_stack_t *s;

    s = (macro_stack_t *)g_malloc(sizeof(macro_stack_t));
    if (!s) {
	g_free(s);
	return NULL;
    }
    memset(s, 0, sizeof(macro_stack_t));

    s->stack = (double *)g_malloc(sizeof(double) * (nuf_push + extra_stack_size));
    if (!s->stack) {
	g_free(s->stack);
	return NULL;
    }

    memset(s->stack, 0, sizeof(double) * (nuf_push + extra_stack_size));
    s->sp = 0;

    return s;
} /* new_stack */


static void
free_stack(macro_stack_t *s)
{
    if (s && s->stack)
	g_free(s->stack);

    if (s)
	g_free(s);

    return;
} /* free_stack */


static void
push(macro_stack_t *s, double val)
{
    s->stack[s->sp++] = val;
    return;
} /* push */


static double
pop(macro_stack_t *s)
{
    return s->stack[--s->sp];
} /* pop */


/*
 * If you want to rotate a
 * column vector v by t degrees using matrix M, use
 *
 *   M = {{cos t, -sin t}, {sin t, cos t}} in M*v.
 *
 * From comp.graphics.algorithms Frequently Asked Questions
 *
 * Due reverse defintion of X-axis in GTK you have to negate
 * angels.
 *
 */
static GdkPoint 
rotate_point(GdkPoint point, int angle)
{
    double sint, cost;
    GdkPoint returned;
    
    if (angle == 0)
	return point;

    sint = sin(-(double)angle * M_PI / 180.0);
    cost = cos(-(double)angle * M_PI / 180.0);
    
    returned.x = (int)round(cost * (double)point.x - sint * (double)point.y);
    returned.y = (int)round(sint * (double)point.x + cost * (double)point.y);
    
    return returned;
}


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim1(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		 int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int diameter_idx = 1;
    const int x_offset_idx = 2;
    const int y_offset_idx = 3;
    const gint full_circle = 23360;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    gint dia    = round(fabs(s->stack[diameter_idx] * scale));
    gint real_x = x - dia / 2;
    gint real_y = y - dia / 2;
    GdkColor color;

    gdk_gc_copy(local_gc, gc);

    real_x += (int)(s->stack[x_offset_idx] * (double)scale);
    real_y -= (int)(s->stack[y_offset_idx] * (double)scale);

    /* Exposure */
    if (s->stack[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_gc_set_line_attributes(local_gc, 
			       1, /* outline always 1 pixels */
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    /* 
     * A filled circle 
     */
    gdk_draw_arc(pixmap, local_gc, 1, real_x, real_y, dia, dia, 
		 0, full_circle);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim1 */


/*
 * Doesn't handle explicit x,y yet
 * Questions:
 *  - should start point be included in number of points?
 *  - how thick is the outline?
 */
static void
gerbv_gdk_draw_prim4(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		 int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int nuf_points_idx = 1;
    const int first_x_idx = 2;
    const int first_y_idx = 3;
    const int rotext_idx = 4;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    int nuf_points, point, closed_shape;
    double rotation;
    GdkPoint *points;
    GdkColor color;


    nuf_points = (int)s->stack[nuf_points_idx];
    points = (GdkPoint *)g_malloc(sizeof(GdkPoint) * nuf_points);
    if (!points) {
	g_free(points);
	return;
    }

    /*
     * Closed (ie filled as I interpret it) shape if first and last point
     * are the same.
     */
    closed_shape = 
	(fabs(s->stack[first_x_idx] - s->stack[nuf_points * 2 + first_x_idx]) < 0.0001) &&
	(fabs(s->stack[first_y_idx] - s->stack[nuf_points * 2 + first_y_idx]) < 0.0001);

    rotation = s->stack[nuf_points * 2 + rotext_idx];
    for (point = 0; point < nuf_points; point++) {
	points[point].x = (int)round(scale * s->stack[point * 2 + first_x_idx]);
	points[point].y = -(int)round(scale * s->stack[point * 2 + first_y_idx]);
	if (rotation > 0.1)
	    points[point] = rotate_point(points[point], rotation);
	points[point].x += x;
	points[point].y += y;
    }

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (s->stack[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_gc_set_line_attributes(local_gc, 
			       1, /* outline always 1 pixels */
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);
    gdk_draw_polygon(pixmap, local_gc, closed_shape, points, nuf_points);

    g_free(points);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim4 */


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim5(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		 int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int nuf_vertices_idx = 1;
    const int diameter_idx = 4;
    const int rotation_idx = 5;
    int nuf_vertices, i;
    double vertex, tick, rotation, radius;
    GdkPoint *points;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkColor color;

    if (s->sp != 6)
	return;

    nuf_vertices = (int)s->stack[nuf_vertices_idx];
    points = (GdkPoint *)g_malloc(sizeof(GdkPoint) * nuf_vertices);
    if (!points) {
	g_free(points);
	return;
    }

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (s->stack[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    tick = 2 * M_PI / (double)nuf_vertices;
    rotation = -s->stack[rotation_idx] * M_PI / 180.0;
    radius = s->stack[diameter_idx] / 2.0;
    for (i = 0; i < nuf_vertices; i++) {
	vertex =  tick * (double)i + rotation;
	points[i].x = (int)round(scale * radius * cos(vertex)) + x;
	points[i].y = (int)round(scale * radius * sin(vertex)) + y;
    }

    gdk_draw_polygon(pixmap, local_gc, 1, points, nuf_vertices);

    gdk_gc_unref(local_gc);

    g_free(points);
    return;
} /* gerbv_gdk_draw_prim5 */


/*
 * Doesn't handle and explicit x,y yet
 * Questions:
 *  - is "gap" distance between edges of circles or distance between
 *    center of line of circle?
 */
static void
gerbv_gdk_draw_prim6(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		 int scale, gint x, gint y)
{
    const int outside_dia_idx = 2;
    const int ci_thickness_idx = 3;
    const int gap_idx = 4;
    const int nuf_circles_idx = 5;
    const int ch_thickness_idx = 6;
    const int ch_length_idx = 7;
    const int rotation_idx = 8;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    double real_dia;
    double real_gap;
    int circle;
    GdkPoint crosshair[4];
    int point;

    gdk_gc_copy(local_gc, gc);
    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * s->stack[ci_thickness_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    real_dia = s->stack[outside_dia_idx] -  s->stack[ci_thickness_idx] / 2.0;
    real_gap = s->stack[gap_idx] + s->stack[ci_thickness_idx];

    for (circle = 0; circle != (int)s->stack[nuf_circles_idx];  circle++) {
	/* 
	 * Non filled circle 
	 */
	const gint full_circle = 23360;
	gint dia = (real_dia - real_gap * circle) * scale;
	gdk_draw_arc(pixmap, local_gc, 0, x - dia / 2, y - dia / 2, 
		     dia, dia, 0, full_circle);
			  
    }

    /*
     * Cross Hair 
     */
    memset(crosshair, 0, sizeof(GdkPoint) * 4);
    crosshair[0].x = (int)((s->stack[ch_length_idx] / 2.0) * scale);
    /*crosshair[0].y = 0;*/
    crosshair[1].x = -crosshair[0].x;
    /*crosshair[1].y = 0;*/
    /*crosshair[2].x = 0;*/
    crosshair[2].y = crosshair[0].x;
    /*crosshair[3].x = 0;*/
    crosshair[3].y = -crosshair[0].x;

    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * s->stack[ch_thickness_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    for (point = 0; point < 4; point++) {
	crosshair[point] = rotate_point(crosshair[point], 
					s->stack[rotation_idx]);
	crosshair[point].x += x;
	crosshair[point].y += y;
    }
    gdk_draw_line(pixmap, local_gc, 
		  crosshair[0].x, crosshair[0].y, 
		  crosshair[1].x, crosshair[1].y);
    gdk_draw_line(pixmap, local_gc, 
		  crosshair[2].x, crosshair[2].y, 
		  crosshair[3].x, crosshair[3].y);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim6 */


static void
gerbv_gdk_draw_prim7(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		 int scale, gint x, gint y)
{
    const int outside_dia_idx = 2;
    const int inside_dia_idx = 3;
    const int ch_thickness_idx = 4;
    const int rotation_idx = 5;
    const gint full_circle = 23360;
    GdkGCValues gc_val;
    int diameter, i;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkPoint point[4];
    double ci_thickness = (s->stack[outside_dia_idx] - 
			s->stack[inside_dia_idx]) / 2.0;

    gdk_gc_copy(local_gc, gc);
    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * ci_thickness),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    /* 
     * Non filled circle 
     */
    diameter = (s->stack[inside_dia_idx] + ci_thickness) * scale;
    gdk_draw_arc(pixmap, local_gc, 0, x - diameter / 2, y - diameter / 2, 
		 diameter, diameter, 0, full_circle);

    /*
     * Cross hair
     */ 
    /* Calculate the end points of the crosshair */    
    /* GDK doesn't always remove all of the circle (round of error probably)
       I extend the crosshair line with 2 (one pixel in each end) to make 
       sure all of the circle is removed with the crosshair */
    for (i = 0; i < 4; i++) {
	point[i].x = round((s->stack[outside_dia_idx] / 2.0) * scale) + 2;
	point[i].y = 0;
	point[i] = rotate_point(point[i], s->stack[rotation_idx] + 90 * i);
	point[i].x += x;
	point[i].y += y;
    }

    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * s->stack[ch_thickness_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    /* The cross hair should "cut out" parts of the circle, hence inverse */
    gdk_gc_get_values(local_gc, &gc_val);
    if (gc_val.foreground.pixel == 1)
	gc_val.foreground.pixel = 0;
    else
	gc_val.foreground.pixel = 1;
    gdk_gc_set_foreground(local_gc, &(gc_val.foreground));

    /* Draw the actual cross */
    gdk_draw_line(pixmap, local_gc, 
		  point[0].x, point[0].y, point[2].x, point[2].y);
    gdk_draw_line(pixmap, local_gc,
		  point[1].x, point[1].y, point[3].x, point[3].y);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim7 */


/*
 * Doesn't handle and explicit x,y yet
 */
static void
gerbv_gdk_draw_prim20(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		  int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int linewidth_idx = 1;
    const int start_x_idx = 2;
    const int start_y_idx = 3;
    const int end_x_idx = 4;
    const int end_y_idx = 5;
    const int rotation_idx = 6;
    const int nuf_points = 2;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkPoint points[nuf_points];
    GdkColor color;
    int i;

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (s->stack[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * s->stack[linewidth_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    points[0].x = (s->stack[start_x_idx] * scale);
    points[0].y = (s->stack[start_y_idx] * scale);
    points[1].x = (s->stack[end_x_idx] * scale);
    points[1].y = (s->stack[end_y_idx] * scale);

    for (i = 0; i < nuf_points; i++) {
	points[i] = rotate_point(points[i], s->stack[rotation_idx]);
	points[i].x = x + points[i].x;
	points[i].y = y - points[i].y;
    }

    gdk_draw_line(pixmap, local_gc, 
		  points[0].x, points[0].y, 
		  points[1].x, points[1].y);
    
    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim20 */


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim21(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		  int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int width_idx = 1;
    const int height_idx = 2;
    const int rotation_idx = 5;
    const int nuf_points = 4;
    GdkPoint points[nuf_points];
    GdkColor color;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    int half_width, half_height;
    int i;

    half_width = (int)round(s->stack[width_idx] * scale / 2.0);
    half_height =(int)round(s->stack[height_idx] * scale / 2.0);

    points[0].x = half_width;
    points[0].y = half_height;

    points[1].x = half_width;
    points[1].y = -half_height;

    points[2].x = -half_width;
    points[2].y = -half_height;

    points[3].x = -half_width;
    points[3].y = half_height;

    for (i = 0; i < nuf_points; i++) {
	points[i] = rotate_point(points[i], s->stack[rotation_idx]);
	points[i].x += x;
	points[i].y += y;
    }

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (s->stack[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_draw_polygon(pixmap, local_gc, 1, points, nuf_points);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim21 */


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim22(GdkPixmap *pixmap, GdkGC *gc, macro_stack_t *s, 
		  int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int width_idx = 1;
    const int height_idx = 2;
    const int x_lower_left_idx = 3;
    const int y_lower_left_idx = 4;
    const int rotation_idx = 5;
    const int nuf_points = 4;
    GdkPoint points[nuf_points];
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkColor color;
    int i;

    points[0].x = (int)round(s->stack[x_lower_left_idx] * scale);
    points[0].y = (int)round(s->stack[y_lower_left_idx] * scale);

    points[1].x = (int)round((s->stack[x_lower_left_idx] + s->stack[width_idx])
			     * scale);
    points[1].y = (int)round(s->stack[y_lower_left_idx] * scale);

    points[2].x = (int)round((s->stack[x_lower_left_idx]  + s->stack[width_idx])
			     * scale);
    points[2].y = (int)round((s->stack[y_lower_left_idx]  - s->stack[height_idx])
			     * scale);

    points[3].x = (int)round(s->stack[x_lower_left_idx] * scale);
    points[3].y = (int)round((s->stack[y_lower_left_idx] - s->stack[height_idx])
			     * scale);

    for (i = 0; i < nuf_points; i++) {
	points[i] = rotate_point(points[i], s->stack[rotation_idx]);
	points[i].x += x;
	points[i].y += y;
    }
    
    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (s->stack[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_draw_polygon(pixmap, local_gc, 1, points, nuf_points);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim22 */


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim1_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		 int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int diameter_idx = 1;
    const int x_offset_idx = 2;
    const int y_offset_idx = 3;
    const gint full_circle = 23360;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    gint dia    = round(fabs(parameters[diameter_idx] * scale));
    gint real_x = x - dia / 2;
    gint real_y = y - dia / 2;
    GdkColor color;

    gdk_gc_copy(local_gc, gc);

    real_x += (int)(parameters[x_offset_idx] * (double)scale);
    real_y -= (int)(parameters[y_offset_idx] * (double)scale);

    /* Exposure */
    if (parameters[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_gc_set_line_attributes(local_gc, 
			       1, /* outline always 1 pixels */
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    /* 
     * A filled circle 
     */
    gdk_draw_arc(pixmap, local_gc, 1, real_x, real_y, dia, dia, 
		 0, full_circle);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim1 */


/*
 * Doesn't handle explicit x,y yet
 * Questions:
 *  - should start point be included in number of points?
 *  - how thick is the outline?
 */
static void
gerbv_gdk_draw_prim4_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		 int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int nuf_points_idx = 1;
    const int first_x_idx = 2;
    const int first_y_idx = 3;
    const int rotext_idx = 4;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    int nuf_points, point, closed_shape;
    double rotation;
    GdkPoint *points;
    GdkColor color;


    nuf_points = (int)parameters[nuf_points_idx];
    points = (GdkPoint *)g_malloc(sizeof(GdkPoint) * nuf_points);
    if (!points) {
	g_free(points);
	return;
    }

    /*
     * Closed (ie filled as I interpret it) shape if first and last point
     * are the same.
     */
    closed_shape = 
	(fabs(parameters[first_x_idx] - parameters[nuf_points * 2 + first_x_idx]) < 0.0001) &&
	(fabs(parameters[first_y_idx] - parameters[nuf_points * 2 + first_y_idx]) < 0.0001);

    rotation = parameters[nuf_points * 2 + rotext_idx];
    for (point = 0; point < nuf_points; point++) {
	points[point].x = (int)round(scale * parameters[point * 2 + first_x_idx]);
	points[point].y = -(int)round(scale * parameters[point * 2 + first_y_idx]);
	if (rotation > 0.1)
	    points[point] = rotate_point(points[point], rotation);
	points[point].x += x;
	points[point].y += y;
    }

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (parameters[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_gc_set_line_attributes(local_gc, 
			       1, /* outline always 1 pixels */
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);
    gdk_draw_polygon(pixmap, local_gc, closed_shape, points, nuf_points);

    g_free(points);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim4 */


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim5_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		 int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int nuf_vertices_idx = 1;
    const int diameter_idx = 4;
    const int rotation_idx = 5;
    int nuf_vertices, i;
    double vertex, tick, rotation, radius;
    GdkPoint *points;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkColor color;

    nuf_vertices = (int)parameters[nuf_vertices_idx];
    points = (GdkPoint *)g_malloc(sizeof(GdkPoint) * nuf_vertices);
    if (!points) {
	g_free(points);
	return;
    }

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (parameters[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    tick = 2 * M_PI / (double)nuf_vertices;
    rotation = -parameters[rotation_idx] * M_PI / 180.0;
    radius = parameters[diameter_idx] / 2.0;
    for (i = 0; i < nuf_vertices; i++) {
	vertex =  tick * (double)i + rotation;
	points[i].x = (int)round(scale * radius * cos(vertex)) + x;
	points[i].y = (int)round(scale * radius * sin(vertex)) + y;
    }

    gdk_draw_polygon(pixmap, local_gc, 1, points, nuf_vertices);

    gdk_gc_unref(local_gc);

    g_free(points);
    return;
} /* gerbv_gdk_draw_prim5 */


/*
 * Doesn't handle and explicit x,y yet
 * Questions:
 *  - is "gap" distance between edges of circles or distance between
 *    center of line of circle?
 */
static void
gerbv_gdk_draw_prim6_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		 int scale, gint x, gint y)
{
    const int outside_dia_idx = 2;
    const int ci_thickness_idx = 3;
    const int gap_idx = 4;
    const int nuf_circles_idx = 5;
    const int ch_thickness_idx = 6;
    const int ch_length_idx = 7;
    const int rotation_idx = 8;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    double real_dia;
    double real_gap;
    int circle;
    GdkPoint crosshair[4];
    int point;

    gdk_gc_copy(local_gc, gc);
    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * parameters[ci_thickness_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    real_dia = parameters[outside_dia_idx] -  parameters[ci_thickness_idx] / 2.0;
    real_gap = parameters[gap_idx] + parameters[ci_thickness_idx];

    for (circle = 0; circle != (int)parameters[nuf_circles_idx];  circle++) {
	/* 
	 * Non filled circle 
	 */
	const gint full_circle = 23360;
	gint dia = (real_dia - real_gap * circle) * scale;
	gdk_draw_arc(pixmap, local_gc, 0, x - dia / 2, y - dia / 2, 
		     dia, dia, 0, full_circle);
			  
    }

    /*
     * Cross Hair 
     */
    memset(crosshair, 0, sizeof(GdkPoint) * 4);
    crosshair[0].x = (int)((parameters[ch_length_idx] / 2.0) * scale);
    /*crosshair[0].y = 0;*/
    crosshair[1].x = -crosshair[0].x;
    /*crosshair[1].y = 0;*/
    /*crosshair[2].x = 0;*/
    crosshair[2].y = crosshair[0].x;
    /*crosshair[3].x = 0;*/
    crosshair[3].y = -crosshair[0].x;

    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * parameters[ch_thickness_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    for (point = 0; point < 4; point++) {
	crosshair[point] = rotate_point(crosshair[point], 
					parameters[rotation_idx]);
	crosshair[point].x += x;
	crosshair[point].y += y;
    }
    gdk_draw_line(pixmap, local_gc, 
		  crosshair[0].x, crosshair[0].y, 
		  crosshair[1].x, crosshair[1].y);
    gdk_draw_line(pixmap, local_gc, 
		  crosshair[2].x, crosshair[2].y, 
		  crosshair[3].x, crosshair[3].y);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim6 */


static void
gerbv_gdk_draw_prim7_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		 int scale, gint x, gint y)
{
    const int outside_dia_idx = 2;
    const int inside_dia_idx = 3;
    const int ch_thickness_idx = 4;
    const int rotation_idx = 5;
    const gint full_circle = 23360;
    GdkGCValues gc_val;
    int diameter, i;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkPoint point[4];
    double ci_thickness = (parameters[outside_dia_idx] - 
			parameters[inside_dia_idx]) / 2.0;

    gdk_gc_copy(local_gc, gc);
    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * ci_thickness),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    /* 
     * Non filled circle 
     */
    diameter = (parameters[inside_dia_idx] + ci_thickness) * scale;
    gdk_draw_arc(pixmap, local_gc, 0, x - diameter / 2, y - diameter / 2, 
		 diameter, diameter, 0, full_circle);

    /*
     * Cross hair
     */ 
    /* Calculate the end points of the crosshair */    
    /* GDK doesn't always remove all of the circle (round of error probably)
       I extend the crosshair line with 2 (one pixel in each end) to make 
       sure all of the circle is removed with the crosshair */
    for (i = 0; i < 4; i++) {
	point[i].x = round((parameters[outside_dia_idx] / 2.0) * scale) + 2;
	point[i].y = 0;
	point[i] = rotate_point(point[i], parameters[rotation_idx] + 90 * i);
	point[i].x += x;
	point[i].y += y;
    }

    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * parameters[ch_thickness_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    /* The cross hair should "cut out" parts of the circle, hence inverse */
    gdk_gc_get_values(local_gc, &gc_val);
    if (gc_val.foreground.pixel == 1)
	gc_val.foreground.pixel = 0;
    else
	gc_val.foreground.pixel = 1;
    gdk_gc_set_foreground(local_gc, &(gc_val.foreground));

    /* Draw the actual cross */
    gdk_draw_line(pixmap, local_gc, 
		  point[0].x, point[0].y, point[2].x, point[2].y);
    gdk_draw_line(pixmap, local_gc,
		  point[1].x, point[1].y, point[3].x, point[3].y);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim7 */


/*
 * Doesn't handle and explicit x,y yet
 */
static void
gerbv_gdk_draw_prim20_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		  int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int linewidth_idx = 1;
    const int start_x_idx = 2;
    const int start_y_idx = 3;
    const int end_x_idx = 4;
    const int end_y_idx = 5;
    const int rotation_idx = 6;
    const int nuf_points = 2;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkPoint points[nuf_points];
    GdkColor color;
    int i;

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (parameters[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_gc_set_line_attributes(local_gc, 
			       (int)round(scale * parameters[linewidth_idx]),
			       GDK_LINE_SOLID, 
			       GDK_CAP_BUTT, 
			       GDK_JOIN_MITER);

    points[0].x = (parameters[start_x_idx] * scale);
    points[0].y = (parameters[start_y_idx] * scale);
    points[1].x = (parameters[end_x_idx] * scale);
    points[1].y = (parameters[end_y_idx] * scale);

    for (i = 0; i < nuf_points; i++) {
	points[i] = rotate_point(points[i], parameters[rotation_idx]);
	points[i].x = x + points[i].x;
	points[i].y = y - points[i].y;
    }

    gdk_draw_line(pixmap, local_gc, 
		  points[0].x, points[0].y, 
		  points[1].x, points[1].y);
    
    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim20 */


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim21_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		  int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int width_idx = 1;
    const int height_idx = 2;
    const int rotation_idx = 5;
    const int nuf_points = 4;
    GdkPoint points[nuf_points];
    GdkColor color;
    GdkGC *local_gc = gdk_gc_new(pixmap);
    int half_width, half_height;
    int i;

    half_width = (int)round(parameters[width_idx] * scale / 2.0);
    half_height =(int)round(parameters[height_idx] * scale / 2.0);

    points[0].x = half_width;
    points[0].y = half_height;

    points[1].x = half_width;
    points[1].y = -half_height;

    points[2].x = -half_width;
    points[2].y = -half_height;

    points[3].x = -half_width;
    points[3].y = half_height;

    for (i = 0; i < nuf_points; i++) {
	points[i] = rotate_point(points[i], parameters[rotation_idx]);
	points[i].x += x;
	points[i].y += y;
    }

    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (parameters[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_draw_polygon(pixmap, local_gc, 1, points, nuf_points);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim21 */


/*
 * Doesn't handle explicit x,y yet
 */
static void
gerbv_gdk_draw_prim22_new(GdkPixmap *pixmap, GdkGC *gc, gdouble *parameters, 
		  int scale, gint x, gint y)
{
    const int exposure_idx = 0;
    const int width_idx = 1;
    const int height_idx = 2;
    const int x_lower_left_idx = 3;
    const int y_lower_left_idx = 4;
    const int rotation_idx = 5;
    const int nuf_points = 4;
    GdkPoint points[nuf_points];
    GdkGC *local_gc = gdk_gc_new(pixmap);
    GdkColor color;
    int i;

    points[0].x = (int)round(parameters[x_lower_left_idx] * scale);
    points[0].y = (int)round(parameters[y_lower_left_idx] * scale);

    points[1].x = (int)round((parameters[x_lower_left_idx] + parameters[width_idx])
			     * scale);
    points[1].y = (int)round(parameters[y_lower_left_idx] * scale);

    points[2].x = (int)round((parameters[x_lower_left_idx]  + parameters[width_idx])
			     * scale);
    points[2].y = (int)round((parameters[y_lower_left_idx]  - parameters[height_idx])
			     * scale);

    points[3].x = (int)round(parameters[x_lower_left_idx] * scale);
    points[3].y = (int)round((parameters[y_lower_left_idx] - parameters[height_idx])
			     * scale);

    for (i = 0; i < nuf_points; i++) {
	points[i] = rotate_point(points[i], parameters[rotation_idx]);
	points[i].x += x;
	points[i].y += y;
    }
    
    gdk_gc_copy(local_gc, gc);

    /* Exposure */
    if (parameters[exposure_idx] == 0.0) {
	color.pixel = 0;
	gdk_gc_set_foreground(local_gc, &color);
    }

    gdk_draw_polygon(pixmap, local_gc, 1, points, nuf_points);

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_prim22 */


int
gerbv_gdk_draw_amacro(GdkPixmap *pixmap, GdkGC *gc,
		  instruction_t *program, unsigned int nuf_push,
		  double *parameters, int scale, gint x, gint y)
{
    macro_stack_t *s = new_stack(nuf_push);
    instruction_t *ip;
    int handled = 1;
    double *lp; /* Local copy of parameters */
    double tmp[2] = {0.0, 0.0};

    lp = (double *)malloc(sizeof(double) * APERTURE_PARAMETERS_MAX);
    if (lp == NULL)
	return -1;
    memcpy(lp, parameters, sizeof(double) * APERTURE_PARAMETERS_MAX);
    
    for(ip = program; ip != NULL; ip = ip->next) {
	switch(ip->opcode) {
	case NOP:
	    break;
	case PUSH :
	    push(s, ip->data.fval);
	    break;
        case PPUSH :
	    push(s, lp[ip->data.ival - 1]);
	    break;
	case PPOP:
	    lp[ip->data.ival - 1] = pop(s);
	    break;
	case ADD :
	    push(s, pop(s) + pop(s));
	    break;
	case SUB :
	    tmp[0] = pop(s);
	    tmp[1] = pop(s);
	    push(s, tmp[1] - tmp[0]);
	    break;
	case MUL :
	    push(s, pop(s) * pop(s));
	    break;
	case DIV :
	    tmp[0] = pop(s);
	    tmp[1] = pop(s);
	    push(s, tmp[1] / tmp[0]);
	    break;
	case PRIM :
	    /* 
	     * This handles the exposure thing in the aperture macro
	     * The exposure is always the first element on stack independent
	     * of aperture macro.
	     */
	    switch(ip->data.ival) {
	    case 1:
		gerbv_gdk_draw_prim1(pixmap, gc, s, scale, x, y);
		break;
	    case 4 :
		gerbv_gdk_draw_prim4(pixmap, gc, s, scale, x, y);
		break;
	    case 5 :
		gerbv_gdk_draw_prim5(pixmap, gc, s, scale, x, y);
		break;
	    case 6 :
		gerbv_gdk_draw_prim6(pixmap, gc, s, scale, x, y);
		break;
	    case 7 :
		gerbv_gdk_draw_prim7(pixmap, gc, s, scale, x, y);
		break;
	    case 2  :
	    case 20 :
		gerbv_gdk_draw_prim20(pixmap, gc, s, scale, x, y);
		break;
	    case 21 :
		gerbv_gdk_draw_prim21(pixmap, gc, s, scale, x, y);
		break;
	    case 22 :
		gerbv_gdk_draw_prim22(pixmap, gc, s, scale, x, y);
		break;
	    default :
		handled = 0;
	    }
	    /* 
	     * Here we reset the stack pointer. It's not general correct
	     * correct to do this, but since I know how the compiler works
	     * I can do this. The correct way to do this should be to 
	     * subtract number of used elements in each primitive operation.
	     */
	    s->sp = 0;
	    break;
	default :
	    break;
	}
    }
    free_stack(s);

    return handled;
} /* gerbv_gdk_draw_amacro */

/*
 * Draws a circle _centered_ at x,y with diameter dia
 */
static void 
gerbv_gdk_draw_circle(GdkPixmap *pixmap, GdkGC *gc, 
		  gint filled, gint x, gint y, gint dia)
{
    static const gint full_circle = 23360;
    gint real_x = x - dia / 2;
    gint real_y = y - dia / 2;
    
    gdk_draw_arc(pixmap, gc, filled, real_x, real_y, dia, dia, 0, full_circle);
    
    return;
} /* gerbv_gdk_draw_circle */


/*
 * Draws a rectangle _centered_ at x,y with sides x_side, y_side
 */
static void 
gerbv_gdk_draw_rectangle(GdkPixmap *pixmap, GdkGC *gc, 
		     gint filled, gint x, gint y, gint x_side, gint y_side)
{
    
    gint real_x = x - x_side / 2;
    gint real_y = y - y_side / 2;
    
    gdk_draw_rectangle(pixmap, gc, filled, real_x, real_y, x_side, y_side);
    
    return;
} /* gerbv_gdk_draw_rectangle */


/*
 * Draws an oval _centered_ at x,y with x axis x_axis and y axis y_axis
 */ 
static void
gerbv_gdk_draw_oval(GdkPixmap *pixmap, GdkGC *gc, 
		gint filled, gint x, gint y, gint x_axis, gint y_axis)
{
    gint delta = 0;
    GdkGC *local_gc = gdk_gc_new(pixmap);

    gdk_gc_copy(local_gc, gc);

    if (x_axis > y_axis) {
	/* Draw in x axis */
	delta = x_axis / 2 - y_axis / 2;
	gdk_gc_set_line_attributes(local_gc, y_axis, 
				   GDK_LINE_SOLID, 
				   GDK_CAP_ROUND, 
				   GDK_JOIN_MITER);
	gdk_draw_line(pixmap, local_gc, x - delta, y, x + delta, y);
    } else {
	/* Draw in y axis */
	delta = y_axis / 2 - x_axis / 2;
	gdk_gc_set_line_attributes(local_gc, x_axis, 
				   GDK_LINE_SOLID, 
				   GDK_CAP_ROUND, 
				   GDK_JOIN_MITER);
	gdk_draw_line(pixmap, local_gc, x, y - delta, x, y + delta);
    }

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_gdk_draw_oval */


/*
 * Draws an arc 
 * Draws an arc _centered_ at x,y
 * direction:  0 counterclockwise, 1 clockwise
 */
static void
gerbv_gdk_draw_arc(GdkPixmap *pixmap, GdkGC *gc,
	       int x, int y,
	       int width, int height,
	       double angle1, double angle2)
{
    gint real_x = x - width / 2;
    gint real_y = y - height / 2;

    gdk_draw_arc(pixmap, gc, FALSE, real_x, real_y, width, height, 
		 (gint)angle1 * 64.0, (gint)(angle2 - angle1) * 64.0);
    
    return;
} /* gerbv_gdk_draw_arc */


/*
 * Convert a gerber image to a GDK clip mask to be used when creating pixmap
 */
int
image2pixmap(GdkPixmap **pixmap, gerb_image_t *image, 
	     int scale, double trans_x, double trans_y,
	     enum polarity_t polarity)
{
    GdkGC *gc = gdk_gc_new(*pixmap);
    GdkGC *pgc = gdk_gc_new(*pixmap);
    GdkGCValues gc_values;
    struct gerb_net *net;
    gint x1, y1, x2, y2;
    int p1, p2, p3;
    int cir_width = 0, cir_height = 0;
    int cp_x = 0, cp_y = 0;
    GdkPoint *points = NULL;
    int curr_point_idx = 0;
    int in_parea_fill = 0;
    double unit_scale;
    GdkColor transparent, opaque;


    if (image == NULL || image->netlist == NULL) {
	/*
	 * Destroy GCs before exiting
	 */
	gdk_gc_unref(gc);
	gdk_gc_unref(pgc);
	
	return 0;
    }
    
    /* Set up the two "colors" we have */
    opaque.pixel = 0; /* opaque will not let color through */
    transparent.pixel = 1; /* transparent will let color through */ 

    /*
     * Clear clipmask and set draw color depending image on image polarity
     */
    if (polarity == NEGATIVE) {
	gdk_gc_set_foreground(gc, &transparent);
	gdk_draw_rectangle(*pixmap, gc, TRUE, 0, 0, -1, -1);
	gdk_gc_set_foreground(gc, &opaque);
    } else {
	gdk_gc_set_foreground(gc, &opaque);
	gdk_draw_rectangle(*pixmap, gc, TRUE, 0, 0, -1, -1);
	gdk_gc_set_foreground(gc, &transparent);
    }

    for (net = image->netlist->next ; net != NULL; net = net->next) {
      int repeat_X=1, repeat_Y=1;
      double repeat_dist_X=0.0, repeat_dist_Y=0.0;
      int repeat_i, repeat_j;

	/*
	 * If step_and_repeat (%SR%) used, repeat the drawing;
	 */
	repeat_X = net->layer->stepAndRepeat.X;
	repeat_Y = net->layer->stepAndRepeat.Y;
	repeat_dist_X = net->layer->stepAndRepeat.dist_X;
	repeat_dist_Y = net->layer->stepAndRepeat.dist_Y;
      for(repeat_i = 0; repeat_i < repeat_X; repeat_i++) {
	for(repeat_j = 0; repeat_j < repeat_Y; repeat_j++) {
	  double sr_x = repeat_i * repeat_dist_X;
	  double sr_y = repeat_j * repeat_dist_Y;
	
      unit_scale = scale;

	/*
	 * Scale points with window scaling and translate them
	 */
	x1 = (int)round((image->info->offsetA + net->start_x + sr_x) * unit_scale +
			trans_x);
	y1 = (int)round((-image->info->offsetB - net->start_y - sr_y) * unit_scale +
			trans_y);
	x2 = (int)round((image->info->offsetA + net->stop_x + sr_x) * unit_scale +
			trans_x);
	y2 = (int)round((-image->info->offsetB - net->stop_y - sr_y) * unit_scale +
			trans_y);

	/* 
	 * If circle segment, scale and translate that one too
	 */
	if (net->cirseg) {
	    cir_width = (int)round(net->cirseg->width * unit_scale);
	    cir_height = (int)round(net->cirseg->height * unit_scale);
	    cp_x = (int)round((image->info->offsetA + net->cirseg->cp_x) *
			      unit_scale + trans_x);
	    cp_y = (int)round((image->info->offsetB - net->cirseg->cp_y) *
			      unit_scale + trans_y);
	}

	/*
	 * Set GdkFunction depending on if this (gerber) layer is inverted
	 * and allow for the photoplot being negative.
	 */
	gdk_gc_set_function(gc, GDK_COPY);
	if ((net->layer->polarity == CLEAR) != (polarity == NEGATIVE))
	    gdk_gc_set_foreground(gc, &opaque);
	else
	    gdk_gc_set_foreground(gc, &transparent);

	/*
	 * Polygon Area Fill routines
	 */
	switch (net->interpolation) {
	case PAREA_START :
	    points = (GdkPoint *)g_malloc(sizeof(GdkPoint) *  net->nuf_pcorners);
	    if (points == NULL) {
		GERB_FATAL_ERROR("Malloc failed\n");
	    }
	    memset(points, 0, sizeof(GdkPoint) *  net->nuf_pcorners);
	    curr_point_idx = 0;
	    in_parea_fill = 1;
	    continue;
	case PAREA_END :
	    gdk_gc_copy(pgc, gc); 
	    gdk_gc_set_line_attributes(pgc, 1, 
				       GDK_LINE_SOLID, 
				       GDK_CAP_PROJECTING, 
				       GDK_JOIN_MITER);
	    gdk_draw_polygon(*pixmap, pgc, 1, points, curr_point_idx);
	    g_free(points);
	    points = NULL;
	    in_parea_fill = 0;
	    continue;
	default :
	    break;
	}

	if (in_parea_fill) {
	    points[curr_point_idx].x = x2;
	    points[curr_point_idx].y = y2;
	    curr_point_idx++;
	    continue;
	}

	/*
	 * If aperture state is off we allow use of undefined apertures.
	 * This happens when gerber files starts, but hasn't decided on 
	 * which aperture to use.
	 */
	if (image->aperture[net->aperture] == NULL) {
	    if (net->aperture_state != OFF)
		GERB_MESSAGE("Aperture [%d] is not defined\n", net->aperture);
	    continue;
	}

	if (image->aperture[net->aperture]->unit == MM)
		unit_scale /= 25.4;
	else
		unit_scale = scale;

	switch (net->aperture_state) {
	case ON :
	    p1 = (int)round(image->aperture[net->aperture]->parameter[0] * unit_scale);
	    if (image->aperture[net->aperture]->type == RECTANGLE)
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_SOLID, 
					   GDK_CAP_PROJECTING, 
					   GDK_JOIN_MITER);
	    else
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_SOLID, 
					   GDK_CAP_ROUND, 
					   GDK_JOIN_MITER);

	    switch (net->interpolation) {
	    case LINEARx10 :
	    case LINEARx01 :
	    case LINEARx001 :
		GERB_MESSAGE("Linear != x1\n");
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_ON_OFF_DASH, 
					   GDK_CAP_ROUND, 
					   GDK_JOIN_MITER);
		gdk_draw_line(*pixmap, gc, x1, y1, x2, y2);
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_SOLID,
					   GDK_CAP_ROUND, 
					   GDK_JOIN_MITER);
		break;
	    case LINEARx1 :
		if (image->aperture[net->aperture]->type != RECTANGLE)
			gdk_draw_line(*pixmap, gc, x1, y1, x2, y2);
		else {
			gint dx, dy;
			GdkPoint poly[6];

			dx = (int)round(image->aperture[net->aperture]->parameter[0]
						* unit_scale / 2);
			dy = (int)round(image->aperture[net->aperture]->parameter[1]
						* unit_scale / 2);
			if(x1 > x2) dx = -dx;
			if(y1 > y2) dy = -dy;
			poly[0].x = x1 - dx; poly[0].y = y1 - dy;
			poly[1].x = x1 - dx; poly[1].y = y1 + dy;
			poly[2].x = x2 - dx; poly[2].y = y2 + dy;
			poly[3].x = x2 + dx; poly[3].y = y2 + dy;
			poly[4].x = x2 + dx; poly[4].y = y2 - dy;
			poly[5].x = x1 + dx; poly[5].y = y1 - dy;
			gdk_draw_polygon(*pixmap, gc, 1, poly, 6);
		}
 		break;
	    case CW_CIRCULAR :
	    case CCW_CIRCULAR :
		gerbv_gdk_draw_arc(*pixmap, gc, cp_x, cp_y, cir_width, cir_height, 
			       net->cirseg->angle1, net->cirseg->angle2);
		break;		
	    default :
		break;
	    }
	    break;
	case OFF :
	    break;
	case FLASH :
	    p1 = (int)round(image->aperture[net->aperture]->parameter[0] * unit_scale);
	    p2 = (int)round(image->aperture[net->aperture]->parameter[1] * unit_scale);
	    p3 = (int)round(image->aperture[net->aperture]->parameter[2] * unit_scale);
	    
	    switch (image->aperture[net->aperture]->type) {
	    case CIRCLE :
		gerbv_gdk_draw_circle(*pixmap, gc, TRUE, x2, y2, p1);
		/*
		 * If circle has an inner diameter we must remove
		 * that part of the circle to make a hole in it.
		 * We should actually support square holes too,
		 * but due to laziness I don't.
		 */
		if (p2) {
		    if (p3) GERB_COMPILE_WARNING("Should be a square hole in this aperture.\n");
		    gdk_gc_get_values(gc, &gc_values);
		    if (gc_values.foreground.pixel == opaque.pixel) {
			gdk_gc_set_foreground(gc, &transparent);
			gerbv_gdk_draw_circle(*pixmap, gc, TRUE, x2, y2, p2);
			gdk_gc_set_foreground(gc, &opaque);
		    } else {
			gdk_gc_set_foreground(gc, &opaque);
			gerbv_gdk_draw_circle(*pixmap, gc, TRUE, x2, y2, p2);
			gdk_gc_set_foreground(gc, &transparent);
		    }
		}

		break;
	    case RECTANGLE:
		gerbv_gdk_draw_rectangle(*pixmap, gc, TRUE, x2, y2, p1, p2);
		break;
	    case OVAL :
		gerbv_gdk_draw_oval(*pixmap, gc, TRUE, x2, y2, p1, p2);
		break;
	    case POLYGON :
		GERB_COMPILE_WARNING("Very bad at drawing polygons.\n");
		gerbv_gdk_draw_circle(*pixmap, gc, TRUE, x2, y2, p1);
		break;
	    case MACRO :
		gerbv_gdk_draw_amacro(*pixmap, gc, 
				  image->aperture[net->aperture]->amacro->program,
				  image->aperture[net->aperture]->amacro->nuf_push,
				  image->aperture[net->aperture]->parameter,
				  unit_scale, x2, y2);
		break;
	    case MACRO_CIRCLE :
	    	gerbv_gdk_draw_prim1_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    case MACRO_OUTLINE :
		gerbv_gdk_draw_prim4_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    case MACRO_POLYGON :
		gerbv_gdk_draw_prim5_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    case MACRO_MOIRE :
		gerbv_gdk_draw_prim6_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    case MACRO_THERMAL :
		gerbv_gdk_draw_prim7_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    case MACRO_LINE20  :
		gerbv_gdk_draw_prim20_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    case MACRO_LINE21 :
		gerbv_gdk_draw_prim21_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    case MACRO_LINE22 :
		gerbv_gdk_draw_prim22_new(*pixmap, gc, image->aperture[net->aperture]->parameter, scale, x2, y2);
		break;
	    default :
		GERB_MESSAGE("Unknown aperture type\n");
		return 0;
	    }
	    break;
	default :
	    GERB_MESSAGE("Unknown aperture state\n");
	    return 0;
	}
    }
      }
    }
    /*
     * Destroy GCs before exiting
     */
    gdk_gc_unref(gc);
    gdk_gc_unref(pgc);
    
    return 1;

} /* image2pixmap */