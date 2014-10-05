/* -*- mode: c; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * gcal-view.c
 *
 * Copyright (C) 2012 - Erick Pérez Castellanos
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gcal-view.h"
#include "gcal-utils.h"

#include <glib.h>

static void
gcal_view_base_init (gpointer g_iface)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      /* create interface signals && properties here. */
      g_object_interface_install_property (
          g_iface,
          g_param_spec_boxed ("active-date",
                              "The active date",
                              "The active/selecetd date in the view",
                              ICAL_TIME_TYPE,
                              G_PARAM_READWRITE));

      g_signal_new ("create-event",
                    GCAL_TYPE_VIEW,
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GcalViewIface,
                                     create_event),
                    NULL, NULL, NULL,
                    G_TYPE_NONE,
                    4,
                    G_TYPE_POINTER,
                    G_TYPE_POINTER,
                    G_TYPE_DOUBLE,
                    G_TYPE_DOUBLE);

      initialized = TRUE;
    }
}

GType
gcal_view_get_type (void)
{
  static GType type = 0;
  if (type == 0)
    {
      const GTypeInfo info =
      {
        sizeof (GcalViewIface),
        gcal_view_base_init,   /* base_init */
        NULL,   /* base_finalize */
        NULL,   /* class_init */
        NULL,   /* class_finalize */
        NULL,   /* class_data */
        0,
        0,      /* n_preallocs */
        NULL    /* instance_init */
      };
      type = g_type_register_static (G_TYPE_INTERFACE,
                                     "GcalView",
                                     &info,
                                     0);
      g_type_interface_add_prerequisite (type,
                                        G_TYPE_OBJECT);
    }
  return type;
}

void
gcal_view_set_date (GcalView     *view,
                    icaltimetype *date)
{
  g_return_if_fail (GCAL_IS_VIEW (view));

  g_object_set (view, "active-date", date, NULL);
}

icaltimetype*
gcal_view_get_date (GcalView *view)
{
  icaltimetype *date;

  g_return_val_if_fail (GCAL_IS_VIEW (view), NULL);

  g_object_get (view, "active-date", &date, NULL);
  return date;
}

void
gcal_view_clear_selection (GcalView *view)
{
  g_return_if_fail (GCAL_IS_VIEW (view));
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->clear_selection);

  GCAL_VIEW_GET_INTERFACE (view)->clear_selection (view);
}

void
gcal_view_create_event_on_current_unit (GcalView *view)
{
  g_return_if_fail (GCAL_IS_VIEW (view));
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->create_event_on_current_unit);

  GCAL_VIEW_GET_INTERFACE (view)->create_event_on_current_unit (view);
}

/* New API */
/**
 * gcal_view_get_initial_date:
 * @view: a #GcalView
 *
 * Return the initial date represented by the view.
 *
 * Returns: (transfer full): An #icaltimetype object
 **/
icaltimetype*
gcal_view_get_initial_date (GcalView *view)
{
  g_return_val_if_fail (GCAL_IS_VIEW (view), NULL);
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->get_initial_date);

  return GCAL_VIEW_GET_INTERFACE (view)->get_initial_date (view);
}

/**
 * gcal_view_get_final_date:
 * @view: a #GcalView
 *
 * Return the final date represented by the view.
 *
 * Returns: (transfer full): An #icaltimetype object
 **/
icaltimetype*
gcal_view_get_final_date (GcalView *view)
{
  g_return_val_if_fail (GCAL_IS_VIEW (view), NULL);
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->get_final_date);

  return GCAL_VIEW_GET_INTERFACE (view)->get_final_date (view);
}

/**
 * gcal_view_mark_current_unit:
 * @view: a #GcalView implementation
 * @x: the x position of the current unit. Returned by the view
 * @y: the y position of the current unit. Returned by the view
 *
 * Mark the current unit in the view as marked for event creation.
 * The (x, y) pair returned is the center of the cell
 **/
void
gcal_view_mark_current_unit (GcalView *view,
                             gint     *x,
                             gint     *y)
{
  g_return_if_fail (GCAL_IS_VIEW (view));
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->mark_current_unit);

  GCAL_VIEW_GET_INTERFACE (view)->mark_current_unit (view, x, y);
}

/**
 * gcal_view_clear_marks:
 * @view: a #GcalView
 *
 * Clear any marking the view had drawn
 **/
void
gcal_view_clear_marks (GcalView *view)
{
  g_return_if_fail (GCAL_IS_VIEW (view));
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->clear_marks);

  GCAL_VIEW_GET_INTERFACE (view)->clear_marks (view);
}

/**
 * gcal_view_get_left_header:
 * @view: a #GcalView
 *
 * Returns the string representing for the left header of the unit.
 * e.g.: for #GcalMonthView returns the month name
 *
 * Returns: (transfer full): a string.
 **/
gchar*
gcal_view_get_left_header (GcalView *view)
{
  g_return_val_if_fail (GCAL_IS_VIEW (view), NULL);
  g_return_val_if_fail (GCAL_VIEW_GET_INTERFACE (view)->get_left_header, NULL);

  return GCAL_VIEW_GET_INTERFACE (view)->get_left_header (view);
}

/**
 * gcal_view_get_right_header:
 * @view: a #GcalView
 *
 * Returns the string representing for the right header of the unit.
 * e.g.: for #GcalMonthView returns the year
 *
 * Returns: (transfer full): a string.
 **/
gchar*
gcal_view_get_right_header (GcalView *view)
{
  g_return_val_if_fail (GCAL_IS_VIEW (view), NULL);
  g_return_val_if_fail (GCAL_VIEW_GET_INTERFACE (view)->get_right_header, NULL);

  return GCAL_VIEW_GET_INTERFACE (view)->get_right_header (view);
}

/**
 * gcal_view_draw_event:
 * @view: a #GcalView
 * @start_date: an #icaltimetype object. The starting date of the event
 * @end_date: an #icaltimetype object. The edn-date of the event
 *
 * Whether the event within these boundaries will be drawn by the #GcalView implementation
 *
 * Returns: %TRUE if it is, %FALSE otherwise
 **/
gboolean
gcal_view_draw_event (GcalView     *view,
                      icaltimetype *start_date,
                      icaltimetype *end_date)
{
  g_return_val_if_fail (GCAL_IS_VIEW (view), FALSE);
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->draw_event);

  return GCAL_VIEW_GET_INTERFACE (view)->draw_event (view,
                                                     start_date,
                                                     end_date);
}

/**
 * gcal_view_get_by_uuid:
 * @view: a #GcalView
 * @uuid: the uuid of the event-widget
 *
 * The uuid is formed by source-uid:event-uid, and the widget with
 * this uuid is fetched by this method. If there's no widget with this
 * uuid in the view %NULL is returned.
 *
 * Returns: a #GcalEventWidget as #GtkWidget, %NULL otherwise
 **/
GtkWidget*
gcal_view_get_by_uuid (GcalView    *view,
                       const gchar *uuid)
{
  g_return_val_if_fail (GCAL_IS_VIEW (view), NULL);
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->get_by_uuid);

  return GCAL_VIEW_GET_INTERFACE (view)->get_by_uuid (view, uuid);
}

/**
 * gcal_view_clear:
 * @view: a #GcalView object
 *
 * Destroy every children inside the view.
 *
 * Note: this is a hack because of not-working container code
 * for day and week view composited widgets.
 **/
void
gcal_view_clear (GcalView *view)
{
  g_return_if_fail (GCAL_IS_VIEW (view));
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->clear);

  GCAL_VIEW_GET_INTERFACE (view)->clear (view);
}

/**
 * gcal_view_will_add_event:
 * @view: a #GcalView
 * @event: a #GcalEventWidget
 *
 * Check the ranges of dates and the existence of the widget in
 * in the view already
 *
 * Returns: %TRUE if the date of the widget fall inside the range and
 *          the widget isn't already on the view, %FALSE otherwise
 **/
gboolean
gcal_view_will_add_event (GcalView        *view,
                          GcalEventWidget *event)
{
  g_return_val_if_fail (GCAL_IS_VIEW (view), FALSE);
  g_return_if_fail (GCAL_VIEW_GET_INTERFACE (view)->will_add_event);

  return GCAL_VIEW_GET_INTERFACE (view)->will_add_event (view,
                                                         event);
}
