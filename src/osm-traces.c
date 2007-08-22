/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2007, Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "viking.h"
#include "viktrwlayer.h"
#include "osm-traces.h"
#include "gpx.h"
#include "background.h"

/**
 * Login to use for OSM uploading.
 */
static gchar *user = NULL;

/**
 * Password to use for OSM uploading.
 */
static gchar *password = NULL;

/**
 * Mutex to protect auth. token
 */
static GMutex *login_mutex = NULL;

/**
 * Struct hosting needed info.
 */
typedef struct _OsmTracesInfo {
  gchar *name;
  gchar *description;
  gchar *tags;
  gboolean public;
  VikTrwLayer *vtl;
} OsmTracesInfo;

/**
 * Free an OsmTracesInfo struct.
 */
static void oti_free(OsmTracesInfo *oti)
{
  if (oti) {
    /* Fields have been g_strdup'ed */
    g_free(oti->name); oti->name = NULL;
    g_free(oti->description); oti->description = NULL;
    g_free(oti->tags); oti->tags = NULL;
    
    g_object_unref(oti->vtl); oti->vtl = NULL;
  }
  /* Main struct has been g_malloc'ed */
  g_free(oti);
}

static void set_login(const gchar *user_, const gchar *password_)
{
  /* Allocate mutex */
  if (login_mutex == NULL)
  {
    login_mutex = g_mutex_new();
  }
  g_mutex_lock(login_mutex);
  g_free(user); user = NULL;
  g_free(password); password = NULL;
  user        = g_strdup(user_);
  password    = g_strdup(password_);
  g_mutex_unlock(login_mutex);
}

static gchar *get_login()
{
  gchar *user_pass = NULL;
  g_mutex_lock(login_mutex);
  user_pass = g_strdup_printf("%s:%s", user, password);
  g_mutex_unlock(login_mutex);
  return user_pass;
}

/*
 * Upload a file
 */
void osm_traces_upload_file(const char *user,
		            const char *password,
		            const char *file,
		            const char *filename,
		            const char *description,
		            const char *tags,
		            gboolean public)
{
  CURL *curl;
  CURLcode res;
  char curl_error_buffer[CURL_ERROR_SIZE];
  struct curl_slist *headers = NULL;
  struct curl_httppost *post=NULL;
  struct curl_httppost *last=NULL;
  gchar *public_string;

  char *base_url = "http://www.openstreetmap.org/api/0.4/gpx/create";

  gchar *user_pass = get_login();

  g_debug("%s: %s %s %s %s %s %s", __FUNCTION__,
  	  user, password, file, filename, description, tags);

  /* Init CURL */
  curl = curl_easy_init();

  /* Filling the form */
  curl_formadd(&post, &last,
               CURLFORM_COPYNAME, "description",
               CURLFORM_COPYCONTENTS, description, CURLFORM_END);
  curl_formadd(&post, &last,
               CURLFORM_COPYNAME, "tags",
               CURLFORM_COPYCONTENTS, tags, CURLFORM_END);
  /* Public field fixed to "1" actually */
  if (public)
    public_string = "1";
  else
    public_string = "0";
  curl_formadd(&post, &last,
               CURLFORM_COPYNAME, "public",
               CURLFORM_COPYCONTENTS, public_string, CURLFORM_END);
  curl_formadd(&post, &last,
               CURLFORM_COPYNAME, "file",
               CURLFORM_FILE, file,
               CURLFORM_FILENAME, filename,
	       CURLFORM_CONTENTTYPE, "text/xml", CURLFORM_END);

  /* Prepare request */
  /* As explained in http://wiki.openstreetmap.org/index.php/User:LA2 */
  /* Expect: header seems to produce incompatibilites between curl and httpd */
  headers = curl_slist_append(headers, "Expect: ");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);
  curl_easy_setopt(curl, CURLOPT_URL, base_url);
  curl_easy_setopt(curl, CURLOPT_USERPWD, user_pass);
  curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &curl_error_buffer);

  /* Execute request */
  res = curl_easy_perform(curl);
  if (res == CURLE_OK)
  {
    long code;
    res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (res == CURLE_OK)
    {
      g_debug("received valid curl response: %ld", code);
      if (code != 200)
        g_warning("failed to upload data: HTTP response is %ld", code);
    }
    else
      g_error("curl_easy_getinfo failed: %d", res);
  }
  else
    {
      g_warning("curl request failed: %s", curl_error_buffer);
    }

  /* Memory */
  g_free(user_pass); user_pass = NULL;
  
  curl_formfree(post);
  curl_easy_cleanup(curl); 
}

/**
 * uploading function executed by the background" thread
 */
static void osm_traces_upload_thread ( OsmTracesInfo *oti, gpointer threaddata )
{
  FILE *file = NULL;
  gchar *filename = NULL;
  int fd;
  GError *error = NULL;
  int ret;

  g_assert(oti != NULL);

  /* Opening temporary file */
  fd = g_file_open_tmp("viking_osm_upload_XXXXXX.gpx", &filename, &error);
  if (fd < 0) {
    g_error("failed to open temporary file: %s", strerror(errno));
    return;
  }
  g_clear_error(&error);
  g_debug("%s: temporary file = %s", __FUNCTION__, filename);

  /* Creating FILE* */
  file = fdopen(fd, "w");

  /* writing gpx file */
  a_gpx_write_file(oti->vtl, file);
  
  /* We can close the file */
  /* This also close the associated fd */
  fclose(file);

  /* finally, upload it */
  osm_traces_upload_file(user, password, filename,
                         oti->name, oti->description, oti->tags, oti->public);

  /* Removing temporary file */
  ret = g_unlink(filename);
  if (ret != 0) {
    g_error("failed to unlink temporary file: %s", strerror(errno));
  }
}

/**
 * Uploading a VikTrwLayer
 */
static void osm_traces_upload_viktrwlayer ( VikTrwLayer *vtl )
{
  GtkWidget *dia = gtk_dialog_new_with_buttons ("OSM upload",
                                                 VIK_GTK_WINDOW_FROM_LAYER(vtl),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_STOCK_CANCEL,
                                                 GTK_RESPONSE_REJECT,
                                                 GTK_STOCK_OK,
                                                 GTK_RESPONSE_ACCEPT,
                                                 NULL);

  const gchar *name = NULL;
  GtkWidget *user_label, *user_entry;
  GtkWidget *password_label, *password_entry;
  GtkWidget *name_label, *name_entry;
  GtkWidget *description_label, *description_entry;
  GtkWidget *tags_label, *tags_entry;
  GtkWidget *public;
  GtkTooltips* dialog_tips;

  dialog_tips = gtk_tooltips_new();

  user_label = gtk_label_new("Email:");
  user_entry = gtk_entry_new();
  if (user != NULL)
    gtk_entry_set_text(GTK_ENTRY(user_entry), user);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), user_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), user_entry, FALSE, FALSE, 0);
  gtk_widget_show_all ( user_label );
  gtk_widget_show_all ( user_entry );
  gtk_tooltips_set_tip (dialog_tips, user_entry,
                        "The email used as login",
                        "Enter the email you use to login into www.openstreetmap.org.");

  password_label = gtk_label_new("Password:");
  password_entry = gtk_entry_new();
  if (password != NULL)
    gtk_entry_set_text(GTK_ENTRY(password_entry), password);
  /* This is a password -> invisible */
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), password_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), password_entry, FALSE, FALSE, 0);
  gtk_widget_show_all ( password_label );
  gtk_widget_show_all ( password_entry );
  gtk_tooltips_set_tip (dialog_tips, password_entry,
                        "The password used to login",
                        "Enter the password you use to login into www.openstreetmap.org.");

  name_label = gtk_label_new("File's name:");
  name_entry = gtk_entry_new();
  name = vik_layer_get_name(VIK_LAYER(vtl));
  gtk_entry_set_text(GTK_ENTRY(name_entry), name);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), name_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), name_entry, FALSE, FALSE, 0);
  gtk_widget_show_all ( name_label );
  gtk_widget_show_all ( name_entry );
  gtk_tooltips_set_tip (dialog_tips, name_entry,
                        "The name of the file on OSM",
                        "This is the name of the file created on the server. "
			"This is not the name of the local file.");

  description_label = gtk_label_new("Description:");
  description_entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), description_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), description_entry, FALSE, FALSE, 0);
  gtk_widget_show_all ( description_label );
  gtk_widget_show_all ( description_entry );
  gtk_tooltips_set_tip (dialog_tips, description_entry,
                        "The description of the trace",
                        "");

  tags_label = gtk_label_new("Tags:");
  tags_entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), tags_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), tags_entry, FALSE, FALSE, 0);
  gtk_widget_show_all ( tags_label );
  gtk_widget_show_all ( tags_entry );
  gtk_tooltips_set_tip (dialog_tips, tags_entry,
                        "The tags associated to the trace",
                        "");

  public = gtk_check_button_new_with_label("Public");
  /* Set public by default */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(public), TRUE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox), public, FALSE, FALSE, 0);
  gtk_widget_show_all ( public );
  gtk_tooltips_set_tip (dialog_tips, public,
                        "Indicates if the trace is public or not",
                        "");

  if ( gtk_dialog_run ( GTK_DIALOG(dia) ) == GTK_RESPONSE_ACCEPT )
  {
    gchar *title = NULL;

    /* overwrite authentication info */
    set_login(gtk_entry_get_text(GTK_ENTRY(user_entry)),
              gtk_entry_get_text(GTK_ENTRY(password_entry)));

    /* Storing data for the future thread */
    OsmTracesInfo *info = g_malloc(sizeof(OsmTracesInfo));
    info->name        = g_strdup(gtk_entry_get_text(GTK_ENTRY(name_entry)));
    info->description = g_strdup(gtk_entry_get_text(GTK_ENTRY(description_entry)));
    /* TODO Normalize tags: they will be used as URL part */
    info->tags        = g_strdup(gtk_entry_get_text(GTK_ENTRY(tags_entry)));
    info->public      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(public));
    info->vtl         = VIK_TRW_LAYER(g_object_ref(vtl));

    title = g_strdup_printf("Uploading %s to OSM", info->name);

    /* launch the thread */
    a_background_thread(VIK_GTK_WINDOW_FROM_LAYER(vtl),          /* parent window */
			title,                                   /* description string */
			(vik_thr_func) osm_traces_upload_thread, /* function to call within thread */
			info,                                    /* pass along data */
			(vik_thr_free_func) oti_free,            /* function to free pass along data */
			(vik_thr_free_func) NULL,
			1 );
    g_free ( title ); title = NULL;
  }
  gtk_widget_destroy ( dia );
}

/**
 * Function called by the button
 */
void osm_traces_upload_cb ( gpointer layer_and_vlp[2], guint file_type )
{
  osm_traces_upload_viktrwlayer(VIK_TRW_LAYER(layer_and_vlp[0]));
}
