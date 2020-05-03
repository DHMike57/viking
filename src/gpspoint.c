/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 * Copyright (C) 2012-2013, Rob Norris <rw_norris@hotmail.com>
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

#include "viking.h"
#include <ctype.h>
/* strtod */

typedef struct {
  FILE *f;
  gboolean is_route;
} TP_write_info_type;

static void a_gpspoint_write_trackpoint ( VikTrackpoint *tp, TP_write_info_type *write_info );

/* outline for file gpspoint.c

reading file:

take a line.
get first tag, if not type, skip it.
if type, record type.  if waypoint list, etc move on. if track, make a new track, make it current track, add it, etc.
if waypoint, read on and store to the waypoint.
if trackpoint, make trackpoint, store to current track (error / skip if none)

*/

/* Thanks to etrex-cache's gpsbabel's gpspoint.c for starting me off! */
#define VIKING_LINE_SIZE 4096
static char line_buffer[VIKING_LINE_SIZE];

#define GPSPOINT_TYPE_NONE 0
#define GPSPOINT_TYPE_WAYPOINT 1
#define GPSPOINT_TYPE_TRACKPOINT 2
#define GPSPOINT_TYPE_ROUTEPOINT 3
#define GPSPOINT_TYPE_TRACK 4
#define GPSPOINT_TYPE_TRACK_END 5
#define GPSPOINT_TYPE_ROUTE 6
#define GPSPOINT_TYPE_ROUTE_END 7

static VikTrack *current_track; /* pointer to pointer to first GList */

static gint line_type = GPSPOINT_TYPE_NONE;
static struct LatLon line_latlon;
static gchar *line_name;
static gchar *line_comment;
static gchar *line_description;
static gchar *line_source;
static guint line_number = 0;
static gchar *line_xtype;
static gchar *line_color;
static gint line_name_label = 0;
static gint line_dist_label = 0;
static gchar *line_image;
static gchar *line_symbol;
static gchar *line_url;
static gchar *line_url_name;
static gdouble line_image_direction = NAN;
static VikWaypointImageDirectionRef line_image_direction_ref = WP_IMAGE_DIRECTION_REF_TRUE;
static gboolean line_newsegment = FALSE;
static gdouble line_timestamp = NAN;
static gdouble line_altitude = NAN;
static gboolean line_visible = TRUE;

static gboolean line_extended = FALSE;
static gdouble line_speed = NAN;
static gdouble line_course = NAN;
static gdouble line_magvar = NAN;
static gdouble line_geoidheight = NAN;
static guint line_sat = 0;
static guint line_fix = 0;
static gdouble line_hdop = NAN;
static gdouble line_vdop = NAN;
static gdouble line_pdop = NAN;
static gdouble line_ageofdgpsdata = NAN;
static guint line_dgpsid = 0;
/* other possible properties go here */


static void gpspoint_process_tag ( const gchar *tag, guint len );
static void gpspoint_process_key_and_value ( const gchar *key, guint key_len, const gchar *value, guint value_len );

static gchar *slashdup(const gchar *str)
{
  size_t len = strlen(str);
  size_t need_bs_count, i, j;
  gchar *rv;
  for ( i = 0, need_bs_count = 0; i < len; i++ )
    if ( str[i] == '\\' || str[i] == '"' )
      need_bs_count++;
  rv = g_malloc ( (len+need_bs_count+1) * sizeof(gchar) );
  for ( i = 0, j = 0; i < len; i++, j++ )
  {
    if ( str[i] == '\\' || str[i] == '"' )
      rv[j++] = '\\';
    rv[j] = str[i];
    // Basic normalization of strings - replace Linefeed and Carriage returns as blanks.
    //  although allowed in GPX Spec - Viking file format can't handle multi-line strings yet...
    if ( str[i] == '\n' || str[i] == '\r' )
      rv[j] = ' ';
  }
  rv[j] = '\0';
  return rv;
}

static gchar *deslashndup ( const gchar *str, guint16 len )
{
  guint16 i,j, bs_count, new_len;
  gboolean backslash = FALSE;
  gchar *rv;

  if ( len < 1 )
    return NULL;

  for ( i = 0, bs_count = 0; i < len; i++ )
   if ( str[i] == '\\' )
   {
     bs_count++;
     i++;
   }

  if ( str[i-1] == '\\' && (len == 1 || str[i-2] != '\\') )
    bs_count--;

  new_len = len - bs_count;
  rv = g_malloc ( (new_len+1) * sizeof(gchar) );
  for ( i = 0, j = 0; i < len && j < new_len; i++ )
    if ( str[i] == '\\' && !backslash )
      backslash = TRUE;
    else
    {
      rv[j++] = str[i];
      backslash = FALSE;
    }

  rv[new_len] = '\0';
  return rv;
}


static void trackpoints_end ()
{
  if ( current_track )
    if ( current_track->trackpoints ) {
      current_track->trackpoints = g_list_reverse ( current_track->trackpoints );
      current_track = NULL;
    }
}

/*
 * Returns whether file read was a success
 * No obvious way to test for a 'gpspoint' file,
 *  thus set a flag if any actual tag found during processing of the file
 */
gboolean a_gpspoint_read_file(VikTrwLayer *trw, FILE *f, const gchar *dirpath ) {
  VikCoordMode coord_mode = vik_trw_layer_get_coord_mode ( trw );
  gchar *tag_start, *tag_end;
  g_assert ( f != NULL && trw != NULL );
  line_type = GPSPOINT_TYPE_NONE;
  line_timestamp = NAN;
  line_newsegment = FALSE;
  line_image = NULL;
  line_symbol = NULL;
  current_track = NULL;
  gboolean have_read_something = FALSE;

  while (fgets(line_buffer, VIKING_LINE_SIZE, f))
  {
    gboolean inside_quote = 0;
    gboolean backslash = 0;

    line_buffer[strlen(line_buffer)-1] = '\0'; /* chop off newline */

    /* for gpspoint files wrapped inside */
    if ( strlen(line_buffer) >= 13 && strncmp ( line_buffer, "~EndLayerData", 13 ) == 0 ) {
      // Even just a blank TRW is ok when in a .vik file
      have_read_something = TRUE;
      break;
    }

    /* each line: nullify stuff, make thing if nes, free name if ness */
    tag_start = line_buffer;
    for (;;)
    {
      /* my addition: find first non-whitespace character. if the null, skip line. */
      while (*tag_start != '\0' && isspace(*tag_start))
        tag_start++;
      if (*tag_start == '\0')
        break;

      if (*tag_start == '#')
        break;

      tag_end = tag_start;
        if (*tag_end == '"')
          inside_quote = !inside_quote;
      while (*tag_end != '\0' && (!isspace(*tag_end) || inside_quote)) {
        tag_end++;
        if (*tag_end == '\\' && !backslash)
          backslash = TRUE;
        else if (backslash)
          backslash = FALSE;
        else if (*tag_end == '"')
          inside_quote = !inside_quote;
      }

      // Won't have super massively long strings, so potential truncation in cast is acceptable.
      guint len = (guint)(tag_end - tag_start);
      gpspoint_process_tag ( tag_start, len );

      if (*tag_end == '\0' )
        break;
      else
        tag_start = tag_end+1;
    }
    if (line_type == GPSPOINT_TYPE_TRACK_END || line_type == GPSPOINT_TYPE_ROUTE_END) {
      trackpoints_end ();
    }
    if (line_type == GPSPOINT_TYPE_WAYPOINT && line_name)
    {
      // Handle a badly formatted file in case of missing explicit track/route end (this shouldn't happen)
      trackpoints_end ();
      have_read_something = TRUE;
      VikWaypoint *wp = vik_waypoint_new();
      wp->visible = line_visible;
      wp->altitude = line_altitude;
      wp->timestamp = line_timestamp;
      wp->speed = line_speed;
      wp->course = line_course;
      wp->magvar = line_magvar;
      wp->geoidheight = line_geoidheight;
      wp->nsats = line_sat;
      wp->fix_mode = line_fix;
      wp->hdop = line_hdop;
      wp->vdop = line_vdop;
      wp->pdop = line_pdop;
      wp->ageofdgpsdata = line_ageofdgpsdata;
      wp->dgpsid = line_dgpsid;

      vik_coord_load_from_latlon ( &(wp->coord), coord_mode, &line_latlon );

      vik_trw_layer_filein_add_waypoint ( trw, line_name, wp );
      g_free ( line_name );
      line_name = NULL;

      if ( line_comment )
        vik_waypoint_set_comment ( wp, line_comment );

      if ( line_description )
        vik_waypoint_set_description ( wp, line_description );

      if ( line_source )
        vik_waypoint_set_source ( wp, line_source );

      if ( line_url )
        vik_waypoint_set_url ( wp, line_url );

      if ( line_url_name )
        vik_waypoint_set_url_name ( wp, line_url_name );

      if ( line_xtype )
        vik_waypoint_set_type ( wp, line_xtype );

      if ( line_image ) {
        gchar *fn = util_make_absolute_filename ( line_image, dirpath );
        vik_waypoint_set_image ( wp, fn ? fn : line_image );
        g_free ( fn );
      }

      if ( !isnan(line_image_direction) ) {
        wp->image_direction = line_image_direction;
        wp->image_direction_ref = line_image_direction_ref;
      }

      if ( line_symbol )
        vik_waypoint_set_symbol ( wp, line_symbol );
    }
    else if ((line_type == GPSPOINT_TYPE_TRACK || line_type == GPSPOINT_TYPE_ROUTE) && line_name)
    {
      // Handle a badly formatted file in case of missing explicit track/route end (this shouldn't happen)
      trackpoints_end ();
      have_read_something = TRUE;
      VikTrack *pl = vik_track_new();
      // NB don't set defaults here as all properties are stored in the GPS_POINT format
      //vik_track_set_defaults ( pl );

      /* Thanks to Peter Jones for this Fix */
      if (!line_name) line_name = g_strdup("UNK");

      pl->visible = line_visible;
      pl->is_route = (line_type == GPSPOINT_TYPE_ROUTE);

      if ( line_comment )
        vik_track_set_comment ( pl, line_comment );

      if ( line_description )
        vik_track_set_description ( pl, line_description );

      if ( line_source )
        vik_track_set_source ( pl, line_source );

      if ( line_number )
        pl->number = line_number;

      if ( line_xtype )
        vik_track_set_type ( pl, line_xtype );

      if ( line_color )
      {
        if ( gdk_color_parse ( line_color, &(pl->color) ) )
        pl->has_color = TRUE;
      }

      pl->draw_name_mode = line_name_label;
      pl->max_number_dist_labels = line_dist_label;

      pl->trackpoints = NULL;
      vik_trw_layer_filein_add_track ( trw, line_name, pl );
      g_free ( line_name );
      line_name = NULL;

      current_track = pl;
    }
    else if ((line_type == GPSPOINT_TYPE_TRACKPOINT || line_type == GPSPOINT_TYPE_ROUTEPOINT) && current_track)
    {
      have_read_something = TRUE;
      VikTrackpoint *tp = vik_trackpoint_new();
      vik_coord_load_from_latlon ( &(tp->coord), coord_mode, &line_latlon );
      tp->newsegment = line_newsegment;
      tp->timestamp = line_timestamp;
      tp->altitude = line_altitude;
      vik_trackpoint_set_name ( tp, line_name );
      if (line_extended) {
        tp->speed = line_speed;
        tp->course = line_course;
        tp->nsats = line_sat;
        tp->fix_mode = line_fix;
        tp->hdop = line_hdop;
        tp->vdop = line_vdop;
        tp->pdop = line_pdop;
      }
      // Much faster to prepend and then reverse list once all points read in
      // Especially if hunderds of thousands or more trackpoints in a file
      current_track->trackpoints = g_list_prepend ( current_track->trackpoints, tp );
    }

    if (line_name) 
      g_free ( line_name );
    line_name = NULL;
    if (line_comment)
      g_free ( line_comment );
    if (line_description)
      g_free ( line_description );
    if (line_source)
      g_free ( line_source );
    if (line_xtype)
      g_free ( line_xtype );
    if (line_color)
      g_free ( line_color );
    if (line_image)
      g_free ( line_image );
    if (line_symbol)
      g_free ( line_symbol );
    if (line_url)
      g_free ( line_url );
    if (line_url_name)
      g_free ( line_url_name );
    line_comment = NULL;
    line_description = NULL;
    line_source = NULL;
    line_xtype = NULL;
    line_color = NULL;
    line_image = NULL;
    line_image_direction = NAN;
    line_image_direction_ref = WP_IMAGE_DIRECTION_REF_TRUE;
    line_symbol = NULL;
    line_type = GPSPOINT_TYPE_NONE;
    line_newsegment = FALSE;
    line_timestamp = NAN;
    line_altitude = NAN;
    line_visible = TRUE;
    line_url = NULL;
    line_url_name = NULL;
    line_magvar = NAN;
    line_geoidheight = NAN;

    line_extended = FALSE;
    line_speed = NAN;
    line_course = NAN;
    line_sat = 0;
    line_fix = 0;
    line_hdop = NAN;
    line_vdop = NAN;
    line_pdop = NAN;
    line_ageofdgpsdata = NAN;
    line_dgpsid = 0;
    line_name_label = 0;
    line_dist_label = 0;
    line_number = 0;
  }

  // Handle a badly formatted file in case of missing explicit track/route end (this shouldn't happen)
  trackpoints_end ();

  return have_read_something;
}

/* Tag will be of a few defined forms:
   ^[:alpha:]*=".*"$
   ^[:alpha:]*=.*$

   <invalid tag>

So we must determine end of tag name, start of value, end of value.
*/
static void gpspoint_process_tag ( const gchar *tag, guint len )
{
  const gchar *key_end, *value_start, *value_end;

  /* Searching for key end */
  key_end = tag;

  while (++key_end - tag < len)
    if (*key_end == '=')
      break;

  if (key_end - tag == len)
    return; /* no good */

  if (key_end - tag == len + 1)
    value_start = value_end = 0; /* size = 0 */
  else
  {
    value_start = key_end + 1; /* equal_sign plus one */

    if (*value_start == '"')
    {
      value_start++;
      if (*value_start == '"')
        value_start = value_end = 0; /* size = 0 */
      else
      {
        if (*(tag+len-1) == '"')
          value_end = tag + len - 1;
        else
          return; /* bogus */
      }
    }
    else
      value_end = tag + len; /* value start really IS value start. */

    // Detect broken lines which end without any text or the enclosing ". i.e. like: comment="
    if ( (value_end - value_start) < 0 )
      return;

    gpspoint_process_key_and_value(tag, key_end - tag, value_start, value_end - value_start);
  }
}

/*
value = NULL for none
*/
static void gpspoint_process_key_and_value ( const gchar *key, guint key_len, const gchar *value, guint value_len )
{
  // Most commonlly encountered keys should be tested first
  if (key_len == 8 && strncasecmp( key, "latitude", key_len ) == 0 && value != NULL)
  {
    line_latlon.lat = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 9 && strncasecmp( key, "longitude", key_len ) == 0 && value != NULL)
  {
    line_latlon.lon = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 8 && strncasecmp( key, "unixtime", key_len ) == 0 && value != NULL)
  {
    line_timestamp = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 8 && strncasecmp( key, "altitude", key_len ) == 0 && value != NULL)
  {
    line_altitude = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 4 && strncasecmp( key, "type", key_len ) == 0 )
  {
    if (value == NULL)
      line_type = GPSPOINT_TYPE_NONE;
    else if (value_len == 5 && strncasecmp( value, "track", value_len ) == 0 )
      line_type = GPSPOINT_TYPE_TRACK;
    else if (value_len == 8 && strncasecmp( value, "trackend", value_len ) == 0 )
      line_type = GPSPOINT_TYPE_TRACK_END;
    else if (value_len == 10 && strncasecmp( value, "trackpoint", value_len ) == 0 )
      line_type = GPSPOINT_TYPE_TRACKPOINT;
    else if (value_len == 8 && strncasecmp( value, "waypoint", value_len ) == 0 )
      line_type = GPSPOINT_TYPE_WAYPOINT;
    else if (value_len == 5 && strncasecmp( value, "route", value_len ) == 0 )
      line_type = GPSPOINT_TYPE_ROUTE;
    else if (value_len == 8 && strncasecmp( value, "routeend", value_len ) == 0 )
      line_type = GPSPOINT_TYPE_ROUTE_END;
    else if (value_len == 10 && strncasecmp( value, "routepoint", value_len ) == 0 )
      line_type = GPSPOINT_TYPE_ROUTEPOINT;
    else
      /* all others are ignored */
      line_type = GPSPOINT_TYPE_NONE;
  }
  else if (key_len == 4 && strncasecmp( key, "name", key_len ) == 0 && value != NULL)
  {
    if (line_name == NULL)
    {
      line_name = deslashndup ( value, value_len );
    }
  }
  else if (key_len == 7 && strncasecmp( key, "comment", key_len ) == 0 && value != NULL)
  {
    if (line_comment == NULL)
      line_comment = deslashndup ( value, value_len );
  }
  else if (key_len == 11 && strncasecmp( key, "description", key_len ) == 0 && value != NULL)
  {
    if (line_description == NULL)
      line_description = deslashndup ( value, value_len );
  }
  else if (key_len == 6 && strncasecmp( key, "source", key_len ) == 0 && value != NULL)
  {
    if (line_source == NULL)
      line_source = deslashndup ( value, value_len );
  }
  else if (key_len == 6 && strncasecmp( key, "number", key_len ) == 0 && value != NULL)
  {
    line_number = atoi(value);
  }
  // NB using 'xtype' to differentiate from our own 'type' key
  else if (key_len == 5 && strncasecmp( key, "xtype", key_len ) == 0 && value != NULL)
  {
    if (line_xtype == NULL)
      line_xtype = deslashndup ( value, value_len );
  }
  else if (key_len == 5 && strncasecmp( key, "color", key_len ) == 0 && value != NULL)
  {
    if (line_color == NULL)
      line_color = deslashndup ( value, value_len );
  }
  else if (key_len == 14 && strncasecmp( key, "draw_name_mode", key_len ) == 0 && value != NULL)
  {
    line_name_label = atoi(value);
  }
  else if (key_len == 18 && strncasecmp( key, "number_dist_labels", key_len ) == 0 && value != NULL)
  {
    line_dist_label = atoi(value);
  }
  else if (key_len == 5 && strncasecmp( key, "image", key_len ) == 0 && value != NULL)
  {
    if (line_image == NULL)
      line_image = deslashndup ( value, value_len );
  }
  else if (key_len == 15 && strncasecmp( key, "image_direction", key_len ) == 0 && value != NULL)
  {
    line_image_direction = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 19 && strncasecmp( key, "image_direction_ref", key_len ) == 0 && value != NULL)
  {
    line_image_direction_ref = atoi(value);
  }
  else if (key_len == 7 && strncasecmp( key, "visible", key_len ) == 0 && value != NULL && value[0] != 'y' && value[0] != 'Y' && value[0] != 't' && value[0] != 'T')
  {
    line_visible = FALSE;
  }
  else if (key_len == 6 && strncasecmp( key, "symbol", key_len ) == 0 && value != NULL)
  {
    line_symbol = g_strndup ( value, value_len );
  }
  else if (key_len == 10 && strncasecmp( key, "newsegment", key_len ) == 0 && value != NULL)
  {
    line_newsegment = TRUE;
  }
  else if (key_len == 8 && strncasecmp( key, "extended", key_len ) == 0 && value != NULL)
  {
    line_extended = TRUE;
  }
  else if (key_len == 5 && strncasecmp( key, "speed", key_len ) == 0 && value != NULL)
  {
    line_speed = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 6 && strncasecmp( key, "course", key_len ) == 0 && value != NULL)
  {
    line_course = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 3 && strncasecmp( key, "sat", key_len ) == 0 && value != NULL)
  {
    line_sat = (guint)atoi(value);
  }
  else if (key_len == 3 && strncasecmp( key, "fix", key_len ) == 0 && value != NULL)
  {
    line_fix = (guint)atoi(value);
  }
  else if (key_len == 4 && strncasecmp( key, "hdop", key_len ) == 0 && value != NULL)
  {
    line_hdop = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 4 && strncasecmp( key, "vdop", key_len ) == 0 && value != NULL)
  {
    line_vdop = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 4 && strncasecmp( key, "pdop", key_len ) == 0 && value != NULL)
  {
    line_pdop = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 6 && strncasecmp( key, "magvar", key_len ) == 0 && value != NULL)
  {
    line_magvar = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 11 && strncasecmp( key, "geoidheight", key_len ) == 0 && value != NULL)
  {
    line_geoidheight = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 3 && strncasecmp( key, "url", key_len ) == 0 && value != NULL)
  {
    line_url = deslashndup ( value, value_len );
  }
  else if (key_len == 8 && strncasecmp( key, "url_name", key_len ) == 0 && value != NULL)
  {
    line_url_name = deslashndup ( value, value_len );
  }
  else if (key_len == 13 && strncasecmp( key, "ageofdgpsdata", key_len ) == 0 && value != NULL)
  {
    line_ageofdgpsdata = g_ascii_strtod(value, NULL);
  }
  else if (key_len == 6 && strncasecmp( key, "dgpsid", key_len ) == 0 && value != NULL)
  {
    line_dgpsid = (guint)atoi(value);
  }
}

typedef struct {
  FILE *file;
  const gchar *dirpath;
} WritingContext;

static void write_double ( FILE *ff, const gchar *tag, gdouble value )
{
  if ( !isnan(value) ) {
    gchar buf[COORDS_STR_BUFFER_SIZE];
    a_coords_dtostr_buffer ( value, buf );
    fprintf ( ff, " %s=\"%s\"", tag, buf );
  }
}

static void write_positive_uint ( FILE *ff, const gchar *tag, guint value )
{
  if ( value )
    fprintf ( ff, " %s=\"%d\"", tag, value );
}

static void write_string ( FILE *ff, const gchar *tag, const gchar *value )
{
  if ( value && strlen(value) ) {
    gchar *tmp = slashdup(value);
    fprintf ( ff, " %s=\"%s\"", tag, tmp );
    g_free ( tmp );
  }
}

static void a_gpspoint_write_waypoint ( const VikWaypoint *wp, WritingContext *wc )
{
  struct LatLon ll;
  gchar s_lat[COORDS_STR_BUFFER_SIZE];
  gchar s_lon[COORDS_STR_BUFFER_SIZE];
  // Sanity clauses
  if ( !wp )
    return;
  if ( !(wp->name) )
    return;
  if ( !wc )
    return;

  FILE *f = wc->file;

  vik_coord_to_latlon ( &(wp->coord), &ll );
  a_coords_dtostr_buffer ( ll.lat, s_lat );
  a_coords_dtostr_buffer ( ll.lon, s_lon );
  gchar *tmp_name = slashdup(wp->name);
  fprintf ( f, "type=\"waypoint\" latitude=\"%s\" longitude=\"%s\" name=\"%s\"", s_lat, s_lon, tmp_name );
  g_free ( tmp_name );

  write_double ( f, "altitude", wp->altitude );
  write_double ( f, "unixtime", wp->timestamp );
  write_double ( f, "speed", wp->speed );
  write_double ( f, "course", wp->course );
  write_double ( f, "magvar", wp->magvar );
  write_double ( f, "geoidheight", wp->geoidheight );
  write_string ( f, "comment", wp->comment );
  write_string ( f, "description", wp->description );
  write_string ( f, "source", wp->source );
  write_string ( f, "url", wp->url );
  write_string ( f, "url_name", wp->url_name );
  write_string ( f, "xtype", wp->type );

  write_positive_uint ( f, "fix", wp->fix_mode );
  write_positive_uint ( f, "sat", wp->nsats );
  write_double ( f, "hdop", wp->hdop );
  write_double ( f, "vdop", wp->vdop );
  write_double ( f, "pdop", wp->pdop );
  write_double ( f, "ageofdgpsdata", wp->ageofdgpsdata );
  write_positive_uint ( f, "dgpsid", wp->dgpsid );

  if ( wp->image )
  {
    gchar *tmp_image = NULL;
    if ( a_vik_get_file_ref_format() == VIK_FILE_REF_FORMAT_RELATIVE ) {
      if ( wc->dirpath )
        tmp_image = g_strdup ( file_GetRelativeFilename ( (gchar*)wc->dirpath, wp->image ) );
    }

    // if tmp_image not available - use image filename as is
    // this should be an absolute path as set in thumbnails
    if ( !tmp_image )
      tmp_image = slashdup(wp->image);

    if ( tmp_image )
      fprintf ( f, " image=\"%s\"", tmp_image );

    g_free ( tmp_image );
  }
  if ( !isnan(wp->image_direction) )
  {
    gchar *tmp = util_formatd ( "%.2f", wp->image_direction );
    fprintf ( f, " image_direction=\"%s\"", tmp );
    g_free ( tmp );
    fprintf ( f, " image_direction_ref=\"%d\"", wp->image_direction_ref );
  }
  if ( wp->symbol )
  {
    // Due to changes in garminsymbols - the symbol name is now in Title Case
    // However to keep newly generated .vik files better compatible with older Viking versions
    //   The symbol names will always be lowercase
    gchar *tmp_symbol = g_utf8_strdown(wp->symbol, -1);
    fprintf ( f, " symbol=\"%s\"", tmp_symbol );
    g_free ( tmp_symbol );
  }
  if ( ! wp->visible )
    fprintf ( f, " visible=\"n\"" );
  fprintf ( f, "\n" );
}

static void a_gpspoint_write_trackpoint ( VikTrackpoint *tp, TP_write_info_type *write_info )
{
  struct LatLon ll;
  gchar s_lat[COORDS_STR_BUFFER_SIZE];
  gchar s_lon[COORDS_STR_BUFFER_SIZE];
  vik_coord_to_latlon ( &(tp->coord), &ll );

  FILE *f = write_info->f;

  a_coords_dtostr_buffer ( ll.lat, s_lat );
  a_coords_dtostr_buffer ( ll.lon, s_lon );
  fprintf ( f, "type=\"%spoint\" latitude=\"%s\" longitude=\"%s\"", write_info->is_route ? "route" : "track", s_lat, s_lon );

  write_string ( f, "name", tp->name );
  write_double ( f, "altitude", tp->altitude );
  write_double ( f, "unixtime", tp->timestamp );

  if ( tp->newsegment )
    fprintf ( f, " newsegment=\"yes\"" );

  if (!isnan(tp->speed) || !isnan(tp->course) || tp->nsats > 0) {
    fprintf ( f, " extended=\"yes\"" );
    write_double ( f, "speed", tp->speed );
    write_double ( f, "course", tp->course );
    write_positive_uint ( f, "sat", tp->nsats );
    write_positive_uint ( f, "fix", tp->fix_mode );
    write_double ( f, "hdop", tp->hdop );
    write_double ( f, "vdop", tp->vdop );
    write_double ( f, "pdop", tp->pdop );
  }
  fprintf ( f, "\n" );
}


static void a_gpspoint_write_track ( const VikTrack *trk, FILE *f )
{
  // Sanity clauses
  if ( !trk )
    return;
  if ( !(trk->name) )
    return;

  gchar *tmp_name = slashdup(trk->name);
  fprintf ( f, "type=\"%s\" name=\"%s\"", trk->is_route ? "route" : "track", tmp_name );
  g_free ( tmp_name );

  write_string ( f, "comment", trk->comment );
  write_string ( f, "description", trk->description );
  write_string ( f, "source", trk->source );
  write_positive_uint ( f, "number", trk->number );
  write_string ( f, "xtype", trk->type );

  if ( trk->has_color ) {
    fprintf ( f, " color=#%.2x%.2x%.2x", (int)(trk->color.red/256),(int)(trk->color.green/256),(int)(trk->color.blue/256));
  }

  write_positive_uint ( f, "draw_name_mode", trk->draw_name_mode );
  write_positive_uint ( f, "number_dist_labels", trk->max_number_dist_labels );

  if ( ! trk->visible ) {
    fprintf ( f, " visible=\"n\"" );
  }
  fprintf ( f, "\n" );

  TP_write_info_type tp_write_info = { f, trk->is_route };
  g_list_foreach ( trk->trackpoints, (GFunc) a_gpspoint_write_trackpoint, &tp_write_info );
  fprintf ( f, "type=\"%send\"\n", trk->is_route ? "route" : "track" );
}

/**
 * Enforce writing waypoints/tracks/routes in the order they have been read in
 * This should enable comparing changes between file saves much better,
 *  as limits it to the actual changes
 *  (rather than reorderings due to the internal usage of hashtables)
 */
void a_gpspoint_write_file ( VikTrwLayer *trw, FILE *f, const gchar *dirpath )
{
  GHashTable *tracks = vik_trw_layer_get_tracks ( trw );
  GHashTable *routes = vik_trw_layer_get_routes ( trw );
  GHashTable *waypoints = vik_trw_layer_get_waypoints ( trw );
  GList *gl = NULL;
  WritingContext wc = { f, dirpath };

  fprintf ( f, "type=\"waypointlist\"\n" );
  gl = vu_sorted_list_from_hash_table ( waypoints, VL_SO_NONE, VIKING_WAYPOINT );
  for ( GList *it = g_list_first(gl); it != NULL; it = g_list_next(it) )
    a_gpspoint_write_waypoint ( (VikWaypoint*)((SortTRWHashT*)it->data)->data, &wc );
  g_list_free_full ( gl, g_free );
  fprintf ( f, "type=\"waypointlistend\"\n" );

  gl = vu_sorted_list_from_hash_table ( tracks, VL_SO_NONE, VIKING_TRACK );
  for ( GList *it = g_list_first(gl); it != NULL; it = g_list_next(it) )
    a_gpspoint_write_track ( (VikTrack*)((SortTRWHashT*)it->data)->data, f );
  g_list_free_full ( gl, g_free );

  gl = vu_sorted_list_from_hash_table ( routes, VL_SO_NONE, VIKING_TRACK );
  for ( GList *it = g_list_first(gl); it != NULL; it = g_list_next(it) )
    a_gpspoint_write_track ( (VikTrack*)((SortTRWHashT*)it->data)->data, f );
  g_list_free_full ( gl, g_free );
}
