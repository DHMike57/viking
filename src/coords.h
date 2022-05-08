/*
coords.h
borrowed from:
http://acme.com/software/coords/
I (Evan Battaglia <viking@greentorch.org> have only made some small changes such as
renaming functions and defining LatLon and UTM structs.
*/
/* coords.h - include file for coords routines
**
** Copyright © 2001 by Jef Poskanzer <jef@acme.com>.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

#ifndef _VIKING_COORDS_H
#define _VIKING_COORDS_H

#include <glib.h>

G_BEGIN_DECLS

struct UTM {
  gdouble northing;
  gdouble easting;
  gchar zone;
  gchar letter;
};

struct LatLon {
  gdouble lat;
  gdouble lon;
};

#define COORDS_STR_BUFFER_SIZE 24

int a_coords_utm_equal( const struct UTM *utm1, const struct UTM *utm2 );
void a_coords_latlon_to_utm ( const struct LatLon *latlon, struct UTM *utm );
void a_coords_utm_to_latlon ( const struct UTM *utm, struct LatLon *latlon );
double a_coords_utm_diff( const struct UTM *utm1, const struct UTM *utm2 );
double a_coords_latlon_diff ( const struct LatLon *ll1, const struct LatLon *ll2 );

/**
 * Convert a double to a string WITHOUT LOCALE.
 *
 * Following GPX specifications, decimal values are xsd:decimal
 * So, they must use the period separator, not the localized one.
 *
 * The returned value must be freed by g_free.
 */
char *a_coords_dtostr ( double d );

/**
 * Similar to a_coords_dtostr() above, but uses an existing allocated
 *  buffer thus avoiding the need for malloc/free
 * Ideal for use where this may be called a lot (e.g. in file saving)
 */
void a_coords_dtostr_buffer ( double d, char buffer[COORDS_STR_BUFFER_SIZE] );

/**
 * Convert a LatLon to strings.
 *
 * Using the preferred representation.
 *
 * Strings are allocated and thus should be freed after use
 */
void a_coords_latlon_to_string ( const struct LatLon *latlon, gchar **lat, gchar **lon );

/**
 * a_coords_latlon_destination:
 *
 * @distance: In metres
 * @brg: Bearing in degrees
 *
 * Given a start point, initial bearing, and distance, this will calculate the destination point
 *  travelling along a (shortest distance) great circle arc.
 */
void a_coords_latlon_destination ( const struct LatLon *start, double distance, double brg, struct LatLon *destination );

G_END_DECLS

#endif
