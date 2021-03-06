/* -*- mode: c; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * gcal-application.c
 * Copyright (C) 2015 Erick Pérez Castellanos <erickpc@gnome.org>
 *
 * gnome-calendar is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gnome-calendar is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "GcalApplication"

#include "config.h"

#include "css-code.h"
#include "gcal-application.h"
#include "gcal-debug.h"
#include "gcal-enums.h"
#include "gcal-log.h"
#include "gcal-night-light-monitor.h"
#include "gcal-shell-search-provider.h"
#include "gcal-weather-service.h"
#include "gcal-window.h"

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

struct _GcalApplication
{
  GtkApplication      parent;

  GtkWidget          *window;

  GcalManager        *manager;

  GtkCssProvider     *provider;
  GtkCssProvider     *colors_provider;

  gchar              *uuid;
  icaltimetype       *initial_date;

  GSettings          *desktop_settings;
  GcalTimeFormat      time_format;

  GcalWeatherService *weather_service;
  GcalShellSearchProvider *search_provider;
  GcalNightLightMonitor *night_light_monitor;

  GcalClock          *clock;
};

static void     gcal_application_launch_search        (GSimpleAction           *search,
                                                       GVariant                *parameter,
                                                       gpointer                 app);

static void     gcal_application_show_about           (GSimpleAction           *simple,
                                                       GVariant                *parameter,
                                                       gpointer                 user_data);

static void     gcal_application_sync                 (GSimpleAction           *sync,
                                                       GVariant                *parameter,
                                                       gpointer                 app);

static void     gcal_application_quit                 (GSimpleAction           *simple,
                                                       GVariant                *parameter,
                                                       gpointer                 user_data);

G_DEFINE_TYPE (GcalApplication, gcal_application, GTK_TYPE_APPLICATION);

static gboolean show_version = FALSE;

static GOptionEntry gcal_application_goptions[] = {
  { 
    "quit", 'q', 0,
    G_OPTION_ARG_NONE, NULL,
    N_("Quit GNOME Calendar"), NULL
  },
  {
    "version", 'v', 0,
    G_OPTION_ARG_NONE, &show_version,
    N_("Display version number"), NULL
  },
  {
    "debug", 'd', 0,
    G_OPTION_ARG_NONE, NULL,
    N_("Enable debug messages"), NULL
  },
  {
    "date", 'd', 0,
    G_OPTION_ARG_STRING, NULL,
    N_("Open calendar on the passed date"), NULL
  },
  {
    "uuid", 'u', 0,
    G_OPTION_ARG_STRING, NULL,
    N_("Open calendar showing the passed event"), NULL
  },
  { NULL }
};

static const GActionEntry gcal_app_entries[] = {
  { "sync",   gcal_application_sync },
  { "search", gcal_application_launch_search },
  { "about",  gcal_application_show_about },
  { "quit",   gcal_application_quit },
};

enum
{
  PROP_0,
  PROP_CLOCK,
  PROP_MANAGER,
  PROP_TIME_FORMAT,
  PROP_WEATHER_SERVICE,
  N_PROPS
};

static GParamSpec* properties[N_PROPS] = { NULL, };

static void
process_sources (GcalApplication *self)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *new_css_data = NULL;
  g_auto (GStrv) new_css_snippets = NULL;
  GList *sources, *l;
  ESource *source;
  GQuark color_id;
  GdkRGBA color;
  gint arr_length;
  gint i = 0;


  sources = gcal_manager_get_sources_connected (self->manager);
  arr_length = g_list_length (sources);
  new_css_snippets = g_new0 (gchar*, arr_length + 2);
  for (l = sources; l != NULL; l = g_list_next (l), i++)
    {
      g_autofree gchar* color_str = NULL;

      source = l->data;

      get_color_name_from_source (source, &color);
      color_str = gdk_rgba_to_string (&color);
      color_id = g_quark_from_string (color_str);

      new_css_snippets[i] = g_strdup_printf (CSS_TEMPLATE, color_id, color_str);
    }

  g_list_free (sources);

  new_css_data = g_strjoinv ("\n", new_css_snippets);

  gtk_css_provider_load_from_data (self->colors_provider, new_css_data, -1, &error);

  if (error)
    g_warning ("Error creating custom stylesheet. %s", error->message);
}

static void
load_css_provider (GcalApplication *self)
{
  g_autoptr (GFile) css_file = NULL;
  g_autofree gchar *theme_name = NULL;
  g_autofree gchar *theme_uri = NULL;

  /* Apply the CSS provider */
  self->provider = gtk_css_provider_new ();
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (self->provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

  /* Retrieve the theme name */
  g_object_get (gtk_settings_get_default (), "gtk-theme-name", &theme_name, NULL);
  theme_uri = g_strconcat ("resource:///org/gnome/calendar/theme/", theme_name, ".css", NULL);

  /* Try and load the CSS file */
  css_file = g_file_new_for_uri (theme_uri);

  if (g_file_query_exists (css_file, NULL))
    gtk_css_provider_load_from_file (self->provider, css_file, NULL);
  else
    gtk_css_provider_load_from_resource (self->provider, "/org/gnome/calendar/theme/Adwaita.css");
}

static void
load_time_format (GcalApplication *self)
{
  g_autofree gchar *clock_format = NULL;
  g_autofree gchar *enum_format = NULL;

  clock_format = g_settings_get_string (self->desktop_settings, "clock-format");

  if (g_strcmp0 (clock_format, "12h") == 0)
    self->time_format = GCAL_TIME_FORMAT_12H;
  else
    self->time_format = GCAL_TIME_FORMAT_24H;

  enum_format = g_enum_to_string (GCAL_TYPE_TIME_FORMAT, self->time_format);
  g_debug ("Setting time format to %s", enum_format);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_FORMAT]);
}


/*
 * GObject overrides
 */

static void
gcal_application_finalize (GObject *object)
{
 GcalApplication *self = GCAL_APPLICATION (object);

  GCAL_ENTRY;

  g_clear_pointer (&self->initial_date, g_free);
  g_clear_pointer (&self->uuid, g_free);
  g_clear_object (&self->colors_provider);
  g_clear_object (&self->clock);
  g_clear_object (&self->desktop_settings);
  g_clear_object (&self->manager);
  g_clear_object (&self->night_light_monitor);
  g_clear_object (&self->provider);
  g_clear_object (&self->search_provider);
  g_clear_object (&self->weather_service);

  G_OBJECT_CLASS (gcal_application_parent_class)->finalize (object);

  GCAL_EXIT;
}

static void
gcal_application_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GcalApplication *self = GCAL_APPLICATION (object);

  switch (property_id)
    {
    case PROP_CLOCK:
      g_value_set_object (value, self->clock);
      break;

    case PROP_MANAGER:
      g_value_set_object (value, self->manager);
      break;

    case PROP_TIME_FORMAT:
      g_value_set_enum (value, self->time_format);
      break;

    case PROP_WEATHER_SERVICE:
      g_value_set_object (value, self->weather_service);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

/*
 * GApplication overrides
 */

static void
gcal_application_activate (GApplication *application)
{
  GcalApplication *self;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (application);

  if (!self->provider)
    load_css_provider (self);

  if (self->colors_provider)
    {
      gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                 GTK_STYLE_PROVIDER (self->colors_provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    }

  if (!self->night_light_monitor)
    self->night_light_monitor = gcal_night_light_monitor_new (self->manager);

  if (!self->window)
    {
      if (!self->initial_date)
        {
          icaltimezone *tz;

          tz = gcal_manager_get_system_timezone (self->manager);

          self->initial_date = g_new0 (icaltimetype, 1);
          *self->initial_date = icaltime_current_time_with_zone (tz);
          *self->initial_date = icaltime_set_timezone (self->initial_date, tz);
        }

      self->window =  g_object_new (GCAL_TYPE_WINDOW,
                                    "application", self,
                                    "manager", self->manager,
                                    "active-date", self->initial_date,
                                    "weather-service", self->weather_service,
                                    "time-format", self->time_format,
                                    NULL);

      g_object_bind_property (self, "time-format", self->window, "time-format", G_BINDING_DEFAULT);

      g_signal_connect (self->window, "destroy", G_CALLBACK (gtk_widget_destroyed), &self->window);
      gtk_widget_show (self->window);
    }

  gtk_window_present (GTK_WINDOW (self->window));

  if (self->initial_date)
    g_object_set (self->window, "active-date", self->initial_date, NULL);

  g_clear_pointer (&self->initial_date, g_free);

  if (self->uuid != NULL)
    {
      gcal_window_open_event_by_uuid (GCAL_WINDOW (self->window), self->uuid);
      g_clear_pointer (&self->uuid, g_free);
    }

  GCAL_EXIT;
}

static void
gcal_application_startup (GApplication *app)
{
  GcalApplication *self;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (app);

  /* add actions */
  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   gcal_app_entries,
                                   G_N_ELEMENTS (gcal_app_entries),
                                   app);

  G_APPLICATION_CLASS (gcal_application_parent_class)->startup (app);

  self->colors_provider = gtk_css_provider_new ();
  self->weather_service = gcal_weather_service_new ();

  /* Time format */
  self->desktop_settings = g_settings_new ("org.gnome.desktop.interface");
  g_signal_connect_swapped (self->desktop_settings,
                            "changed::clock-format",
                            G_CALLBACK (load_time_format),
                            self);
  load_time_format (self);

  /* Startup the manager */
  gcal_manager_startup (self->manager);

  /* We're assuming the application is called as a service only by the shell search system */
  if ((g_application_get_flags (app) & G_APPLICATION_IS_SERVICE) != 0)
    g_application_set_inactivity_timeout (app, 3 * 60 * 1000);

  GCAL_EXIT;
}

static gint
gcal_application_command_line (GApplication            *app,
                               GApplicationCommandLine *command_line)
{
  GcalApplication *self;
  GVariantDict *options;
  GVariant *option;
  const gchar* date = NULL;
  const gchar* uuid = NULL;
  gsize length;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (app);
  options = g_application_command_line_get_options_dict (command_line);

  if (g_variant_dict_contains (options, "quit"))
    {
      g_application_quit (app);
      GCAL_RETURN (0);
    }

  if (g_variant_dict_contains (options, "uuid"))
    {
      option = g_variant_dict_lookup_value (options, "uuid", G_VARIANT_TYPE_STRING);
      uuid = g_variant_get_string (option, &length);

      gcal_application_set_uuid (GCAL_APPLICATION (app), uuid);

      g_variant_unref (option);
    }
  else if (g_variant_dict_contains (options, "date"))
    {
      struct tm result;

      option = g_variant_dict_lookup_value (options, "date", G_VARIANT_TYPE_STRING);
      date = g_variant_get_string (option, &length);

      if (e_time_parse_date_and_time (date, &result) == E_TIME_PARSE_OK)
        {
          if (!self->initial_date)
            self->initial_date = g_new0 (icaltimetype, 1);

          *self->initial_date = tm_to_icaltimetype (&result, FALSE);
          *self->initial_date = icaltime_set_timezone (self->initial_date,
                                                       gcal_manager_get_system_timezone (self->manager));
        }

      g_variant_unref (option);
    }

  g_application_activate (app);

  GCAL_RETURN (0);
}

static gint
gcal_application_handle_local_options (GApplication *app,
                                       GVariantDict *options)
{
  /* Initialize logging before anything else */
  if (g_variant_dict_contains (options, "debug"))
    gcal_log_init ();

  if (show_version)
    {
      g_print ("gnome-calendar: Version %s\n", PACKAGE_VERSION);
      return 0;
    }

  return -1;
}

static gboolean
gcal_application_dbus_register (GApplication    *application,
                                GDBusConnection *connection,
                                const gchar     *object_path,
                                GError         **error)
{
  GcalApplication *self;
  g_autofree gchar *search_provider_path = NULL;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (application);

  if (!G_APPLICATION_CLASS (gcal_application_parent_class)->dbus_register (application, connection, object_path, error))
    GCAL_RETURN (FALSE);

  search_provider_path = g_strconcat (object_path, "/SearchProvider", NULL);

  if (!gcal_shell_search_provider_dbus_export (self->search_provider, connection, search_provider_path, error))
    GCAL_RETURN (FALSE);

  GCAL_RETURN (TRUE);
}

static void
gcal_application_dbus_unregister (GApplication    *application,
                                  GDBusConnection *connection,
                                  const gchar     *object_path)
{
  GcalApplication *self;
  g_autofree gchar *search_provider_path = NULL;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (application);

  search_provider_path = g_strconcat (object_path, "/SearchProvider", NULL);
  gcal_shell_search_provider_dbus_unexport (self->search_provider, connection, search_provider_path);

  G_APPLICATION_CLASS (gcal_application_parent_class)->dbus_unregister (application, connection, object_path);

  GCAL_EXIT;
}

static void
gcal_application_class_init (GcalApplicationClass *klass)
{
  GObjectClass *object_class;
  GApplicationClass *application_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gcal_application_finalize;
  object_class->get_property = gcal_application_get_property;

  application_class = G_APPLICATION_CLASS (klass);
  application_class->activate = gcal_application_activate;
  application_class->startup = gcal_application_startup;
  application_class->command_line = gcal_application_command_line;
  application_class->handle_local_options = gcal_application_handle_local_options;
  application_class->dbus_register = gcal_application_dbus_register;
  application_class->dbus_unregister = gcal_application_dbus_unregister;

  properties[PROP_CLOCK] = g_param_spec_object ("clock",
                                                "Clock",
                                                "The internal clock of Calendar",
                                                GCAL_TYPE_CLOCK,
                                                G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_MANAGER] = g_param_spec_object ("manager",
                                                  "The manager object",
                                                  "The manager object",
                                                  GCAL_TYPE_MANAGER,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_TIME_FORMAT] = g_param_spec_enum ("time-format",
                                                    "The time format of the computer",
                                                    "The time format of the computer",
                                                    GCAL_TYPE_TIME_FORMAT,
                                                    GCAL_TIME_FORMAT_24H,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_WEATHER_SERVICE] = g_param_spec_object ("weather-service",
                                                          "The weather service object",
                                                          "The weather service object",
                                                          GCAL_TYPE_WEATHER_SERVICE,
                                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gcal_application_init (GcalApplication *self)
{
  g_application_add_main_option_entries (G_APPLICATION (self), gcal_application_goptions);

  self->clock = gcal_clock_new ();
  self->manager = gcal_manager_new ();
  g_signal_connect_swapped (self->manager, "source-added", G_CALLBACK (process_sources), self);
  g_signal_connect_swapped (self->manager, "source-changed", G_CALLBACK (process_sources), self);

  self->search_provider = gcal_shell_search_provider_new (self->manager);
}

static void
gcal_application_sync (GSimpleAction *sync,
                       GVariant      *parameter,
                       gpointer       app)
{
  GcalApplication *self = GCAL_APPLICATION (app);
  gcal_manager_refresh (self->manager);
  gcal_weather_service_update (self->weather_service);
}

static void
gcal_application_launch_search (GSimpleAction *search,
                                GVariant      *parameter,
                                gpointer       app)
{
  GcalApplication *self = GCAL_APPLICATION (app);
  gcal_window_set_search_mode (GCAL_WINDOW (self->window), TRUE);
}

static void
on_about_response (GtkAboutDialog *about,
                   int             response_id,
                   gpointer        user_data)
{
  g_return_if_fail (GTK_IS_ABOUT_DIALOG (about));
  g_return_if_fail (user_data == NULL);

  if (response_id == GTK_RESPONSE_CANCEL)
    gtk_widget_destroy (GTK_WIDGET (about));
}

static gchar*
build_about_copyright (GcalApplication *self)
{
  g_autoptr (GDateTime) dt = NULL;
  const gchar *attribution;
  GString *builder;

  builder = g_string_new ("<span size=\"small\">");
  dt = g_date_time_new_now_local ();

  /* Build string: */
  g_string_append_printf (builder,
                         _("Copyright \xC2\xA9 2012\xE2\x80\x93%d " "The Calendar authors"),
                          g_date_time_get_year (dt));

  attribution = gcal_weather_service_get_attribution (self->weather_service);
  if (attribution)
    {
      g_string_append_c (builder, '\n');
      g_string_append (builder, attribution);
    }
  g_string_append (builder, "</span>");

  return g_string_free (builder, FALSE);
}

static void
gcal_application_show_about (GSimpleAction *simple,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  GcalApplication *self;
  GtkWidget *dialog;
  GtkLabel *copyright_label;
  g_autofree gchar *copyright = NULL;

  const gchar *authors[] = {
    "Erick Pérez Castellanos <erickpc@gnome.org>",
    "Georges Basile Stavracas Neto <georges.stavracas@gmail.com>",
    "Isaque Galdino <igaldino@gmail.com>",
    NULL
  };
  const gchar *artists[] = {
    "Jakub Steiner <jimmac@gmail.com>",
    "Lapo Calamandrei <calamandrei@gmail.com>",
    "Reda Lazri <the.red.shortcut@gmail.com>",
    "William Jon McCann <jmccann@redhat.com>",
    NULL
  };


  self = GCAL_APPLICATION (user_data);

  dialog = g_object_new (GTK_TYPE_ABOUT_DIALOG,
                         "transient-for", GTK_WINDOW (self->window),
                         "modal", TRUE,
                         "destroy-with-parent", TRUE,
                         "program-name", _("Calendar"),
                         "version", VERSION,
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "authors", authors,
                         "artists", artists,
                         "logo-icon-name", "org.gnome.Calendar",
                         "translator-credits", _("translator-credits"),
                         NULL);

  copyright = build_about_copyright (self);
  copyright_label = GTK_LABEL (gtk_widget_get_template_child (GTK_WIDGET (dialog),
                                                              GTK_TYPE_ABOUT_DIALOG,
                                                              "copyright_label"));
  gtk_label_set_markup (copyright_label, copyright);
  gtk_widget_show (GTK_WIDGET (copyright_label));

  g_signal_connect (dialog, "response", G_CALLBACK (on_about_response), NULL);
  gtk_widget_show (dialog);
}

static void
gcal_application_quit (GSimpleAction *simple,
                       GVariant      *parameter,
                       gpointer       user_data)
{
  GcalApplication *self = GCAL_APPLICATION (user_data);

  gtk_widget_destroy (self->window);
}

/* Public API */
GcalApplication*
gcal_application_new (void)
{
  return g_object_new (gcal_application_get_type (),
                       "resource-base-path", "/org/gnome/calendar",
                       "application-id", "org.gnome.Calendar",
                       "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                       NULL);
}

/**
 * gcal_application_get_manager:
 * @self: a #GcalApplication
 *
 * Retrieves the #GcalManager of the application.
 *
 * Returns: (transfer none): a #GcalManager
 */
GcalManager*
gcal_application_get_manager (GcalApplication *self)
{
  g_return_val_if_fail (GCAL_IS_APPLICATION (self), NULL);

  return self->manager;
}

/**
 * gcal_application_get_weather_service:
 * @self: A #GcalApplication
 *
 * Provides the #GcalWeatherService used by this application.
 *
 * Returns: (transfer none): A #GcalWeatehrService
 */
GcalWeatherService*
gcal_application_get_weather_service (GcalApplication *self)
{
  g_return_val_if_fail (GCAL_IS_APPLICATION (self), NULL);

  return self->weather_service;
}

void
gcal_application_set_uuid (GcalApplication *self,
                           const gchar     *app_uuid)
{
  g_return_if_fail (GCAL_IS_APPLICATION (self));

  g_free (self->uuid);
  self->uuid = g_strdup (app_uuid);
}

void
gcal_application_set_initial_date (GcalApplication *self,
                                   GDateTime       *initial_date)
{
  g_return_if_fail (GCAL_IS_APPLICATION (self));

  g_clear_pointer (&self->initial_date, g_free);
  self->initial_date = datetime_to_icaltime (initial_date);
}

GcalClock*
gcal_application_get_clock (GcalApplication *self)
{
  g_return_val_if_fail (GCAL_IS_APPLICATION (self), NULL);

  return self->clock;
}
