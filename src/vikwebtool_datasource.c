/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2013-2015, Rob Norris <rw_norris@hotmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vikwebtool_datasource.h"
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "globals.h"
#include "acquire.h"
#include "maputils.h"
#include "dialog.h"
#include "ui_util.h"

static GObjectClass *parent_class;
static GHashTable *last_user_strings = NULL;

static void webtool_datasource_finalize ( GObject *gob );

static gchar *webtool_datasource_get_url ( VikWebtool *self, VikWindow *vw );

static gboolean webtool_needs_user_string ( VikWebtool *self );

typedef struct _VikWebtoolDatasourcePrivate VikWebtoolDatasourcePrivate;

struct _VikWebtoolDatasourcePrivate
{
	gchar *url;
	gchar *url_format_code;
	gchar *file_type;
	gchar *babel_filter_args;
    gchar *input_label;
	gchar *user_string;
	DownloadFileOptions options;
};

G_DEFINE_TYPE_WITH_PRIVATE (VikWebtoolDatasource, vik_webtool_datasource, VIK_WEBTOOL_TYPE)
#define WEBTOOL_DATASOURCE_GET_PRIVATE(o) (vik_webtool_datasource_get_instance_private (VIK_WEBTOOL_DATASOURCE(o)))

enum
{
	PROP_0,
	PROP_URL,
	PROP_URL_FORMAT_CODE,
	PROP_FILE_TYPE,
	PROP_BABEL_FILTER_ARGS,
	PROP_INPUT_LABEL,
	PROP_REFERER,
	PROP_USER_AGENT,
	PROP_FOLLOW_LOCATION,
	PROP_CUSTOM_HTTP_HEADERS,
};

static void webtool_datasource_set_property (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE ( object );

	switch ( property_id ) {

    case PROP_URL:
		g_free ( priv->url );
		priv->url = g_value_dup_string ( value );
		g_debug ( "VikWebtoolDatasource.url: %s", priv->url );
		break;

	case PROP_URL_FORMAT_CODE:
		g_free ( priv->url_format_code );
		priv->url_format_code = g_value_dup_string ( value );
		g_debug ( "VikWebtoolDatasource.url_format_code: %s", priv->url_format_code );
		break;

	case PROP_FILE_TYPE:
		g_free ( priv->file_type );
		priv->file_type = g_value_dup_string ( value );
		g_debug ( "VikWebtoolDatasource.file_type: %s", priv->file_type );
		break;

	case PROP_BABEL_FILTER_ARGS:
		g_free ( priv->babel_filter_args );
		priv->babel_filter_args = g_value_dup_string ( value );
		g_debug ( "VikWebtoolDatasource.babel_filter_args: %s", priv->babel_filter_args );
		break;

    case PROP_INPUT_LABEL:
		g_free ( priv->input_label );
		priv->input_label = g_value_dup_string ( value );
		g_debug ( "VikWebtoolDatasource.input_label: %s", priv->input_label );
		break;

	case PROP_REFERER:
		g_free (priv->options.referer);
		priv->options.referer = g_value_dup_string (value);
		break;

	case PROP_USER_AGENT:
		g_free (priv->options.user_agent);
		priv->options.user_agent = g_value_dup_string (value);
		break;

    case PROP_FOLLOW_LOCATION:
		priv->options.follow_location = g_value_get_long (value);
		break;

    case PROP_CUSTOM_HTTP_HEADERS:
		{
			g_free (priv->options.custom_http_headers);
			const gchar *str = g_value_get_string ( value );
			// Convert the literal characters '\'+'n' into actual newlines '\n'
			if ( str )
				priv->options.custom_http_headers = g_strcompress ( str );
			else
				priv->options.custom_http_headers = NULL;
		}
		break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID ( object, property_id, pspec );
		break;
	}
}

static void webtool_datasource_get_property (GObject    *object,
                                             guint       property_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE ( object );

	switch ( property_id ) {

	case PROP_URL:                 g_value_set_string ( value, priv->url ); break;
	case PROP_URL_FORMAT_CODE:     g_value_set_string ( value, priv->url_format_code ); break;
	case PROP_FILE_TYPE:           g_value_set_string ( value, priv->url ); break;
	case PROP_BABEL_FILTER_ARGS:   g_value_set_string ( value, priv->babel_filter_args ); break;
	case PROP_INPUT_LABEL:         g_value_set_string ( value, priv->input_label ); break;
	case PROP_REFERER:             g_value_set_string ( value, priv->options.referer ); break;
	case PROP_USER_AGENT:          g_value_set_string ( value, priv->options.user_agent );break;
	case PROP_FOLLOW_LOCATION:     g_value_set_long   ( value, priv->options.follow_location); break;
	case PROP_CUSTOM_HTTP_HEADERS: g_value_set_string ( value, priv->options.custom_http_headers); break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID ( object, property_id, pspec );
		break;
	}
}

typedef struct {
	VikExtTool *self;
	VikWindow *vw;
	VikViewport *vvp;
	GtkWidget *user_string;
} datasource_t;


static void ensure_last_user_strings_hash() {
    if ( last_user_strings == NULL ) {
        last_user_strings = g_hash_table_new_full ( g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    g_free );
    }
}


static gchar *get_last_user_string ( const datasource_t *source ) {
    ensure_last_user_strings_hash();
    gchar *label = vik_ext_tool_get_label ( source->self );
    gchar *last_str = g_hash_table_lookup ( last_user_strings, label );
    g_free( label );
    return last_str;
}


static void set_last_user_string ( const datasource_t *source, const gchar *s ) {
    ensure_last_user_strings_hash();
    g_hash_table_insert ( last_user_strings,
                          vik_ext_tool_get_label ( source->self ),
                          g_strdup ( s ) );
}

static gpointer datasource_init ( acq_vik_t *avt )
{
	datasource_t *data = g_malloc(sizeof(*data));
	data->self = avt->userdata;
	data->vw = avt->vw;
	data->vvp = avt->vvp;
	data->user_string = NULL;
	return data;
}

static void datasource_add_setup_widgets ( GtkWidget *dialog, VikViewport *vvp, gpointer user_data )
{
	datasource_t *widgets = (datasource_t *)user_data;
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE ( widgets->self );
	GtkWidget *user_string_label;
	gchar *label = g_strdup_printf( "%s:", priv->input_label );
	user_string_label = gtk_label_new ( label );
	widgets->user_string = ui_entry_new ( NULL, GTK_ENTRY_ICON_SECONDARY );

	gchar *last_str = get_last_user_string ( widgets );
	if ( last_str )
		gtk_entry_set_text( GTK_ENTRY( widgets->user_string ), last_str );

	// 'ok' when press return in the entry
	g_signal_connect_swapped (widgets->user_string, "activate", G_CALLBACK(a_dialog_response_accept), dialog);

	/* Packing all widgets */
	GtkBox *box = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
	gtk_box_pack_start ( box, user_string_label, FALSE, FALSE, 5 );
	gtk_box_pack_start ( box, widgets->user_string, FALSE, FALSE, 5 );
	gtk_widget_show_all ( dialog );
	gtk_dialog_set_default_response ( GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT );
	// NB presently the focus is overridden later on by the acquire.c code.
	gtk_widget_grab_focus ( widgets->user_string );

	g_free ( label );
}



static void datasource_get_process_options ( gpointer user_data, ProcessOptions *po, DownloadFileOptions *options, const gchar *notused1, const gchar *notused2 )
{
	datasource_t *data = (datasource_t*) user_data;

	VikWebtool *vwd = VIK_WEBTOOL ( data->self );
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE ( data->self );

	if ( webtool_needs_user_string ( vwd ) ) {
		priv->user_string = g_strdup ( gtk_entry_get_text ( GTK_ENTRY ( data->user_string ) ) );

        if ( priv->user_string[0] != '\0' ) {
            set_last_user_string ( data, priv->user_string );
        }
    }

	gchar *url = vik_webtool_get_url ( vwd, data->vw );
	g_debug ("%s: %s", __FUNCTION__, url );

	po->url = g_strdup ( url );

	// Only use first section of the file_type string
	// One can't use values like 'kml -x transform,rte=wpt' in order to do fancy things
	//  since it won't be in the right order for the overall GPSBabel command
	// So prevent any potentially dangerous behaviour
	gchar **parts = NULL;
	if ( priv->file_type )
		parts = g_strsplit ( priv->file_type, " ", 0);
	if ( parts )
		po->input_file_type = g_strdup ( parts[0] );
	else
		po->input_file_type = NULL;
	g_strfreev ( parts );

	po->babel_filters = g_strdup ( priv->babel_filter_args );

	options->referer             = priv->options.referer;
	options->user_agent          = priv->options.user_agent;
	options->follow_location     = priv->options.follow_location;
	options->custom_http_headers = priv->options.custom_http_headers;
}

static void cleanup ( gpointer data )
{
	g_free ( data );
}

static void webtool_datasource_open ( VikExtTool *self, VikWindow *vw )
{
	gboolean search = webtool_needs_user_string ( VIK_WEBTOOL ( self ) );

	// Use VikDataSourceInterface to give thready goodness controls of downloading stuff (i.e. can cancel the request)

	// Can now create a 'VikDataSourceInterface' on the fly...
	VikDataSourceInterface *vik_datasource_interface = g_malloc(sizeof(VikDataSourceInterface));

	// An 'easy' way of assigning values
	VikDataSourceInterface data = {
		vik_ext_tool_get_label (self),
		vik_ext_tool_get_label (self),
		VIK_DATASOURCE_ADDTOLAYER,
		VIK_DATASOURCE_INPUTTYPE_NONE,
		FALSE, // Maintain current view - rather than setting it to the acquired points
		TRUE,
		TRUE,
		(VikDataSourceInitFunc)               datasource_init,
		(VikDataSourceCheckExistenceFunc)     NULL,
		(VikDataSourceAddSetupWidgetsFunc)    (search ? datasource_add_setup_widgets : NULL),
		(VikDataSourceGetProcessOptionsFunc)  datasource_get_process_options,
		(VikDataSourceProcessFunc)            a_babel_convert_from,
		(VikDataSourceProgressFunc)           NULL,
		(VikDataSourceAddProgressWidgetsFunc) NULL,
		(VikDataSourceCleanupFunc)            cleanup,
		(VikDataSourceOffFunc)                NULL,
		NULL,
		0,
		NULL,
		NULL,
		0
	};
	memcpy ( vik_datasource_interface, &data, sizeof(VikDataSourceInterface) );

	a_acquire ( vw, vik_window_layers_panel(vw), vik_window_viewport (vw), data.mode, vik_datasource_interface, self, cleanup );
}

static void vik_webtool_datasource_class_init ( VikWebtoolDatasourceClass *klass )
{
	GObjectClass *gobject_class;
	VikWebtoolClass *base_class;
	GParamSpec *pspec;

	gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->finalize = webtool_datasource_finalize;
	gobject_class->set_property = webtool_datasource_set_property;
	gobject_class->get_property = webtool_datasource_get_property;

	pspec = g_param_spec_string ("url",
	                             "Template URL",
	                             "Set the template URL",
	                             VIKING_URL /* default value */,
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class,
	                                 PROP_URL,
	                                 pspec);

	pspec = g_param_spec_string ("url_format_code",
	                             "Template URL Format Code",
	                             "Set the template URL format code",
	                             "LRBT", // default value
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class,
	                                 PROP_URL_FORMAT_CODE,
	                                 pspec);

	pspec = g_param_spec_string ("file_type",
	                             "The file type expected",
	                             "Set the file type",
	                             NULL, // default value ~ equates to internal GPX reading
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class,
	                                 PROP_FILE_TYPE,
	                                 pspec);

	pspec = g_param_spec_string ("babel_filter_args",
	                             "The command line filter options to pass to gpsbabel",
	                             "Set the command line filter options for gpsbabel",
	                             NULL, // default value
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class,
	                                 PROP_BABEL_FILTER_ARGS,
	                                 pspec);

	pspec = g_param_spec_string ("input_label",
	                             "The label for the user input box if input is required.",
	                             "Set the label to be shown next to the user input box if an input term is required",
	                             _("Search Term"),
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class,
	                                 PROP_INPUT_LABEL,
	                                 pspec);

	pspec = g_param_spec_string ("referer",
	                             "Referer",
	                             "The REFERER string to use in HTTP request",
	                             NULL, // default value
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class, PROP_REFERER, pspec);

	pspec = g_param_spec_string ("user-agent",
	                             "User Agent",
	                             "The User Agent string to send in the request",
	                             NULL, // default value
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class, PROP_USER_AGENT, pspec);

	pspec = g_param_spec_long ("follow-location",
	                           "Follow location",
	                           "Specifies the number of retries to follow a redirect while downloading a page",
	                           -1, // minimum value (unlimited)
	                           G_MAXLONG, // maximum value
	                           2, // default value
	                           G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class, PROP_FOLLOW_LOCATION, pspec);

	pspec = g_param_spec_string ("custom-http-headers",
	                             "Custom HTTP Headers",
	                             "Custom HTTP Headers, use '\n' to separate multiple headers",
	                             NULL, // default value
	                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property (gobject_class, PROP_CUSTOM_HTTP_HEADERS, pspec);

	parent_class = g_type_class_peek_parent (klass);

	base_class = VIK_WEBTOOL_CLASS ( klass );
	base_class->get_url = webtool_datasource_get_url;

	// Override default open function here:
	VikExtToolClass *ext_tool_class = VIK_EXT_TOOL_CLASS ( klass );
	ext_tool_class->open = webtool_datasource_open;
}

VikWebtoolDatasource *vik_webtool_datasource_new ()
{
	return VIK_WEBTOOL_DATASOURCE ( g_object_new ( VIK_WEBTOOL_DATASOURCE_TYPE, NULL ) );
}

VikWebtoolDatasource *vik_webtool_datasource_new_with_members ( const gchar *label,
                                                                const gchar *url,
                                                                const gchar *url_format_code,
                                                                const gchar *file_type,
                                                                const gchar *babel_filter_args,
                                                                const gchar *input_label )
{
	VikWebtoolDatasource *result = VIK_WEBTOOL_DATASOURCE ( g_object_new ( VIK_WEBTOOL_DATASOURCE_TYPE,
	                                                        "label", label,
	                                                        "url", url,
	                                                        "url_format_code", url_format_code,
	                                                        "file_type", file_type,
	                                                        "babel_filter_args", babel_filter_args,
                                                            "input_label", input_label,
	                                                        NULL ) );

	return result;
}

static void vik_webtool_datasource_init ( VikWebtoolDatasource *self )
{
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE (self);
	priv->url = NULL;
	priv->url_format_code = NULL;
	priv->file_type = NULL;
	priv->babel_filter_args = NULL;
    priv->input_label = NULL;
	priv->user_string = NULL;
	priv->options.referer = NULL;
	priv->options.follow_location = 0;
	priv->options.expiry_age = 0;
	priv->options.check_file = NULL;
	priv->options.check_file_server_time = FALSE;
	priv->options.use_etag = FALSE;
	priv->options.user_agent = NULL;
	priv->options.custom_http_headers = NULL;
}

static void webtool_datasource_finalize ( GObject *gob )
{
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE ( gob );
	g_free ( priv->url ); priv->url = NULL;
	g_free ( priv->url_format_code ); priv->url_format_code = NULL;
	g_free ( priv->file_type ); priv->file_type = NULL;
	g_free ( priv->babel_filter_args ); priv->babel_filter_args = NULL;
	g_free ( priv->input_label ); priv->input_label = NULL;
	g_free ( priv->user_string); priv->user_string = NULL;
	g_free ( priv->options.referer ); priv->options.referer = NULL;
	g_free ( priv->options.user_agent ); priv->options.user_agent = NULL;
	g_free ( priv->options.custom_http_headers ); priv->options.custom_http_headers = NULL;
	G_OBJECT_CLASS(parent_class)->finalize(gob);
}

#define MAX_NUMBER_CODES 7

/**
 * Calculate individual elements (similarly to the VikWebtool Bounds & Center) for *all* potential values
 * Then only values specified by the URL format are used in parameterizing the URL
 */
static gchar *webtool_datasource_get_url ( VikWebtool *self, VikWindow *vw )
{
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE ( self );
	VikViewport *viewport = vik_window_viewport ( vw );

	// Get top left and bottom right lat/lon pairs from the viewport
	gdouble min_lat, max_lat, min_lon, max_lon;
	gchar sminlon[COORDS_STR_BUFFER_SIZE];
	gchar smaxlon[COORDS_STR_BUFFER_SIZE];
	gchar sminlat[COORDS_STR_BUFFER_SIZE];
	gchar smaxlat[COORDS_STR_BUFFER_SIZE];
	vik_viewport_get_min_max_lat_lon ( viewport, &min_lat, &max_lat, &min_lon, &max_lon );

	min_lon = ROUND_TO_DECIMAL_PLACES ( min_lon, 6 );
	max_lon = ROUND_TO_DECIMAL_PLACES ( max_lon, 6 );
	min_lat = ROUND_TO_DECIMAL_PLACES ( min_lat, 6 );
	max_lat = ROUND_TO_DECIMAL_PLACES ( max_lat, 6 );
	// Cannot simply use g_strdup_printf and gdouble due to locale.
	// As we compute an URL, we have to think in C locale.
	// Furthermore ensure decimal output (never scientific notation)
	a_coords_dtostr_buffer ( min_lon, sminlon );
	a_coords_dtostr_buffer ( max_lon, smaxlon );
	a_coords_dtostr_buffer ( min_lat, sminlat );
	a_coords_dtostr_buffer ( max_lat, smaxlat );

	// Center values
	const VikCoord *coord = vik_viewport_get_center ( viewport );
	struct LatLon ll;
	vik_coord_to_latlon ( coord, &ll );

	ll.lat = ROUND_TO_DECIMAL_PLACES ( ll.lat, 6 );
	ll.lon = ROUND_TO_DECIMAL_PLACES ( ll.lon, 6 );

	gchar scenterlat[COORDS_STR_BUFFER_SIZE];
	gchar scenterlon[COORDS_STR_BUFFER_SIZE];
	a_coords_dtostr_buffer ( ll.lat, scenterlat );
	a_coords_dtostr_buffer ( ll.lon, scenterlon );

	guint8 zoom = 17; // A zoomed in default
	// zoom - ideally x & y factors need to be the same otherwise use the default
	if ( vik_viewport_get_xmpp ( viewport ) == vik_viewport_get_ympp ( viewport ) )
		zoom = map_utils_mpp_to_zoom_level ( vik_viewport_get_zoom ( viewport ) );

	gchar szoom[G_ASCII_DTOSTR_BUF_SIZE];
	g_snprintf ( szoom, G_ASCII_DTOSTR_BUF_SIZE, "%d", zoom );

	gint len = 0;
	if ( priv->url_format_code )
		len = strlen ( priv->url_format_code );
	if ( len > MAX_NUMBER_CODES )
		len = MAX_NUMBER_CODES;

	gchar* values[MAX_NUMBER_CODES];
	int i;
	for ( i = 0; i < MAX_NUMBER_CODES; i++ ) {
		values[i] = '\0';
	}

	for ( i = 0; i < len; i++ ) {
		switch ( g_ascii_toupper ( priv->url_format_code[i] ) ) {
		case 'L': values[i] = g_strdup ( sminlon ); break;
		case 'R': values[i] = g_strdup ( smaxlon ); break;
		case 'B': values[i] = g_strdup ( sminlat ); break;
		case 'T': values[i] = g_strdup ( smaxlat ); break;
		case 'A': values[i] = g_strdup ( scenterlat ); break;
		case 'O': values[i] = g_strdup ( scenterlon ); break;
		case 'Z': values[i] = g_strdup ( szoom ); break;
		case 'S': values[i] = g_strdup ( priv->user_string ); break;
		default: break;
		}
	}

	gchar *url = g_strdup_printf ( priv->url, values[0], values[1], values[2], values[3], values[4], values[5], values[6] );

	for ( i = 0; i < MAX_NUMBER_CODES; i++ ) {
		g_free ( values[i] );
	}

	return url;
}

// NB Only works for ascii strings
char* strcasestr2(const char *dst, const char *src)
{
	if ( !dst || !src )
		return NULL;

	if(src[0] == '\0')
		return (char*)dst;

	int len = strlen(src) - 1;
	char sc = tolower(src[0]);
	for(char dc = *dst; (dc = *dst); dst++) {
		dc = tolower(dc);
		if(sc == dc && (len == 0 || !strncasecmp(dst+1, src+1, len)))
			return (char*)dst;
	}

	return NULL;
}

/**
 * Returns true if the URL format contains 'S' -- that is, a search term entry
 * box needs to be displayed
 */
static gboolean webtool_needs_user_string ( VikWebtool *self )
{
	VikWebtoolDatasourcePrivate *priv = WEBTOOL_DATASOURCE_GET_PRIVATE ( self );
	// For some reason (my) Windows build gets built with -D_GNU_SOURCE
#if (_GNU_SOURCE && !WINDOWS)
	return (strcasestr(priv->url_format_code, "S") != NULL);
#else
	return (strcasestr2(priv->url_format_code, "S") != NULL);
#endif
}
