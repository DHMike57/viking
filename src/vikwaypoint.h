/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _VIKING_WAYPOINT_H
#define _VIKING_WAYPOINT_H

#include "vikcoord.h"

#include <gdk-pixbuf/gdk-pixdata.h>

G_BEGIN_DECLS

typedef enum {
  WP_IMAGE_DIRECTION_REF_TRUE=0,
  WP_IMAGE_DIRECTION_REF_MAGNETIC=1,
} VikWaypointImageDirectionRef;

#define VIK_WAYPOINT(x) ((VikWaypoint *)(x))

typedef struct _VikWaypoint VikWaypoint;

struct _VikWaypoint {
  VikCoord coord;
  gboolean visible;
  gdouble timestamp;         /* NAN if data unavailable */
  gdouble altitude;
  gdouble course;            /* NAN if data unavailable */
  gdouble speed;             /* NAN if data unavailable */
  gdouble magvar;            /* NAN if data unavailable */
  gdouble geoidheight;       /* NAN if data unavailable */
  gboolean hide_name;
  gchar *name;
  gchar *comment;
  gchar *description;
  gchar *source;
  gchar *url;
  gchar *url_name;
  gchar *type;
  guint fix_mode;            /* VIK_GPS_MODE_NOT_SEEN if data unavailable */
  guint nsats;               /* number of satellites used. 0 if data unavailable */
  gdouble hdop;              /* NAN if data unavailable */
  gdouble vdop;              /* NAN if data unavailable */
  gdouble pdop;              /* NAN if data unavailable */
  gdouble ageofdgpsdata;     /* NAN if data unavailable */
  guint dgpsid;              /* 0 .. 1023 */
  gchar *image;
  // NB Only really applicable if geotagging(exif info) is being used
  gdouble image_direction;   /* NAN if data unavailable */
  VikWaypointImageDirectionRef image_direction_ref;
  /* a rather misleading, ugly hack needed for trwlayer's click image.
   * these are the height at which the thumbnail is being drawn, not the 
   * dimensions of the original image. */
  guint8 image_width;
  guint8 image_height;
  gchar *symbol;
  gchar *extensions;         // GPX 1.1
  // Only for GUI display
  GdkPixbuf *symbol_pixbuf;
};

VikWaypoint *vik_waypoint_new();
void vik_waypoint_set_name(VikWaypoint *wp, const gchar *name);
void vik_waypoint_set_comment(VikWaypoint *wp, const gchar *comment);
void vik_waypoint_set_description(VikWaypoint *wp, const gchar *description);
void vik_waypoint_set_source(VikWaypoint *wp, const gchar *source);
void vik_waypoint_set_type(VikWaypoint *wp, const gchar *type);
void vik_waypoint_set_url(VikWaypoint *wp, const gchar *url);
void vik_waypoint_set_url_name(VikWaypoint *wp, const gchar *url_name);
void vik_waypoint_set_image(VikWaypoint *wp, const gchar *image);
void vik_waypoint_set_image_direction_info(VikWaypoint *wp, gdouble direction, VikWaypointImageDirectionRef direction_ref);
void vik_waypoint_set_symbol(VikWaypoint *wp, const gchar *symname);
void vik_waypoint_set_extensions(VikWaypoint *wp, const gchar *value);
void vik_waypoint_free(VikWaypoint * wp);
VikWaypoint *vik_waypoint_copy(const VikWaypoint *wp);
void vik_waypoint_set_comment_no_copy(VikWaypoint *wp, gchar *comment);
gboolean vik_waypoint_apply_dem_data ( VikWaypoint *wp, gboolean skip_existing );
void vik_waypoint_marshall ( VikWaypoint *wp, guint8 **data, guint *len);
VikWaypoint *vik_waypoint_unmarshall (const guint8 *data_in, guint datalen);

G_END_DECLS

#endif
