/*

Copyright (c) 2001-2007 Michael Terry
Copyright (c) 2013-2014 Arthur Borsboom
Copyright (c) 2019 Siergiej Riaguzow

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

#include "xpad-text-buffer.h"
#include "xpad-pad.h"

struct XpadTextBufferPrivate
{
	XpadPad *pad;
};

G_DEFINE_TYPE_WITH_PRIVATE (XpadTextBuffer, xpad_text_buffer, GTK_SOURCE_TYPE_BUFFER)

/* Unicode chars in the Private Use Area. */
static gunichar TAG_CHAR = 0xe000;

static GtkTextTagTable *create_tag_table (void);

enum
{
	PROP_0,
	PROP_PAD,
	LAST_PROP
};

static void xpad_text_buffer_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void xpad_text_buffer_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void xpad_text_buffer_dispose (GObject *object);
static void xpad_text_buffer_finalize (GObject *object);

XpadTextBuffer *
xpad_text_buffer_new (XpadPad *pad)
{
	return g_object_new (XPAD_TYPE_TEXT_BUFFER, "tag_table", create_tag_table(), "pad", pad, NULL);
}

static void
xpad_text_buffer_class_init (XpadTextBufferClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->dispose = xpad_text_buffer_dispose;
	gobject_class->finalize = xpad_text_buffer_finalize;
	gobject_class->set_property = xpad_text_buffer_set_property;
	gobject_class->get_property = xpad_text_buffer_get_property;

	g_object_class_install_property (gobject_class,
					 PROP_PAD,
					 g_param_spec_pointer ("pad",
							       "Pad",
							       "Pad connected to this buffer",
							       G_PARAM_READWRITE));
}

static void
xpad_text_buffer_init (XpadTextBuffer *buffer)
{
	buffer->priv = xpad_text_buffer_get_instance_private (buffer);
}

static void
xpad_text_buffer_dispose (GObject *object)
{
	G_OBJECT_CLASS (xpad_text_buffer_parent_class)->dispose (object);
}

static void
xpad_text_buffer_finalize (GObject *object)
{
	G_OBJECT_CLASS (xpad_text_buffer_parent_class)->finalize (object);
}

static void
xpad_text_buffer_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	XpadTextBuffer *buffer = XPAD_TEXT_BUFFER (object);

	switch (prop_id)
	{
	case PROP_PAD:
		buffer->priv->pad = g_value_get_pointer (value);
		g_object_ref (buffer->priv->pad);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
xpad_text_buffer_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	XpadTextBuffer *buffer = XPAD_TEXT_BUFFER (object);

	switch (prop_id)
	{
	case PROP_PAD:
		g_value_set_pointer (value, buffer->priv->pad);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
xpad_text_buffer_set_text_with_tags (XpadTextBuffer *buffer, const gchar *text)
{
	GtkTextIter start, end;
	GList *tags = NULL;
	gchar **tokens;
	gint count;
	gchar tag_char_utf8[7] = {0};

	if (!text)
		return;

	GtkSourceBuffer *buffer_tb = GTK_SOURCE_BUFFER (buffer);

	gtk_text_buffer_begin_user_action (GTK_TEXT_BUFFER (buffer_tb));

	gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer_tb), &start, &end);
	gtk_text_buffer_delete (GTK_TEXT_BUFFER (buffer_tb), &start, &end);
	gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer_tb), &start, &end);

	g_unichar_to_utf8 (TAG_CHAR, tag_char_utf8);

	tokens = g_strsplit (text, tag_char_utf8, 0);

	for (count = 0; tokens[count]; count++)
	{
		if (count % 2 == 0)
		{
			gint offset;
			GList *j;

			offset = gtk_text_iter_get_offset (&end);
			gtk_text_buffer_insert (GTK_TEXT_BUFFER (buffer_tb), &end, tokens[count], -1);
			gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (buffer_tb), &start, offset);

			for (j = tags; j; j = j->next)
			{
				gtk_text_buffer_apply_tag_by_name (GTK_TEXT_BUFFER (buffer_tb), j->data, &start, &end);
			}
		}
		else
		{
			if (tokens[count][0] != '/')
			{
				tags = g_list_prepend (tags, tokens[count]);
			}
			else
			{
				GList *element = g_list_find_custom (tags, &(tokens[count][1]), (GCompareFunc) g_ascii_strcasecmp);

				if (element)
				{
					tags = g_list_delete_link (tags, element);
				}
			}
		}
	}

	gtk_text_buffer_end_user_action (GTK_TEXT_BUFFER (buffer_tb));

	g_strfreev (tokens);
}


gchar *
xpad_text_buffer_get_text_with_tags (XpadTextBuffer *buffer)
{
	GtkTextIter start, prev;
	GSList *tags = NULL, *i;
	gchar tag_char_utf8[7] = {0};
	gchar *text = g_strdup (""), *oldtext = NULL, *tmp;
	gboolean done = FALSE;
	GtkSourceBuffer *buffer_tb = GTK_SOURCE_BUFFER (buffer);

	gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (buffer_tb), &start);

	g_unichar_to_utf8 (TAG_CHAR, tag_char_utf8);

	prev = start;

	while (!done)
	{
		tmp = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buffer_tb), &prev, &start, TRUE);
		oldtext = text;
		text = g_strconcat (text, tmp, NULL);
		g_free (oldtext);
		g_free (tmp);

		tags = gtk_text_iter_get_toggled_tags (&start, TRUE);
		for (i = tags; i; i = i->next)
		{
			gchar *name;
			g_object_get (G_OBJECT (i->data), "name", &name, NULL);

			if (name) {
				oldtext = text;
				text = g_strconcat (text, tag_char_utf8, name, tag_char_utf8, NULL);
				g_free (oldtext);
			}

			g_free (name);
		}
		g_slist_free (tags);

		tags = gtk_text_iter_get_toggled_tags (&start, FALSE);
		for (i = tags; i; i = i->next)
		{
			gchar *name;
			g_object_get (G_OBJECT (i->data), "name", &name, NULL);

			if (name) {
				oldtext = text;
				text = g_strconcat (text, tag_char_utf8, "/", name, tag_char_utf8, NULL);
				g_free (oldtext);
			}

			g_free (name);
		}
		g_slist_free (tags);

		if (gtk_text_iter_is_end (&start))
			done = TRUE;
		prev = start;
		gtk_text_iter_forward_to_tag_toggle (&start, NULL);
	}

	return text;
}

void
xpad_text_buffer_insert_text (XpadTextBuffer *buffer, gint pos, const gchar *text, gint len)
{
	GtkSourceBuffer *parent = (GtkSourceBuffer*) buffer;
	GtkTextIter iter;
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (parent), &iter, pos);
	gtk_text_buffer_insert (GTK_TEXT_BUFFER (parent), &iter, text, len);
	gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (parent), &iter);
}

void
xpad_text_buffer_delete_range (XpadTextBuffer *buffer, gint start, gint end)
{
	GtkSourceBuffer *parent = (GtkSourceBuffer*) buffer;

	GtkTextIter start_iter;
	GtkTextIter end_iter;

	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (parent), &start_iter, start);

	if (end < 0)
		gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (parent), &end_iter);
	else
		gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (parent), &end_iter, end);

	gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (parent), &start_iter);
	gtk_text_buffer_delete (GTK_TEXT_BUFFER (parent), &start_iter, &end_iter);
}

void
xpad_text_buffer_toggle_tag (XpadTextBuffer *buffer, const gchar *name)
{
	GtkTextTagTable *table;
	GtkTextTag *tag;
	GtkTextIter start, end, i;
	gboolean all_tagged;
	GtkSourceBuffer *buffer_tb = GTK_SOURCE_BUFFER (buffer);

	table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer_tb));
	tag = gtk_text_tag_table_lookup (table, name);
	gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (buffer_tb), &start, &end);

	if (!tag)
	{
		g_printerr ("Tag not found in table %p\n", (void *) table);
		return;
	}

	for (all_tagged = TRUE, i = start; !gtk_text_iter_equal (&i, &end); gtk_text_iter_forward_char (&i))
	{
		if (!gtk_text_iter_has_tag (&i, tag))
		{
			all_tagged = FALSE;
			break;
		}
	}

	if (all_tagged)
	{
		gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (buffer_tb), tag, &start, &end);
	}
	else
	{
		gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (buffer_tb), tag, &start, &end);
	}
}

static GtkTextTagTable *
create_tag_table (void)
{
	GtkTextTagTable *table;
	GtkTextTag *tag;

	table = gtk_text_tag_table_new ();

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "bold", "weight", PANGO_WEIGHT_BOLD, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "italic", "style", PANGO_STYLE_ITALIC, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "strikethrough", "strikethrough", TRUE, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "underline", "underline", PANGO_UNDERLINE_SINGLE, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "small-xx", "scale", PANGO_SCALE_XX_SMALL, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "small-x", "scale", PANGO_SCALE_X_SMALL, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "small", "scale", PANGO_SCALE_SMALL, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "medium", "scale", PANGO_SCALE_MEDIUM, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "large", "scale", PANGO_SCALE_LARGE, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "large-x", "scale", PANGO_SCALE_X_LARGE, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "large-xx", "scale", PANGO_SCALE_XX_LARGE, NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	/* Checklist tags for a modern, color-coded look */

	/* task-done: completed task text — sleek slate grey + strikethrough */
	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "task-done",
		"strikethrough", TRUE,
		"foreground", "#64748b", /* Tailwind slate-500 */
		NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	/* task-checkbox-pending: thick black for unchecked checkbox characters */
	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "task-checkbox-pending",
		"foreground", "#000000", /* Deep black */
		"weight", PANGO_WEIGHT_HEAVY, /* Heaviest possible font weight (900) */
		NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	/* task-checkbox-done: deep emerald green for checked checkbox characters */
	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "task-checkbox-done",
		"foreground", "#059669", /* Tailwind emerald-600 */
		"weight", PANGO_WEIGHT_HEAVY, /* Heaviest possible font weight */
		NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	/* task-indent: subtle grey for indent connector characters */
	tag = GTK_TEXT_TAG (g_object_new (GTK_TYPE_TEXT_TAG, "name", "task-indent",
		"foreground", "#bdbdbd",
		NULL));
	gtk_text_tag_table_add (table, tag);
	g_clear_object (&tag);

	return table;
}

gboolean
xpad_text_buffer_undo_available (XpadTextBuffer *buffer)
{
	GtkSourceBuffer *parent = GTK_SOURCE_BUFFER (buffer);
	return gtk_source_buffer_can_undo (parent);
}

gboolean
xpad_text_buffer_redo_available (XpadTextBuffer *buffer)
{
	GtkSourceBuffer *parent = GTK_SOURCE_BUFFER (buffer);
	return gtk_source_buffer_can_redo (parent);
}

void
xpad_text_buffer_undo (XpadTextBuffer *buffer)
{
	GtkSourceBuffer *parent = GTK_SOURCE_BUFFER (buffer);
	if (gtk_source_buffer_can_undo (parent))
	{
		gtk_source_buffer_undo (parent);
	}
}

void
xpad_text_buffer_redo (XpadTextBuffer *buffer)
{
	GtkSourceBuffer *parent = GTK_SOURCE_BUFFER (buffer);
	if (gtk_source_buffer_can_redo (parent))
	{
		gtk_source_buffer_redo (parent);
	}
}

void
xpad_text_buffer_freeze_undo (XpadTextBuffer *buffer)
{
	GtkSourceBuffer *parent = GTK_SOURCE_BUFFER (buffer);
	gtk_source_buffer_begin_not_undoable_action (parent);
}

void
xpad_text_buffer_thaw_undo (XpadTextBuffer *buffer)
{
	GtkSourceBuffer *parent = GTK_SOURCE_BUFFER (buffer);
	gtk_source_buffer_end_not_undoable_action (parent);
}

XpadPad *
xpad_text_buffer_get_pad (XpadTextBuffer *buffer)
{
	if (buffer == NULL)
		return NULL;

	XpadPad *pad = NULL;
	g_object_get (G_OBJECT (buffer), "pad", &pad, NULL);
	return pad;
}

void
xpad_text_buffer_set_pad (XpadTextBuffer *buffer, XpadPad *pad)
{
	g_return_if_fail (buffer);

	g_object_set (G_OBJECT (buffer), "pad", pad, NULL);
}

/* Checklist Unicode characters — modern, visually distinct */
static const gchar *TASK_UNCHECKED    = "\xe2\x98\x90"; /* ☐ U+2610 — hollow square */
static const gchar *TASK_CHECKED      = "\xe2\x9c\x94"; /* ✔ U+2714 — heavy check mark  */
static const gchar *SUBTASK_UNCHECKED = "\xe2\x97\x8b"; /* ○ U+25CB — hollow circle */
static const gchar *SUBTASK_CHECKED   = "\xe2\x97\x8f"; /* ● U+25CF — filled circle */

/**
 * xpad_text_buffer_insert_task:
 * @buffer: the text buffer
 * @nested: if TRUE, insert a nested sub-task (circle), else top-level (square)
 *
 * Inserts a task checkbox at the current cursor position.
 * For nested tasks, also inserts leading indentation.
 */
void
xpad_text_buffer_insert_task (XpadTextBuffer *buffer, gboolean nested)
{
	GtkTextBuffer *tb = GTK_TEXT_BUFFER (buffer);
	GtkTextIter iter, cb_start;
	GtkTextMark *mark;
	gint cb_offset;

	mark = gtk_text_buffer_get_insert (tb);
	gtk_text_buffer_get_iter_at_mark (tb, &iter, mark);

	gtk_text_buffer_begin_user_action (tb);

	if (nested) {
		gtk_text_buffer_insert (tb, &iter, "  ", -1);
		cb_offset = gtk_text_iter_get_offset (&iter);
		gtk_text_buffer_insert (tb, &iter, SUBTASK_UNCHECKED, -1);
	} else {
		cb_offset = gtk_text_iter_get_offset (&iter);
		gtk_text_buffer_insert (tb, &iter, TASK_UNCHECKED, -1);
	}

	/* Apply the pending color tag to the checkbox character */
	gtk_text_buffer_get_iter_at_offset (tb, &cb_start, cb_offset);
	gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-pending", &cb_start, &iter);

	gtk_text_buffer_insert (tb, &iter, " ", -1);

	gtk_text_buffer_end_user_action (tb);
}

/**
 * xpad_text_buffer_get_task_char_at_line:
 * Returns the checkbox character type at the start of a line.
 * 0 = not a task line, 1 = top-level unchecked, 2 = top-level checked,
 * 3 = nested unchecked, 4 = nested checked.
 */
int
xpad_text_buffer_get_task_type_at_line (XpadTextBuffer *buffer, gint line_num)
{
	GtkTextBuffer *tb = GTK_TEXT_BUFFER (buffer);
	GtkTextIter line_start, line_end;
	gchar *line_text;
	int result = 0;

	gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
	line_end = line_start;
	gtk_text_iter_forward_to_line_end (&line_end);

	line_text = gtk_text_buffer_get_text (tb, &line_start, &line_end, FALSE);
	if (!line_text)
		return 0;

	/* Check for top-level task (starts with checkbox directly) */
	if (g_str_has_prefix (line_text, TASK_UNCHECKED))
		result = 1;
	else if (g_str_has_prefix (line_text, TASK_CHECKED))
		result = 2;
	/* Check for nested task (starts with spaces then circle) */
	else {
		/* Skip leading spaces */
		const gchar *p = line_text;
		while (*p == ' ')
			p++;
		if (g_str_has_prefix (p, SUBTASK_UNCHECKED))
			result = 3;
		else if (g_str_has_prefix (p, SUBTASK_CHECKED))
			result = 4;
	}

	g_free (line_text);
	return result;
}

/**
 * xpad_text_buffer_is_task_line:
 * Returns TRUE if the given line is a task line (has a checkbox).
 */
gboolean
xpad_text_buffer_is_task_line (XpadTextBuffer *buffer, gint line_num)
{
	return xpad_text_buffer_get_task_type_at_line (buffer, line_num) > 0;
}

/**
 * xpad_text_buffer_toggle_task_at_line:
 * Toggles the checkbox state at the given line and applies/removes
 * the "task-done" tag on the task text.
 */
void
xpad_text_buffer_toggle_task_at_line (XpadTextBuffer *buffer, gint line_num)
{
	GtkTextBuffer *tb = GTK_TEXT_BUFFER (buffer);
	GtkTextIter line_start, line_end, cb_start, cb_end;
	GtkTextIter text_start, text_end;
	int task_type;
	const gchar *replacement = NULL;

	task_type = xpad_text_buffer_get_task_type_at_line (buffer, line_num);
	if (task_type == 0)
		return;

	gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
	line_end = line_start;
	gtk_text_iter_forward_to_line_end (&line_end);

	/* First: remove ALL checklist tags from the entire line to start clean */
	gtk_text_buffer_remove_tag_by_name (tb, "task-done", &line_start, &line_end);
	gtk_text_buffer_remove_tag_by_name (tb, "task-checkbox-pending", &line_start, &line_end);
	gtk_text_buffer_remove_tag_by_name (tb, "task-checkbox-done", &line_start, &line_end);

	/* Find the checkbox character position */
	cb_start = line_start;
	if (task_type >= 3) {
		while (gtk_text_iter_get_char (&cb_start) == ' ')
			gtk_text_iter_forward_char (&cb_start);
	}

	cb_end = cb_start;
	gtk_text_iter_forward_char (&cb_end);

	/* Determine replacement character */
	switch (task_type) {
	case 1: replacement = TASK_CHECKED; break;
	case 2: replacement = TASK_UNCHECKED; break;
	case 3: replacement = SUBTASK_CHECKED; break;
	case 4: replacement = SUBTASK_UNCHECKED; break;
	default: return;
	}

	gtk_text_buffer_begin_user_action (tb);

	/* Replace the checkbox character */
	gtk_text_buffer_delete (tb, &cb_start, &cb_end);
	gtk_text_buffer_insert (tb, &cb_start, replacement, -1);

	/* Re-fetch line boundaries after edit */
	gtk_text_buffer_get_iter_at_line (tb, &line_start, line_num);
	line_end = line_start;
	gtk_text_iter_forward_to_line_end (&line_end);

	/* Find checkbox char again to apply color tag */
	cb_start = line_start;
	if (task_type >= 3) {
		while (gtk_text_iter_get_char (&cb_start) == ' ')
			gtk_text_iter_forward_char (&cb_start);
	}
	cb_end = cb_start;
	gtk_text_iter_forward_char (&cb_end);

	/* text_start is after the checkbox + space */
	text_start = cb_end;
	if (gtk_text_iter_get_char (&text_start) == ' ')
		gtk_text_iter_forward_char (&text_start);
	text_end = line_end;

	if (task_type == 1 || task_type == 3) {
		/* Was unchecked → now checked */
		gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-done", &cb_start, &cb_end);
		gtk_text_buffer_apply_tag_by_name (tb, "task-done", &text_start, &text_end);
	} else {
		/* Was checked → now unchecked — just apply pending color to checkbox */
		gtk_text_buffer_apply_tag_by_name (tb, "task-checkbox-pending", &cb_start, &cb_end);
		/* text stays default color (no tag needed) */
	}

	gtk_text_buffer_end_user_action (tb);
}

/**
 * xpad_text_buffer_get_line_indent_spaces:
 * Returns the number of leading spaces on the given line.
 */
gint
xpad_text_buffer_get_line_indent_spaces (XpadTextBuffer *buffer, gint line_num)
{
	GtkTextBuffer *tb = GTK_TEXT_BUFFER (buffer);
	GtkTextIter iter;
	gint count = 0;

	gtk_text_buffer_get_iter_at_line (tb, &iter, line_num);

	while (!gtk_text_iter_ends_line (&iter) && gtk_text_iter_get_char (&iter) == ' ') {
		count++;
		gtk_text_iter_forward_char (&iter);
	}

	return count;
}
