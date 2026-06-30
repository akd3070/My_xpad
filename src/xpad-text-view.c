/*

Copyright (c) 2001-2007 Michael Terry
Copyright (c) 2013-2014 Arthur Borsboom

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "../config.h"

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <math.h>

#include "xpad-text-view.h"
#include "xpad-text-buffer.h"
#include "xpad-pad.h"
#include "xpad-toolbar.h"

struct XpadTextViewPrivate
{
	gboolean follow_font_style;
	gboolean follow_color_style;
	gulong notify_text_handler;
	gulong notify_back_handler;
	gulong notify_font_handler;
	GtkCssProvider *font_provider;
	XpadTextBuffer *buffer;
	XpadSettings *settings;
	XpadPad *pad;
};

G_DEFINE_TYPE_WITH_PRIVATE (XpadTextView, xpad_text_view, GTK_SOURCE_TYPE_VIEW)

static void xpad_text_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void xpad_text_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void xpad_text_view_constructed (GObject *object);
static void xpad_text_view_dispose (GObject *object);
static void xpad_text_view_finalize (GObject *object);
static void xpad_text_view_realize (XpadTextView *widget);
static gboolean xpad_text_view_button_press_event (GtkWidget *widget, GdkEventButton *event, XpadSettings *settings);
static gboolean xpad_text_view_focus_out_event (GtkWidget *widget, GdkEventFocus *event, XpadSettings *settings);
static void xpad_text_view_notify_edit_lock (XpadTextView *view);
static void xpad_text_view_notify_editable (XpadTextView *view);
static void xpad_text_view_notify_fontname (XpadTextView *view);
static void xpad_text_view_notify_colors (XpadTextView *view);
static void xpad_text_view_notify_line_numbering (XpadTextView *view);
static gboolean xpad_text_view_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static gboolean xpad_text_view_checklist_click (GtkWidget *widget, GdkEventButton *event, gpointer user_data);

enum
{
	PROP_0,
	PROP_SETTINGS,
	PROP_PAD,
	PROP_FOLLOW_FONT_STYLE,
	PROP_FOLLOW_COLOR_STYLE,
	N_PROPERTIES
};

static GParamSpec *obj_prop[N_PROPERTIES] = { NULL, };

GtkWidget *
xpad_text_view_new (XpadSettings *settings, XpadPad *pad)
{
	return GTK_WIDGET (g_object_new (XPAD_TYPE_TEXT_VIEW, "settings", settings, "pad", pad, "follow-font-style", TRUE, "follow-color-style", TRUE, NULL));
}

static void
draw_task_glass_tile (GtkTextView *text_view, cairo_t *cr, gint start_y, gint end_y, gint width)
{
	/* Adjust coordinates to account for new 16px margins */
	gdouble x = 6.0;
	gdouble y = start_y; 
	gdouble w = width - 12.0;
	gdouble h = (end_y - start_y);
	gdouble radius = 10.0; /* More rounded corners */

	if (h <= 0 || w <= 0) return;

	GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (text_view));
	GdkRGBA color;
	gtk_style_context_get_color (context, gtk_style_context_get_state (context), &color);
	gboolean is_dark = (color.red + color.green + color.blue > 1.5);

	cairo_save (cr);

	/* Soft Multi-Layer Drop Shadow for realistic blur */
	for (int i = 1; i <= 4; i++) {
		if (is_dark) {
			cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.06 / i);
		} else {
			cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.03 / i);
		}
		gdouble sx = x - i + 1;
		gdouble sy = y + 2 - i + 1;
		gdouble sw = w + (i * 2) - 2;
		gdouble sh = h + (i * 2) - 2;
		cairo_move_to (cr, sx + radius, sy);
		cairo_arc (cr, sx + sw - radius, sy + radius, radius, -M_PI/2, 0);
		cairo_arc (cr, sx + sw - radius, sy + sh - radius, radius, 0, M_PI/2);
		cairo_arc (cr, sx + radius, sy + sh - radius, radius, M_PI/2, M_PI);
		cairo_arc (cr, sx + radius, sy + radius, radius, M_PI, 3*M_PI/2);
		cairo_fill (cr);
	}

	/* Glass tile background */
	if (is_dark) {
		/* White frosted glass for dark themes */
		cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.08);
	} else {
		/* Clean, bright white card for light themes to pop against grey backgrounds */
		cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.75);
	}

	cairo_move_to (cr, x + radius, y);
	cairo_arc (cr, x + w - radius, y + radius, radius, -M_PI/2, 0);
	cairo_arc (cr, x + w - radius, y + h - radius, radius, 0, M_PI/2);
	cairo_arc (cr, x + radius, y + h - radius, radius, M_PI/2, M_PI);
	cairo_arc (cr, x + radius, y + radius, radius, M_PI, 3*M_PI/2);
	cairo_fill_preserve (cr);

	/* Outer Border */
	if (is_dark) {
		cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.15);
	} else {
		cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.10);
	}
	cairo_set_line_width (cr, 1.0);
	cairo_stroke (cr);

	/* Inner top highlight for extra glass shine */
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, is_dark ? 0.10 : 0.8);
	cairo_move_to (cr, x + radius, y + 1.0);
	cairo_line_to (cr, x + w - radius, y + 1.0);
	cairo_stroke (cr);

	cairo_restore (cr);
}

static void
xpad_text_view_draw_layer (GtkTextView *text_view, GtkTextViewLayer layer, cairo_t *cr)
{
	/* Chain up to parent */
	GtkTextViewClass *parent_class = GTK_TEXT_VIEW_CLASS (xpad_text_view_parent_class);
	if (parent_class->draw_layer)
		parent_class->draw_layer (text_view, layer, cr);

	if (layer != GTK_TEXT_VIEW_LAYER_BELOW_TEXT)
		return;

	GtkTextBuffer *tb = gtk_text_view_get_buffer (text_view);
	if (!XPAD_IS_TEXT_BUFFER (tb))
		return;

	XpadTextBuffer *buffer = XPAD_TEXT_BUFFER (tb);

	GdkRectangle visible_rect;
	gtk_text_view_get_visible_rect (text_view, &visible_rect);

	GtkTextIter iter;
	gtk_text_view_get_line_at_y (text_view, &iter, visible_rect.y, NULL);

	gint start_y = -1, end_y = -1;
	gboolean in_task_group = FALSE;

	while (!gtk_text_iter_is_end (&iter)) {
		gint line_num = gtk_text_iter_get_line (&iter);
		gint y, height;
		gtk_text_view_get_line_yrange (text_view, &iter, &y, &height);

		if (y > visible_rect.y + visible_rect.height)
			break;

		int task_type = xpad_text_buffer_get_task_type_at_line (buffer, line_num);
		
		if (task_type > 0) {
			if (!in_task_group) {
				in_task_group = TRUE;
				start_y = y;
			}
			end_y = y + height;
		} else {
			if (in_task_group) {
				draw_task_glass_tile (text_view, cr, start_y, end_y, visible_rect.width);
				in_task_group = FALSE;
			}
		}
		
		if (!gtk_text_iter_forward_line (&iter))
			break;
	}

	if (in_task_group) {
		draw_task_glass_tile (text_view, cr, start_y, end_y, visible_rect.width);
	}
}

static void
xpad_text_view_class_init (XpadTextViewClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	GtkTextViewClass *textview_class = GTK_TEXT_VIEW_CLASS (klass);

	gobject_class->constructed = xpad_text_view_constructed;
	gobject_class->dispose = xpad_text_view_dispose;
	gobject_class->finalize = xpad_text_view_finalize;
	gobject_class->set_property = xpad_text_view_set_property;
	gobject_class->get_property = xpad_text_view_get_property;

	textview_class->draw_layer = xpad_text_view_draw_layer;

	obj_prop[PROP_SETTINGS] = g_param_spec_pointer ("settings", "Xpad settings", "Xpad global settings", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	obj_prop[PROP_PAD] = g_param_spec_pointer ("pad", "Pad", "Pad connected to this textview", G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	obj_prop[PROP_FOLLOW_FONT_STYLE] = g_param_spec_boolean ("follow-font-style", "Follow font style", "Whether to use the default xpad font style", TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	obj_prop[PROP_FOLLOW_COLOR_STYLE] = g_param_spec_boolean ("follow-color-style", "Follow color style", "Whether to use the default xpad color style", TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

	g_object_class_install_properties (gobject_class, N_PROPERTIES, obj_prop);
}

static void
xpad_text_view_init (XpadTextView *view)
{
	view->priv = xpad_text_view_get_instance_private (view);
}

static void
xpad_text_view_constructed (GObject *object)
{
	XpadTextView *view = XPAD_TEXT_VIEW (object);

	view->priv->buffer = xpad_text_buffer_new (view->priv->pad);

	gtk_text_view_set_buffer (GTK_TEXT_VIEW (view), GTK_TEXT_BUFFER (view->priv->buffer));
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD);
	gtk_text_view_set_top_margin (GTK_TEXT_VIEW (view), 8);
	gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (view), 8);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (view), 16);
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (view), 16);

	gchar *widget_name = g_strdup_printf ("%p", (void *) view);
	gtk_widget_set_name (GTK_WIDGET (view), widget_name);
	g_free (widget_name);
	xpad_text_view_notify_line_numbering(view);

	/* Hide the GtkSourceView gutter (marks margin) to remove the white side bar */
	gtk_source_view_set_show_line_marks (GTK_SOURCE_VIEW (view), FALSE);

	/* Add CSS style class, so the styling can be overridden by a GTK theme */
	GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET (view));
	gtk_style_context_add_class(context, "XpadTextView");

	/* Signals */
	g_signal_connect (view, "button-press-event", G_CALLBACK (xpad_text_view_button_press_event), view->priv->settings);
	g_signal_connect_after (view, "focus-out-event", G_CALLBACK (xpad_text_view_focus_out_event), view->priv->settings);
	g_signal_connect (view, "realize", G_CALLBACK (xpad_text_view_realize), NULL);
	g_signal_connect (view, "notify::editable", G_CALLBACK (xpad_text_view_notify_editable), NULL);
	g_signal_connect_swapped (view->priv->settings, "notify::edit-lock", G_CALLBACK (xpad_text_view_notify_edit_lock), view);
	g_signal_connect_swapped (view->priv->settings, "notify::line-numbering", G_CALLBACK (xpad_text_view_notify_line_numbering), view);

	view->priv->notify_font_handler = g_signal_connect_swapped (view->priv->settings, "notify::fontname", G_CALLBACK (xpad_text_view_notify_fontname), view);
	view->priv->notify_text_handler = g_signal_connect_swapped (view->priv->settings, "notify::text-color", G_CALLBACK (xpad_text_view_notify_colors), view);
	view->priv->notify_back_handler = g_signal_connect_swapped (view->priv->settings, "notify::back-color", G_CALLBACK (xpad_text_view_notify_colors), view);

	/* Checklist signal handlers */
	g_signal_connect (view, "key-press-event", G_CALLBACK (xpad_text_view_key_press_event), NULL);
	g_signal_connect (view, "button-press-event", G_CALLBACK (xpad_text_view_checklist_click), NULL);

	g_signal_handler_block (view->priv->settings, view->priv->notify_font_handler);
}

static void
xpad_text_view_dispose (GObject *object)
{
	XpadTextView *view = XPAD_TEXT_VIEW (object);

	g_clear_object (&view->priv->buffer);
	g_clear_object (&view->priv->pad);
	g_clear_object (&view->priv->settings);
	g_clear_object (&view->priv->font_provider);

	G_OBJECT_CLASS (xpad_text_view_parent_class)->dispose (object);
}

static void
xpad_text_view_finalize (GObject *object)
{
	XpadTextView *view = XPAD_TEXT_VIEW (object);

	if (view->priv->settings)
		g_signal_handlers_disconnect_matched (view->priv->settings, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, view);

	G_OBJECT_CLASS (xpad_text_view_parent_class)->finalize (object);
}

static void
xpad_text_view_realize (XpadTextView *view)
{
	gboolean edit_lock;
	g_object_get (view->priv->settings, "edit-lock", &edit_lock, NULL);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (view), !edit_lock);
}

static void
xpad_text_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	XpadTextView *view = XPAD_TEXT_VIEW (object);

	switch (prop_id)
	{
	case PROP_SETTINGS:
		view->priv->settings = g_value_get_pointer (value);
		g_object_ref (view->priv->settings);
		break;

	case PROP_PAD:
		view->priv->pad = g_value_get_pointer (value);
		g_object_ref (view->priv->pad);
		break;

	case PROP_FOLLOW_FONT_STYLE:
		view->priv->follow_font_style = g_value_get_boolean (value);
		if (view->priv->follow_font_style) {
			xpad_text_view_notify_fontname (view);
			if (view->priv->notify_font_handler != 0) {
				g_signal_handler_unblock (view->priv->settings, view->priv->notify_font_handler);
			}
		}
		else
			g_signal_handler_block (view->priv->settings, view->priv->notify_font_handler);
		break;

	case PROP_FOLLOW_COLOR_STYLE:
		view->priv->follow_color_style = g_value_get_boolean (value);
		xpad_text_view_notify_colors (view);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
xpad_text_view_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	XpadTextView *view = XPAD_TEXT_VIEW (object);

	switch (prop_id)
	{
	case PROP_SETTINGS:
		g_value_set_pointer (value, view->priv->settings);
		break;

	case PROP_PAD:
		g_value_set_pointer (value, view->priv->pad);
		break;

	case PROP_FOLLOW_FONT_STYLE:
		g_value_set_boolean (value, view->priv->follow_font_style);
		break;

	case PROP_FOLLOW_COLOR_STYLE:
		g_value_set_boolean (value, view->priv->follow_color_style);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
xpad_text_view_focus_out_event (GtkWidget *widget, GdkEventFocus *event, XpadSettings *settings)
{
	/* A dirty way to silence the compiler for these unused variables. */
	(void) event;

	gboolean edit_lock;
	g_object_get (settings, "edit-lock", &edit_lock, NULL);

	if (edit_lock)
	{
		gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), FALSE);
		return TRUE;
	}

	return FALSE;
}

static gboolean
xpad_text_view_button_press_event (GtkWidget *widget, GdkEventButton *event, XpadSettings *settings)
{
	gboolean edit_lock;
	g_object_get (settings, "edit-lock", &edit_lock, NULL);

	if (event->button == 1 &&
	    edit_lock &&
	    !gtk_text_view_get_editable (GTK_TEXT_VIEW (widget)))
	{
		if (event->type == GDK_2BUTTON_PRESS)
		{
			gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), TRUE);
			return TRUE;
		}
		else if (event->type == GDK_BUTTON_PRESS)
		{
			gtk_window_begin_move_drag (GTK_WINDOW (gtk_widget_get_toplevel (widget)), (gint) event->button, (gint) event->x_root, (gint) event->y_root, event->time);
			return TRUE;
		}
	}

	return FALSE;
}

static void
xpad_text_view_notify_edit_lock (XpadTextView *view)
{
	/* chances are good that they don't have the text view focused while it changed, so make non-editable if edit lock turned on */
	gboolean edit_lock;
	g_object_get (view->priv->settings, "edit-lock", &edit_lock, NULL);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (view), !edit_lock);
}

static void
xpad_text_view_notify_editable (XpadTextView *view)
{
	GdkCursor *cursor;
	gboolean editable;
	GdkDisplay *display;
	GdkWindow *view_window;
	GtkSourceView *view_tv = GTK_SOURCE_VIEW (view);

	editable = gtk_text_view_get_editable (GTK_TEXT_VIEW (view_tv));
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view_tv), editable);

	view_window = gtk_text_view_get_window (GTK_TEXT_VIEW (view_tv), GTK_TEXT_WINDOW_TEXT);
	display = gdk_window_get_display(view_window);
	cursor = editable ? gdk_cursor_new_for_display (display, GDK_XTERM) : NULL;

	/* Only set for pads which are currently visible */
	if (view_window != NULL)
		gdk_window_set_cursor (view_window, cursor);

	g_clear_object (&cursor);
}

static void
xpad_text_view_notify_fontname (XpadTextView *view)
{
	gchar *font;
	g_object_get (view->priv->settings, "fontname", &font, NULL);

	PangoFontDescription *fontdesc = font ? pango_font_description_from_string (font) : NULL;
	xpad_text_view_set_font (GTK_WIDGET (view), fontdesc);
	if (fontdesc)
		pango_font_description_free (fontdesc);

	g_free (font);
}

/* Update the colors of the textview */
static void
xpad_text_view_notify_colors (XpadTextView *view)
{
	if (view->priv->follow_color_style) {
		/* Set the colors of this individual pad to the global setting preference. */
		GdkRGBA *text_color, *back_color;

		GtkWidget *view_widget = GTK_WIDGET (view);

		/* Set the colors to the global preferences colors */
		g_object_get (view->priv->settings, "text-color", &text_color, "back-color", &back_color, NULL);
		xpad_text_view_set_colors(view_widget, text_color, back_color);
		gdk_rgba_free (text_color);
		gdk_rgba_free (back_color);
	}
}

/* Set the foreground and background color of the visible part of the pad, which is the text view */
void
xpad_text_view_set_colors (GtkWidget *view, GdkRGBA *text_color, GdkRGBA *back_color) {
	gchar *text_color_string = text_color ? gdk_rgba_to_string (text_color) : "@theme-fg_color";
	gchar *back_color_string = back_color ? gdk_rgba_to_string (back_color) : "@theme_bg_color";

	gchar *cssStyling = g_strconcat(
			"textview, textview text {caret-color: ", text_color_string,
			"; color: ", text_color_string,
			"; background-color: ", back_color_string,
			";}\n", NULL);

	if (text_color) {
		g_free (text_color_string);
	}

	if (back_color) {
		g_free (back_color_string);
	}

	/*
	 * TODO: If the background color is close to the text selection background color (blue-ish),
	 * then change the color for selected text by adding this to the CSS.
	 * */

	GtkStyleContext *context = gtk_widget_get_style_context (view);
	GtkCssProvider *provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (provider, cssStyling, -1, NULL);
	gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);

	g_free(cssStyling);
	g_clear_object (&provider);
}

/* Set the font of the pad, which is in the text view */
void
xpad_text_view_set_font (GtkWidget *view, PangoFontDescription *desc) {
	GtkStyleContext *context = gtk_widget_get_style_context (view);

	if (desc == NULL && XPAD_TEXT_VIEW (view)->priv->font_provider) {
		/* Remove font provider */
		gtk_style_context_remove_provider (context, GTK_STYLE_PROVIDER (XPAD_TEXT_VIEW (view)->priv->font_provider));
		g_clear_object (&XPAD_TEXT_VIEW (view)->priv->font_provider);
	} else {
		/* Add/replace font provider */
		gchar *font_description;
		gchar *cssStyling;

		font_description = pango_font_description_to_css(desc);
		cssStyling = g_strconcat("textview, textview text ", font_description, "\n", NULL);
		g_free (font_description);

		if (XPAD_TEXT_VIEW (view)->priv->font_provider) {
			gtk_style_context_remove_provider (context, GTK_STYLE_PROVIDER (XPAD_TEXT_VIEW (view)->priv->font_provider));
			g_clear_object (&XPAD_TEXT_VIEW (view)->priv->font_provider);
		}

		GtkCssProvider *provider = gtk_css_provider_new ();
		gtk_css_provider_load_from_data (provider, cssStyling, -1, NULL);
		gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
		XPAD_TEXT_VIEW (view)->priv->font_provider = provider;

		g_free (cssStyling);
	}
}

static void
xpad_text_view_notify_line_numbering (XpadTextView *view)
{
	gboolean line_numbering;
	g_object_get (view->priv->settings, "line-numbering", &line_numbering, NULL);
	gtk_source_view_set_show_line_numbers (GTK_SOURCE_VIEW (view), line_numbering);
}

/*
 * Checklist key-press-event handler:
 * - Enter on a task line: inserts a new task of the same type on the next line
 * - Tab on a top-level task: converts it to a nested sub-task
 * - Shift+Tab on a nested task: converts it back to top-level
 * - Backspace on an empty task line: removes the checkbox and exits task mode
 */
static gboolean
xpad_text_view_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	(void) user_data;

	XpadTextView *view = XPAD_TEXT_VIEW (widget);
	GtkTextBuffer *tb = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	XpadTextBuffer *buffer = XPAD_TEXT_BUFFER (tb);
	GtkTextMark *insert_mark;
	GtkTextIter cursor;
	gint line_num;
	int task_type;

	insert_mark = gtk_text_buffer_get_insert (tb);
	gtk_text_buffer_get_iter_at_mark (tb, &cursor, insert_mark);
	line_num = gtk_text_iter_get_line (&cursor);
	task_type = xpad_text_buffer_get_task_type_at_line (buffer, line_num);

	/* Only handle keys when we're on a task line */
	if (task_type == 0)
		return FALSE;

	/* --- ENTER: Create new task on the next line --- */
	if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
	{
		GtkTextIter line_start, line_end;
		gchar *line_text;
		const gchar *text_after_checkbox;
		gboolean is_empty_task = FALSE;

		gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
		line_end = line_start;
		gtk_text_iter_forward_to_line_end (&line_end);
		line_text = gtk_text_buffer_get_text (tb, &line_start, &line_end, FALSE);

		/* Check if the task line is empty (only checkbox + optional space) */
		text_after_checkbox = line_text;
		/* Skip leading spaces */
		while (*text_after_checkbox == ' ')
			text_after_checkbox++;
		/* Skip the checkbox character (3 bytes for UTF-8 encoded Unicode) */
		if (*text_after_checkbox)
			text_after_checkbox = g_utf8_next_char (text_after_checkbox);
		/* Skip space after checkbox */
		if (*text_after_checkbox == ' ')
			text_after_checkbox++;

		is_empty_task = (*text_after_checkbox == '\0');
		g_free (line_text);

		if (is_empty_task) {
			/* Empty task line: remove the checkbox and exit task mode */
			gtk_text_buffer_begin_user_action (tb);
			gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
			line_end = line_start;
			gtk_text_iter_forward_to_line_end (&line_end);
			gtk_text_buffer_delete (tb, &line_start, &line_end);
			gtk_text_buffer_end_user_action (tb);
			return TRUE;
		}

		/* Insert newline and new task of the same type */
		gtk_text_buffer_begin_user_action (tb);

		/* Move cursor to end of current line */
		gtk_text_buffer_get_iter_at_line (tb, &line_end, line_num);
		gtk_text_iter_forward_to_line_end (&line_end);
		gtk_text_buffer_place_cursor (tb, &line_end);

		/* Insert newline */
		gtk_text_buffer_insert_at_cursor (tb, "\n", -1);

		/* Insert checkbox of the same type */
		if (task_type == 1 || task_type == 2) {
			/* Top-level task */
			xpad_text_buffer_insert_task (buffer, FALSE);
		} else {
			/* Nested task: preserve indentation */
			gint indent = xpad_text_buffer_get_line_indent_spaces (buffer, line_num);
			gchar *spaces = g_strnfill ((gsize) indent, ' ');

			insert_mark = gtk_text_buffer_get_insert (tb);
			gtk_text_buffer_get_iter_at_mark (tb, &cursor, insert_mark);
			gtk_text_buffer_insert (tb, &cursor, spaces, -1);

			/* Insert circle checkbox with color tag */
			static const gchar *sub_unchecked = "\xe2\x97\x8b"; /* ○ */
			gtk_text_buffer_get_iter_at_mark (tb, &cursor, insert_mark);
			gint cb_off = gtk_text_iter_get_offset (&cursor);
			gtk_text_buffer_insert (tb, &cursor, sub_unchecked, -1);

			/* Apply pending color tag to the checkbox */
			GtkTextIter cb_s;
			gtk_text_buffer_get_iter_at_offset (tb, &cb_s, cb_off);
			gtk_text_buffer_get_iter_at_mark (tb, &cursor, insert_mark);
			gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-pending", &cb_s, &cursor);

			gtk_text_buffer_get_iter_at_mark (tb, &cursor, insert_mark);
			gtk_text_buffer_insert (tb, &cursor, " ", -1);

			g_free (spaces);
		}

		gtk_text_buffer_end_user_action (tb);
		return TRUE;
	}

	/* --- TAB: Convert top-level task to nested, or increase nesting --- */
	if (event->keyval == GDK_KEY_Tab && !(event->state & GDK_SHIFT_MASK))
	{
		GtkTextIter line_start, line_end, cb_iter, cb_end;
		static const gchar *sub_unchecked = "\xe2\x97\x8b"; /* ○ */
		static const gchar *sub_checked   = "\xe2\x97\x8f"; /* ● */

		gtk_text_buffer_begin_user_action (tb);

		gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
		line_end = line_start;
		gtk_text_iter_forward_to_line_end (&line_end);

		if (task_type == 1 || task_type == 2) {
			/* Top-level → nested: replace □/✓ with ○/● and add indentation */
			cb_iter = line_start;
			cb_end = cb_iter;
			gtk_text_iter_forward_char (&cb_end);

			/* Remove old color tags */
			gtk_text_buffer_remove_tag_by_name (tb, "task-checkbox-pending", &cb_iter, &cb_end);
			gtk_text_buffer_remove_tag_by_name (tb, "task-checkbox-done", &cb_iter, &cb_end);

			const gchar *new_char = (task_type == 1) ? sub_unchecked : sub_checked;
			gtk_text_buffer_delete (tb, &cb_iter, &cb_end);
			gtk_text_buffer_insert (tb, &cb_iter, new_char, -1);

			/* Add indentation at line start */
			gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
			gtk_text_buffer_insert (tb, &line_start, "  ", -1);

			/* Re-apply color tag to the new checkbox character */
			gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
			cb_iter = line_start;
			while (gtk_text_iter_get_char (&cb_iter) == ' ')
				gtk_text_iter_forward_char (&cb_iter);
			cb_end = cb_iter;
			gtk_text_iter_forward_char (&cb_end);
			if (task_type == 1)
				gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-pending", &cb_iter, &cb_end);
			else
				gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-done", &cb_iter, &cb_end);
		}
		else if (task_type == 3 || task_type == 4) {
			/* Already nested: add more indentation */
			gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
			gtk_text_buffer_insert (tb, &line_start, "  ", -1);
		}

		gtk_text_buffer_end_user_action (tb);
		return TRUE;
	}

	/* --- SHIFT+TAB: Un-nest a task --- */
	if (event->keyval == GDK_KEY_ISO_Left_Tab ||
	    (event->keyval == GDK_KEY_Tab && (event->state & GDK_SHIFT_MASK)))
	{
		static const gchar *task_unchecked = "\xe2\x98\x90"; /* ☐ */
		static const gchar *task_checked   = "\xe2\x9c\x94"; /* ✔ */

		if (task_type == 3 || task_type == 4) {
			gint indent = xpad_text_buffer_get_line_indent_spaces (buffer, line_num);
			GtkTextIter line_start, cb_iter, cb_end;

			gtk_text_buffer_begin_user_action (tb);

			if (indent <= 2) {
				/* Convert to top-level: remove all indentation and replace ○/● with □/✓ */
				gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);

				/* Remove leading spaces */
				cb_iter = line_start;
				while (gtk_text_iter_get_char (&cb_iter) == ' ')
					gtk_text_iter_forward_char (&cb_iter);

				/* Remove old color tags before deletion */
				cb_end = cb_iter;
				gtk_text_iter_forward_char (&cb_end);
				gtk_text_buffer_remove_tag_by_name (tb, "task-checkbox-pending", &cb_iter, &cb_end);
				gtk_text_buffer_remove_tag_by_name (tb, "task-checkbox-done", &cb_iter, &cb_end);

				/* Remove spaces */
				gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
				cb_iter = line_start;
				while (gtk_text_iter_get_char (&cb_iter) == ' ')
					gtk_text_iter_forward_char (&cb_iter);
				if (!gtk_text_iter_equal (&line_start, &cb_iter))
					gtk_text_buffer_delete (tb, &line_start, &cb_iter);

				/* Replace circle with square/check */
				gtk_text_buffer_get_iter_at_line (tb, &cb_iter, line_num);
				cb_end = cb_iter;
				gtk_text_iter_forward_char (&cb_end);

				const gchar *new_char = (task_type == 3) ? task_unchecked : task_checked;
				gtk_text_buffer_delete (tb, &cb_iter, &cb_end);
				gtk_text_buffer_insert (tb, &cb_iter, new_char, -1);

				/* Apply color tag to new checkbox */
				gtk_text_buffer_get_iter_at_line (tb, &cb_iter, line_num);
				cb_end = cb_iter;
				gtk_text_iter_forward_char (&cb_end);
				if (task_type == 3)
					gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-pending", &cb_iter, &cb_end);
				else
					gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-done", &cb_iter, &cb_end);
			} else {
				/* Reduce indentation by 2 spaces */
				gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
				cb_iter = line_start;
				gtk_text_iter_forward_chars (&cb_iter, 2);
				gtk_text_buffer_delete (tb, &line_start, &cb_iter);
			}

			gtk_text_buffer_end_user_action (tb);
			return TRUE;
		}
	}

	/* --- BACKSPACE: On empty task, remove checkbox --- */
	if (event->keyval == GDK_KEY_BackSpace)
	{
		GtkTextIter line_start, line_end;
		gchar *line_text;
		const gchar *text_after;
		gboolean is_cursor_at_task_end = FALSE;

		/* Check if cursor is right after the checkbox + space */
		gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
		line_end = line_start;
		gtk_text_iter_forward_to_line_end (&line_end);
		line_text = gtk_text_buffer_get_text (tb, &line_start, &line_end, FALSE);

		text_after = line_text;
		while (*text_after == ' ')
			text_after++;
		if (*text_after)
			text_after = g_utf8_next_char (text_after);
		if (*text_after == ' ')
			text_after++;

		/* If cursor is at the position right after checkbox+space and no text follows */
		gint cursor_offset = gtk_text_iter_get_line_offset (&cursor);
		gint prefix_len = (gint) (text_after - line_text);
		is_cursor_at_task_end = (cursor_offset == prefix_len && *text_after == '\0');

		g_free (line_text);

		if (is_cursor_at_task_end) {
			/* Remove the entire task prefix */
			gtk_text_buffer_begin_user_action (tb);
			gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
			line_end = line_start;
			gtk_text_iter_forward_to_line_end (&line_end);
			gtk_text_buffer_delete (tb, &line_start, &line_end);
			gtk_text_buffer_end_user_action (tb);
			return TRUE;
		}
	}

	return FALSE;
}

/*
 * Checklist click handler:
 * Detects clicks on checkbox characters and toggles their state.
 */
static gboolean
xpad_text_view_checklist_click (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	(void) user_data;

	XpadTextView *view = XPAD_TEXT_VIEW (widget);
	GtkTextView *text_view = GTK_TEXT_VIEW (view);
	GtkTextBuffer *tb = gtk_text_view_get_buffer (text_view);
	XpadTextBuffer *buffer = XPAD_TEXT_BUFFER (tb);
	GtkTextIter iter;
	gint x, y;
	gint line_num;

	if (event->type != GDK_BUTTON_PRESS || event->button != 1)
		return FALSE;

	/* Convert window coords to buffer coords */
	gtk_text_view_window_to_buffer_coords (text_view, GTK_TEXT_WINDOW_WIDGET,
		(gint) event->x, (gint) event->y, &x, &y);

	/* Get the text iter at the click position */
	gtk_text_view_get_iter_at_location (text_view, &iter, x, y);

	line_num = gtk_text_iter_get_line (&iter);

	int task_type = xpad_text_buffer_get_task_type_at_line (buffer, line_num);
	if (task_type == 0)
		return FALSE;

	/* Check if the click was on the checkbox character.
	 * The checkbox is at the start of the line (possibly after spaces for nested).
	 * We check if the clicked offset is within the checkbox area. */
	gint click_offset = gtk_text_iter_get_line_offset (&iter);
	gint indent = xpad_text_buffer_get_line_indent_spaces (buffer, line_num);

	/* Checkbox is at position 'indent' (0 for top-level, N for nested) */
	if (click_offset >= indent && click_offset <= indent + 1)
	{
		xpad_text_buffer_toggle_task_at_line (buffer, line_num);
		return TRUE;
	}

	return FALSE;
}

