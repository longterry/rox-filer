/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* gui_support.c - general (GUI) support routines */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "main.h"
#include "gui_support.h"
#include "support.h"
#include "pixmaps.h"
#include "choices.h"
#include "options.h"

/* XXX: RandR will break this! */
gint	screen_width, screen_height;

static GdkAtom xa_cardinal;

static GtkWidget *current_dialog = NULL;

static GtkWidget *tip_widget = NULL;
static time_t tip_time = 0; 	/* Time tip widget last closed */
static gint tip_timeout = 0;	/* When primed */

void gui_support_init()
{
	xa_cardinal = gdk_atom_intern("CARDINAL", FALSE);

	/* This call starts returning strange values after a while, so get
	 * the result here during init.
	 */
	gdk_drawable_get_size(gdk_get_default_root_window(),
			    &screen_width, &screen_height);
}

/* Open a modal dialog box showing a message.
 * The user can choose from a selection of buttons at the bottom.
 * Returns -1 if the window is destroyed, or the number of the button
 * if one is clicked (starting from zero).
 *
 * If a dialog is already open, returns -1 without waiting AND
 * brings the current dialog to the front.
 */
int get_choice(const char *title,
	       const char *message,
	       int number_of_buttons, ...)
{
	GtkWidget	*dialog;
	GtkWidget	*button = NULL;
	int		i, retval;
	va_list	ap;

	if (current_dialog)
	{
		gtk_widget_hide(current_dialog);
		gtk_widget_show(current_dialog);
		return -1;
	}

	current_dialog = dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_QUESTION,
					GTK_BUTTONS_NONE,
					"%s", message);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	va_start(ap, number_of_buttons);

	for (i = 0; i < number_of_buttons; i++)
		button = gtk_dialog_add_button(GTK_DIALOG(current_dialog),
				va_arg(ap, char *), i);

	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	gtk_dialog_set_default_response(GTK_DIALOG(dialog), i - 1);

	va_end(ap);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_NONE)
		retval = -1;
	gtk_widget_destroy(dialog);

	current_dialog = NULL;

	return retval;
}

void info_message(const char *message, ...)
{
	GtkWidget *dialog;
        va_list args;
	gchar *s;

	g_return_if_fail(message != NULL);

	va_start(args, message);
	s = g_strdup_vprintf(message, args);
	va_end(args);

	dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_INFO,
					GTK_BUTTONS_OK,
					"%s", s);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	g_free(s);
}

/* Display a message in a window with "ROX-Filer" as title */
void report_error(const char *message, ...)
{
	GtkWidget *dialog;
        va_list args;
	gchar *s;

	g_return_if_fail(message != NULL);

	va_start(args, message);
	s = g_strdup_vprintf(message, args);
	va_end(args);

	dialog = gtk_message_dialog_new(NULL,
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_OK,
			"%s", s);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	g_free(s);
}

void set_cardinal_property(GdkWindow *window, GdkAtom prop, guint32 value)
{
	gdk_property_change(window, prop, xa_cardinal, 32,
				GDK_PROP_MODE_REPLACE, (gchar *) &value, 1);
}

/* NB: Also used for pinned icons.
 * TODO: Set the level here too.
 */
void make_panel_window(GtkWidget *widget)
{
	static gboolean need_init = TRUE;
	static GdkAtom xa_state, xa_atom, xa_net_state, xa_hints, xa_win_hints;
	static GdkAtom state_list[3];
	GdkWindow *window = widget->window;
	gint32  wm_hints_values[] = {1, False, 0, 0, 0, 0, 0, 0};
	GdkAtom	wm_protocols[2];

	g_return_if_fail(window != NULL);

	if (o_override_redirect.int_value)
	{
		gdk_window_set_override_redirect(window, TRUE);
		return;
	}

	if (need_init)
	{
		xa_win_hints = gdk_atom_intern("_WIN_HINTS", FALSE);
		xa_state = gdk_atom_intern("_WIN_STATE", FALSE);
		xa_atom = gdk_atom_intern("ATOM", FALSE);
		xa_net_state = gdk_atom_intern("_NET_WM_STATE", FALSE);
		xa_hints = gdk_atom_intern("WM_HINTS", FALSE);

		/* Note: Starting with Gtk+-1.3.12, Gtk+ converts GdkAtoms
		 * to X atoms automatically when the type is ATOM.
		 */
		state_list[0] = gdk_atom_intern("_NET_WM_STATE_STICKY", FALSE);
		state_list[1] = gdk_atom_intern("_NET_WM_STATE_SKIP_PAGER",
						FALSE);
		state_list[2] = gdk_atom_intern("_NET_WM_STATE_SKIP_TASKBAR",
						FALSE);
		
		need_init = FALSE;
	}
	
	gdk_window_set_decorations(window, 0);
	gdk_window_set_functions(window, 0);
	gtk_window_set_resizable(GTK_WINDOW(widget), FALSE);

	/* Note: DON'T do gtk_window_stick(). Setting the state via
	 * gdk will override our other atoms (pager/taskbar).
	 */

	/* Don't hide panel/pinboard windows initially (WIN_STATE_HIDDEN).
	 * Needed for IceWM - Christopher Arndt <chris.arndt@web.de>
	 */
	set_cardinal_property(window, xa_state,
			WIN_STATE_STICKY |
			WIN_STATE_FIXED_POSITION | WIN_STATE_ARRANGE_IGNORE);

	set_cardinal_property(window, xa_win_hints,
			WIN_HINTS_SKIP_FOCUS | WIN_HINTS_SKIP_WINLIST |
			WIN_HINTS_SKIP_TASKBAR);

	gdk_property_change(window, xa_net_state, xa_atom, 32,
			GDK_PROP_MODE_APPEND, (guchar *) state_list, 3);

	g_return_if_fail(window != NULL);

	gdk_property_change(window, xa_hints, xa_hints, 32,
			GDK_PROP_MODE_REPLACE, (guchar *) wm_hints_values,
			sizeof(wm_hints_values) / sizeof(gint32));

	wm_protocols[0] = gdk_atom_intern("WM_DELETE_WINDOW", FALSE);
	wm_protocols[1] = gdk_atom_intern("_NET_WM_PING", FALSE);
	gdk_property_change(window,
			gdk_atom_intern("WM_PROTOCOLS", FALSE), xa_atom, 32,
			GDK_PROP_MODE_REPLACE, (guchar *) wm_protocols,
			sizeof(wm_protocols) / sizeof(GdkAtom));
}

static gboolean error_idle_cb(gpointer data)
{
	char	**error = (char **) data;
	
	report_error("%s", *error);
	null_g_free(error);

	one_less_window();
	return FALSE;
}

/* Display an error with "ROX-Filer" as title next time we are idle.
 * If multiple errors are reported this way before the window is opened,
 * all are displayed in a single window.
 * If an error is reported while the error window is open, it is discarded.
 */
void delayed_error(const char *error, ...)
{
	static char *delayed_error_data = NULL;
	char *old, *new;
	va_list args;

	g_return_if_fail(error != NULL);

	old = delayed_error_data;

	va_start(args, error);
	new = g_strdup_vprintf(error, args);
	va_end(args);

	if (old)
	{
		delayed_error_data = g_strconcat(old,
				_("\n---\n"),
				new, NULL);
		g_free(old);
		g_free(new);
	}
	else
	{
		delayed_error_data = new;
		gtk_idle_add(error_idle_cb, &delayed_error_data);

		number_of_windows++;
	}
}

/* Load the file into memory. Return TRUE on success.
 * Block is zero terminated (but this is not included in the length).
 */
gboolean load_file(const char *pathname, char **data_out, long *length_out)
{
	gsize len;
	GError *error = NULL;
	
	if (!g_file_get_contents(pathname, data_out, &len, &error))
	{
		delayed_error("%s", error->message);
		g_error_free(error);
		return FALSE;
	}
		
	if (length_out)
		*length_out = len;
	return TRUE;
}

GtkWidget *new_help_button(HelpFunc show_help, gpointer data)
{
	GtkWidget	*b, *icon;
	
	b = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	icon = gtk_image_new_from_stock(GTK_STOCK_DIALOG_INFO,
					GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(b), icon);
	g_signal_connect_swapped(b, "clicked", G_CALLBACK(show_help), data);

	GTK_WIDGET_UNSET_FLAGS(b, GTK_CAN_FOCUS);

	return b;
}

/* Read file into memory. Call parse_line(guchar *line) for each line
 * in the file. Callback returns NULL on success, or an error message
 * if something went wrong. Only the first error is displayed to the user.
 */
void parse_file(const char *path, ParseFunc *parse_line)
{
	char		*data;
	long		length;
	gboolean	seen_error = FALSE;

	if (load_file(path, &data, &length))
	{
		char *eol;
		const char *error;
		char *line = data;
		int  line_number = 1;

		if (strncmp(data, "<?xml ", 6) == 0)
		{
			delayed_error(_("Attempt to read an XML file as "
					"a text file. File '%s' may be "
					"corrupted."), path);
			return;
		}

		while (line && *line)
		{
			eol = strchr(line, '\n');
			if (eol)
				*eol = '\0';

			error = parse_line(line);

			if (error && !seen_error)
			{
				delayed_error(
		_("Error in '%s' file at line %d: "
		"\n\"%s\"\n"
		"This may be due to upgrading from a previous version of "
		"ROX-Filer. Open the Options window and click on Save.\n"
		"Further errors will be ignored."),
					path,
					line_number,
					error);
				seen_error = TRUE;
			}

			if (!eol)
				break;
			line = eol + 1;
			line_number++;
		}
		g_free(data);
	}
}

/* Sets up a proxy window for DnD on the specified X window.
 * Courtesy of Owen Taylor (taken from gmc).
 */
gboolean setup_xdnd_proxy(guint32 xid, GdkWindow *proxy_window)
{
	GdkAtom	xdnd_proxy_atom;
	Window	proxy_xid;
	Atom	type;
	int	format;
	unsigned long nitems, after;
	Window	*proxy_data;
	Window	proxy;

	XGrabServer(GDK_DISPLAY());

	xdnd_proxy_atom = gdk_atom_intern("XdndProxy", FALSE);
	proxy_xid = GDK_WINDOW_XWINDOW(proxy_window);
	type = None;
	proxy = None;

	gdk_error_trap_push();

	/* Check if somebody else already owns drops on the root window */

	XGetWindowProperty(GDK_DISPLAY(), xid,
			   gdk_x11_atom_to_xatom(xdnd_proxy_atom), 0,
			   1, False, AnyPropertyType,
			   &type, &format, &nitems, &after,
			   (guchar **) &proxy_data);

	if (type != None)
	{
		if (format == 32 && nitems == 1)
			proxy = *proxy_data;

		XFree(proxy_data);
	}

	/* The property was set, now check if the window it points to exists
	 * and has a XdndProxy property pointing to itself.
	 */
	if (proxy)
	{
		gint	gdk_error_code;

		XGetWindowProperty(GDK_DISPLAY(), proxy,
				    gdk_x11_atom_to_xatom(xdnd_proxy_atom),
				    0, 1, False, AnyPropertyType,
				    &type, &format, &nitems, &after,
				   (guchar **) &proxy_data);

		gdk_error_code = gdk_error_trap_pop();
		gdk_error_trap_push();
		
		if (!gdk_error_code && type != None)
		{
			if (format == 32 && nitems == 1)
				if (*proxy_data != proxy)
					proxy = None;

			XFree(proxy_data);
		}
		else
			proxy = gdk_x11_atom_to_xatom(GDK_NONE);
	}

	if (!proxy)
	{
		/* OK, we can set the property to point to us */
		/* TODO: Use gdk call? */

		XChangeProperty(GDK_DISPLAY(), xid,
				gdk_x11_atom_to_xatom(xdnd_proxy_atom),
				gdk_x11_atom_to_xatom(gdk_atom_intern("WINDOW",
						      FALSE)),
				32, PropModeReplace,
				(guchar *) &proxy_xid, 1);
	}

	gdk_error_trap_pop();

	XUngrabServer(GDK_DISPLAY());
	gdk_flush();

	if (!proxy)
	{
		/* Mark our window as a valid proxy window with a XdndProxy
		 * property pointing recursively;
		 */
		XChangeProperty(GDK_DISPLAY(), proxy_xid,
				gdk_x11_atom_to_xatom(xdnd_proxy_atom),
				gdk_x11_atom_to_xatom(gdk_atom_intern("WINDOW",
						      FALSE)),
				32, PropModeReplace,
				(guchar *) &proxy_xid, 1);
	}
	
	return !proxy;
}

/* xid is the window (usually the root) which points to the proxy */
void release_xdnd_proxy(guint32 xid)
{
	GdkAtom	xdnd_proxy_atom;

	xdnd_proxy_atom = gdk_atom_intern("XdndProxy", FALSE);

	XDeleteProperty(GDK_DISPLAY(), xid,
			gdk_x11_atom_to_xatom(xdnd_proxy_atom));
}

/* Looks for the proxy window to get root window clicks from the window
 * manager. Taken from gmc. NULL if there is no proxy window.
 */
GdkWindow *find_click_proxy_window(void)
{
	GdkAtom click_proxy_atom;
	Atom type;
	int format;
	unsigned long nitems, after;
	Window *proxy_data;
	Window proxy;
	GdkWindow *proxy_gdk_window;

	XGrabServer(GDK_DISPLAY());

	click_proxy_atom = gdk_atom_intern("_WIN_DESKTOP_BUTTON_PROXY", FALSE);
	type = None;
	proxy = None;

	gdk_error_trap_push();

	/* Check if the proxy window exists */

	XGetWindowProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
			   gdk_x11_atom_to_xatom(click_proxy_atom), 0,
			   1, False, AnyPropertyType,
			   &type, &format, &nitems, &after,
			   (guchar **) &proxy_data);

	if (type != None)
	{
		if (format == 32 && nitems == 1)
			proxy = *proxy_data;

		XFree(proxy_data);
	}

	/* If the property was set, check if the window it points to exists
	 * and has a _WIN_DESKTOP_BUTTON_PROXY property pointing to itself.
	 */

	if (proxy)
	{
		gint	gdk_error_code;
		XGetWindowProperty(GDK_DISPLAY(), proxy,
				   gdk_x11_atom_to_xatom(click_proxy_atom), 0,
				   1, False, AnyPropertyType,
				   &type, &format, &nitems, &after,
				   (guchar **) &proxy_data);

		gdk_error_code = gdk_error_trap_pop();
		gdk_error_trap_push();
		
		if (!gdk_error_code && type != None)
		{
			if (format == 32 && nitems == 1)
				if (*proxy_data != proxy)
					proxy = gdk_x11_atom_to_xatom(GDK_NONE);

			XFree(proxy_data);
		}
		else
			proxy = gdk_x11_atom_to_xatom(GDK_NONE);
	}

	gdk_error_trap_pop();

	XUngrabServer(GDK_DISPLAY());
	gdk_flush();

	if (proxy)
		proxy_gdk_window = gdk_window_foreign_new(proxy);
	else
		proxy_gdk_window = NULL;

	return proxy_gdk_window;
}

/* Returns the position of the pointer.
 * TRUE if any modifier keys or mouse buttons are pressed.
 */
gboolean get_pointer_xy(int *x, int *y)
{
	unsigned int mask;

	gdk_window_get_pointer(NULL, x, y, &mask);

	return mask != 0;
}

#define DECOR_BORDER 32

/* Centre the window at these coords */
void centre_window(GdkWindow *window, int x, int y)
{
	int	w, h;

	g_return_if_fail(window != NULL);

	gdk_drawable_get_size(window, &w, &h);
	
	x -= w / 2;
	y -= h / 2;

	gdk_window_move(window,
		CLAMP(x, DECOR_BORDER, screen_width - w - DECOR_BORDER),
		CLAMP(y, DECOR_BORDER, screen_height - h - DECOR_BORDER));
}

static GtkWidget *current_wink_widget = NULL;
static gint	wink_timeout = -1;	/* Called when it's time to stop */
static gulong	wink_destroy;		/* Called if the widget dies first */

static gboolean end_wink(gpointer data)
{
	gtk_drag_unhighlight(current_wink_widget);

	g_signal_handler_disconnect(current_wink_widget, wink_destroy);

	current_wink_widget = NULL;

	return FALSE;
}

static void cancel_wink(void)
{
	gtk_timeout_remove(wink_timeout);
	end_wink(NULL);
}

static void wink_widget_died(gpointer data)
{
	current_wink_widget = NULL;
	gtk_timeout_remove(wink_timeout);
}

/* Draw a black box around this widget, briefly.
 * Note: uses the drag highlighting code for now.
 */
void wink_widget(GtkWidget *widget)
{
	g_return_if_fail(widget != NULL);
	
	if (current_wink_widget)
		cancel_wink();

	current_wink_widget = widget;
	gtk_drag_highlight(current_wink_widget);
	
	wink_timeout = gtk_timeout_add(300, (GtkFunction) end_wink, NULL);

	wink_destroy = g_signal_connect_swapped(widget, "destroy",
				G_CALLBACK(wink_widget_died), NULL);
}

static gboolean idle_destroy_cb(GtkWidget *widget)
{
	gtk_widget_unref(widget);
	gtk_widget_destroy(widget);
	return FALSE;
}

/* Destroy the widget in an idle callback */
void destroy_on_idle(GtkWidget *widget)
{
	gtk_widget_ref(widget);
	gtk_idle_add((GtkFunction) idle_destroy_cb, widget);
}

/* Spawn a child process (as spawn_full), and report errors.
 * TRUE on success.
 */
gboolean rox_spawn(const gchar *dir, const gchar **argv)
{
	GError	*error = NULL;

	if (!g_spawn_async_with_pipes(dir, (gchar **) argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,		/* Child setup fn */
			NULL,			/* Child PID */
			NULL, NULL, NULL,	/* Standard pipes */
			&error))
	{
		delayed_error("%s", error ? error->message : "(null)");
		g_error_free(error);

		return FALSE;
	}

	return TRUE;
}

GtkWidget *button_new_mixed(const char *stock, const char *message)
{
	GtkWidget *button, *align, *image, *hbox, *label;
	
	button = gtk_button_new();
	label = gtk_label_new_with_mnemonic(message);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);

	image = gtk_image_new_from_stock(stock, GTK_ICON_SIZE_BUTTON);
	hbox = gtk_hbox_new(FALSE, 2);

	align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);

	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(button), align);
	gtk_container_add(GTK_CONTAINER(align), hbox);
	gtk_widget_show_all(align);

	return button;
}

/* Highlight entry in red if 'error' is TRUE */
void entry_set_error(GtkWidget *entry, gboolean error)
{
	GdkColor red = {0, 0xffff, 0, 0};
	static gboolean need_init = TRUE;
	static GdkColor normal;

	if (need_init)
	{
		normal = entry->style->text[GTK_STATE_NORMAL];
		need_init = FALSE;
	}

	gtk_widget_modify_text(entry, GTK_STATE_NORMAL, error ? &red : &normal);
}

/* Change stacking position of higher to be just above lower */
void window_put_just_above(GdkWindow *higher, GdkWindow *lower)
{
	if (o_override_redirect.int_value && lower)
	{
		XWindowChanges restack;

		gdk_error_trap_push();
		
		restack.stack_mode = Above;

		restack.sibling = GDK_WINDOW_XWINDOW(lower);

		XConfigureWindow(gdk_display, GDK_WINDOW_XWINDOW(higher),
				CWSibling | CWStackMode, &restack);

		gdk_flush();
		if (gdk_error_trap_pop())
			g_warning("window_put_just_above()\n");
	}
	else
		gdk_window_lower(higher);	/* To bottom of stack */
}

/* Copied from Gtk */
static GtkFixedChild* fixed_get_child(GtkFixed *fixed, GtkWidget *widget)
{
	GList *children;

	children = fixed->children;
	while (children)
	{
		GtkFixedChild *child;

		child = children->data;
		children = children->next;

		if (child->widget == widget)
			return child;
	}

	return NULL;
}

/* Like gtk_fixed_move(), except not insanely slow */
void fixed_move_fast(GtkFixed *fixed, GtkWidget *widget, int x, int y)
{
	GtkFixedChild *child;

	child = fixed_get_child(fixed, widget);

	g_assert(child);

	gtk_widget_freeze_child_notify(widget);

	child->x = x;
	gtk_widget_child_notify(widget, "x");

	child->y = y;
	gtk_widget_child_notify(widget, "y");

	gtk_widget_thaw_child_notify(widget);

	if (GTK_WIDGET_VISIBLE(widget) && GTK_WIDGET_VISIBLE(fixed))
	{
		int border_width = GTK_CONTAINER(fixed)->border_width;
		GtkAllocation child_allocation;
		GtkRequisition child_requisition;

		gtk_widget_get_child_requisition(child->widget,
					&child_requisition);
		child_allocation.x = child->x + border_width;
		child_allocation.y = child->y + border_width;

		child_allocation.x += GTK_WIDGET(fixed)->allocation.x;
		child_allocation.y += GTK_WIDGET(fixed)->allocation.y;

		child_allocation.width = child_requisition.width;
		child_allocation.height = child_requisition.height;
		gtk_widget_size_allocate(child->widget, &child_allocation);
	}
}

/* Draw the black border */
static gint tooltip_draw(GtkWidget *w)
{
	gdk_draw_rectangle(w->window, w->style->fg_gc[w->state], FALSE, 0, 0,
			w->allocation.width - 1, w->allocation.height - 1);

	return FALSE;
}

/* When the tips window closed, record the time. If we try to open another
 * tip soon, it will appear more quickly.
 */
static void tooltip_destroyed(gpointer data)
{
	time(&tip_time);
}

/* Display a tooltip-like widget near the pointer with 'text'. If 'text' is
 * NULL, close any current tooltip.
 */
void tooltip_show(guchar *text)
{
	GtkWidget *label;
	int	x, y, py;
	int	w, h;

	if (tip_timeout)
	{
		gtk_timeout_remove(tip_timeout);
		tip_timeout = 0;
	}

	if (tip_widget)
	{
		gtk_widget_destroy(tip_widget);
		tip_widget = NULL;
	}

	if (!text)
		return;

	/* Show the tip */
	tip_widget = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_set_app_paintable(tip_widget, TRUE);
	gtk_widget_set_name(tip_widget, "gtk-tooltips");

	g_signal_connect_swapped(tip_widget, "expose_event",
			G_CALLBACK(tooltip_draw), tip_widget);

	label = gtk_label_new(text);
	gtk_misc_set_padding(GTK_MISC(label), 4, 2);
	gtk_container_add(GTK_CONTAINER(tip_widget), label);
	gtk_widget_show(label);
	gtk_widget_realize(tip_widget);

	w = tip_widget->allocation.width;
	h = tip_widget->allocation.height;
	gdk_window_get_pointer(NULL, &x, &py, NULL);

	x -= w / 2;
	y = py + 12; /* I don't know the pointer height so I use a constant */

	/* Now check for screen boundaries */
	x = CLAMP(x, 0, screen_width - w);
	y = CLAMP(y, 0, screen_height - h);

	/* And again test if pointer is over the tooltip window */
	if (py >= y && py <= y + h)
		y = py - h- 2;
	gtk_window_move(GTK_WINDOW(tip_widget), x, y);
	gtk_widget_show(tip_widget);

	g_signal_connect_swapped(tip_widget, "destroy",
			G_CALLBACK(tooltip_destroyed), NULL);
	time(&tip_time);
}

/* Call callback(user_data) after a while, unless cancelled.
 * Object is refd now and unref when cancelled / after callback called.
 */
void tooltip_prime(GtkFunction callback, GObject *object)
{
	time_t  now;
	int	delay;

	g_return_if_fail(tip_timeout == 0);
	
	time(&now);
	delay = now - tip_time > 2 ? 1000 : 200;

	g_object_ref(object);
	tip_timeout = gtk_timeout_add_full(delay,
					   (GtkFunction) callback,
					   NULL,
					   object,
					   g_object_unref);
}

/* Like gtk_widget_modify_font, but copes with font_desc == NULL */
void widget_modify_font(GtkWidget *widget, PangoFontDescription *font_desc)
{
	GtkRcStyle *rc_style;

	g_return_if_fail(GTK_IS_WIDGET(widget));

	rc_style = gtk_widget_get_modifier_style(widget);  

	if (rc_style->font_desc)
		pango_font_description_free(rc_style->font_desc);

	rc_style->font_desc = font_desc
				? pango_font_description_copy(font_desc)
				: NULL;

	gtk_widget_modify_style(widget, rc_style);
}
