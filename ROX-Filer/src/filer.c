/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

/* filer.c - code for handling filer windows */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include "collection.h"

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "run.h"
#include "mount.h"
#include "type.h"
#include "options.h"
#include "minibuffer.h"
#include "pinboard.h"
#include "toolbar.h"

#define PANEL_BORDER 2

extern int collection_menu_button;
extern gboolean collection_single_click;

FilerWindow 	*window_with_focus = NULL;
GList		*all_filer_windows = NULL;

static FilerWindow *window_with_selection = NULL;

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *filer_single_click(char *data);
static char *filer_unique_windows(char *data);
static char *filer_menu_on_2(char *data);
static char *filer_new_window_on_1(char *data);

static OptionsSection options =
{
	N_("Filer window options"),
	create_options,
	update_options,
	set_options,
	save_options
};

gboolean o_single_click = TRUE;
gboolean o_new_window_on_1 = FALSE;	/* Button 1 => New window */
gboolean o_unique_filer_windows = FALSE;
static GtkWidget *toggle_single_click;
static GtkWidget *toggle_new_window_on_1;
static GtkWidget *toggle_menu_on_2;
static GtkWidget *toggle_unique_filer_windows;

/* Static prototypes */
static void attach(FilerWindow *filer_window);
static void detach(FilerWindow *filer_window);
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);
static void show_menu(Collection *collection, GdkEventButton *event,
		int number_selected, gpointer user_data);
static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static void add_item(FilerWindow *filer_window, DirItem *item);
static int filer_confirm_close(GtkWidget *widget, GdkEvent *event,
				FilerWindow *window);
static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window);
static void set_scanning_display(FilerWindow *filer_window, gboolean scanning);
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning);
static void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data);
static gboolean minibuffer_show_cb(FilerWindow *filer_window);
static FilerWindow *find_filer_window(char *path, FilerWindow *diff);
static void filer_set_title(FilerWindow *filer_window);

static GdkAtom xa_string;
enum
{
	TARGET_STRING,
	TARGET_URI_LIST,
};

static GdkCursor *busy_cursor = NULL;

void filer_init()
{
	xa_string = gdk_atom_intern("STRING", FALSE);

	options_sections = g_slist_prepend(options_sections, &options);
	option_register("filer_new_window_on_1", filer_new_window_on_1);
	option_register("filer_menu_on_2", filer_menu_on_2);
	option_register("filer_single_click", filer_single_click);
	option_register("filer_unique_windows", filer_unique_windows);

	busy_cursor = gdk_cursor_new(GDK_WATCH);
}

static gboolean if_deleted(gpointer item, gpointer removed)
{
	int	i = ((GPtrArray *) removed)->len;
	DirItem	**r = (DirItem **) ((GPtrArray *) removed)->pdata;
	char	*leafname = ((DirItem *) item)->leafname;

	while (i--)
	{
		if (strcmp(leafname, r[i]->leafname) == 0)
			return TRUE;
	}

	return FALSE;
}

static void update_item(FilerWindow *filer_window, DirItem *item)
{
	int	i;
	char	*leafname = item->leafname;

	if (leafname[0] == '.')
	{
		if (filer_window->show_hidden == FALSE || leafname[1] == '\0'
				|| (leafname[1] == '.' && leafname[2] == '\0'))
		return;
	}

	i = collection_find_item(filer_window->collection, item, dir_item_cmp);

	if (i >= 0)
		collection_draw_item(filer_window->collection, i, TRUE);
	else
		g_warning("Failed to find '%s'\n", item->leafname);
}

static void update_display(Directory *dir,
			DirAction	action,
			GPtrArray	*items,
			FilerWindow *filer_window)
{
	int	old_num;
	int	i;
	int	cursor = filer_window->collection->cursor_item;
	char	*as;
	Collection *collection = filer_window->collection;

	switch (action)
	{
		case DIR_ADD:
			as = filer_window->auto_select;

			old_num = collection->number_of_items;
			for (i = 0; i < items->len; i++)
			{
				DirItem *item = (DirItem *) items->pdata[i];

				add_item(filer_window, item);

				if (cursor != -1 || !as)
					continue;

				if (strcmp(as, item->leafname) != 0)
					continue;

				cursor = collection->number_of_items - 1;
				if (filer_window->had_cursor)
				{
					collection_set_cursor_item(collection,
							cursor);
					filer_window->mini_cursor_base = cursor;
				}
				else
					collection_wink_item(collection,
							cursor);
			}

			if (old_num != collection->number_of_items)
				collection_qsort(filer_window->collection,
						filer_window->sort_fn);
			break;
		case DIR_REMOVE:
			collection_delete_if(filer_window->collection,
					if_deleted,
					items);
			break;
		case DIR_START_SCAN:
			set_scanning_display(filer_window, TRUE);
			break;
		case DIR_END_SCAN:
			if (filer_window->window->window)
				gdk_window_set_cursor(
						filer_window->window->window,
						NULL);
			shrink_width(filer_window);
			if (filer_window->had_cursor &&
					collection->cursor_item == -1)
			{
				collection_set_cursor_item(collection, 0);
				filer_window->had_cursor = FALSE;
			}
			set_scanning_display(filer_window, FALSE);
			break;
		case DIR_UPDATE:
			for (i = 0; i < items->len; i++)
			{
				DirItem *item = (DirItem *) items->pdata[i];

				update_item(filer_window, item);
			}
			collection_qsort(filer_window->collection,
					filer_window->sort_fn);
			break;
	}
}

static void attach(FilerWindow *filer_window)
{
	gdk_window_set_cursor(filer_window->window->window, busy_cursor);
	collection_clear(filer_window->collection);
	filer_window->scanning = TRUE;
	dir_attach(filer_window->directory, (DirCallback) update_display,
			filer_window);
	filer_set_title(filer_window);
}

static void detach(FilerWindow *filer_window)
{
	g_return_if_fail(filer_window->directory != NULL);

	dir_detach(filer_window->directory,
			(DirCallback) update_display, filer_window);
	g_fscache_data_unref(dir_cache, filer_window->directory);
	filer_window->directory = NULL;
}

static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	all_filer_windows = g_list_remove(all_filer_windows, filer_window);

	if (window_with_selection == filer_window)
		window_with_selection = NULL;
	
	if (window_with_focus == filer_window)
	{
		if (popup_menu)
			gtk_menu_popdown(GTK_MENU(popup_menu));
		window_with_focus = NULL;
	}

	if (filer_window->directory)
		detach(filer_window);

	g_free(filer_window->auto_select);
	g_free(filer_window->path);
	g_free(filer_window);

	if (--number_of_windows < 1)
		gtk_main_quit();
}
	
/* Add a single object to a directory display */
static void add_item(FilerWindow *filer_window, DirItem *item)
{
	char		*leafname = item->leafname;
	int		item_width;

	if (leafname[0] == '.')
	{
		if (filer_window->show_hidden == FALSE || leafname[1] == '\0'
				|| (leafname[1] == '.' && leafname[2] == '\0'))
		return;
	}

	item_width = calc_width(filer_window, item); 
	if (item_width > filer_window->collection->item_width)
		collection_set_item_size(filer_window->collection,
					 item_width,
					 filer_window->collection->item_height);
	collection_insert(filer_window->collection, item);
}

static void show_menu(Collection *collection, GdkEventButton *event,
		int item, gpointer user_data)
{
	show_filer_menu((FilerWindow *) user_data, event, item);
}

/* Returns TRUE iff the directory still exists. */
static gboolean may_rescan(FilerWindow *filer_window, gboolean warning)
{
	Directory *dir;
	
	g_return_val_if_fail(filer_window != NULL, FALSE);

	/* We do a fresh lookup (rather than update) because the inode may
	 * have changed.
	 */
	dir = g_fscache_lookup(dir_cache, filer_window->path);
	if (!dir)
	{
		if (warning)
			delayed_error(PROJECT, _("Directory missing/deleted"));
		gtk_widget_destroy(filer_window->window);
		return FALSE;
	}
	if (dir == filer_window->directory)
		g_fscache_data_unref(dir_cache, dir);
	else
	{
		detach(filer_window);
		filer_window->directory = dir;
		attach(filer_window);
	}

	return TRUE;
}

/* Another app has grabbed the selection */
static gint collection_lose_selection(GtkWidget *widget,
				      GdkEventSelection *event)
{
	if (window_with_selection &&
			window_with_selection->collection == COLLECTION(widget))
	{
		FilerWindow *filer_window = window_with_selection;
		window_with_selection = NULL;
		collection_clear_selection(filer_window->collection);
	}

	return TRUE;
}

/* Someone wants us to send them the selection */
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data)
{
	GString	*reply, *header;
	FilerWindow 	*filer_window;
	int		i;
	Collection	*collection;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");

	reply = g_string_new(NULL);
	header = g_string_new(NULL);

	switch (info)
	{
		case TARGET_STRING:
			g_string_sprintf(header, " %s",
					make_path(filer_window->path, "")->str);
			break;
		case TARGET_URI_LIST:
			g_string_sprintf(header, " file://%s%s",
					our_host_name(),
					make_path(filer_window->path, "")->str);
			break;
	}

	collection = filer_window->collection;
	for (i = 0; i < collection->number_of_items; i++)
	{
		if (collection->items[i].selected)
		{
			DirItem *item =
				(DirItem *) collection->items[i].data;
			
			g_string_append(reply, header->str);
			g_string_append(reply, item->leafname);
		}
	}
	/* This works, but I don't think I like it... */
	/* g_string_append_c(reply, ' '); */
	
	gtk_selection_data_set(selection_data, xa_string,
			8, reply->str + 1, reply->len - 1);
	g_string_free(reply, TRUE);
	g_string_free(header, TRUE);
}

/* No items are now selected. This might be because another app claimed
 * the selection or because the user unselected all the items.
 */
static void lose_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (window_with_selection == filer_window)
	{
		window_with_selection = NULL;
		gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				time);
	}
}

static void gain_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (gtk_selection_owner_set(GTK_WIDGET(collection),
				GDK_SELECTION_PRIMARY,
				time))
	{
		window_with_selection = filer_window;
	}
	else
		collection_clear_selection(filer_window->collection);
}

static void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data)
{
	FilerWindow	*filer_window = (FilerWindow *) user_data;
	GdkEvent 	*event;
	GdkEventButton 	*bevent;
	GdkEventKey 	*kevent;
	OpenFlags	flags = 0;

	event = (GdkEvent *) gtk_get_current_event();

	bevent = (GdkEventButton *) event;
	kevent = (GdkEventKey *) event;

	switch (event->type)
	{
		case GDK_2BUTTON_PRESS:
		case GDK_BUTTON_PRESS:
		case GDK_BUTTON_RELEASE:
			if (bevent->state & GDK_SHIFT_MASK)
				flags |= OPEN_SHIFT;

			if (o_new_window_on_1 ^ (bevent->button == 1))
				flags |= OPEN_SAME_WINDOW;
			
			if (bevent->button != 1)
				flags |= OPEN_CLOSE_WINDOW;
			
			if (o_single_click == FALSE &&
				(bevent->state & GDK_CONTROL_MASK) != 0)
				flags ^= OPEN_SAME_WINDOW | OPEN_CLOSE_WINDOW;
			break;
		case GDK_KEY_PRESS:
			flags |= OPEN_SAME_WINDOW;
			if (kevent->state & GDK_SHIFT_MASK)
				flags |= OPEN_SHIFT;
			break;
		default:
			break;
	}

	filer_openitem(filer_window, item_number, flags);
}

/* Open the item (or add it to the shell command minibuffer) */
void filer_openitem(FilerWindow *filer_window, int item_number, OpenFlags flags)
{
	gboolean	shift = (flags & OPEN_SHIFT) != 0;
	gboolean	close_mini = flags & OPEN_FROM_MINI;
	gboolean	close_window = (flags & OPEN_CLOSE_WINDOW) != 0
					&& !filer_window->panel_type;
	GtkWidget	*widget;
	DirItem		*item = (DirItem *)
			filer_window->collection->items[item_number].data;
	guchar		*full_path;
	gboolean	wink = TRUE;
	Directory	*old_dir;

	widget = filer_window->window;
	if (filer_window->mini_type == MINI_SHELL)
	{
		minibuffer_add(filer_window, item->leafname);
		return;
	}

	if (item->base_type == TYPE_DIRECTORY)
	{
		/* Never close a filer window when opening a directory
		 * (click on a dir or click on an app with shift).
		 */
		if (shift || !(item->flags & ITEM_FLAG_APPDIR))
			close_window = FALSE;
	}

	full_path = make_path(filer_window->path, item->leafname)->str;

	old_dir = filer_window->directory;
	if (run_diritem(full_path, item,
			flags & OPEN_SAME_WINDOW ? filer_window : NULL,
			shift))
	{
		if (old_dir != filer_window->directory)
			return;

		if (close_window)
			gtk_widget_destroy(filer_window->window);
		else
		{
			if (wink)
				collection_wink_item(filer_window->collection,
						item_number);
			if (close_mini)
				minibuffer_hide(filer_window);
		}
	}
}

static gint pointer_in(GtkWidget *widget,
			GdkEventCrossing *event,
			FilerWindow *filer_window)
{
	may_rescan(filer_window, TRUE);
	return FALSE;
}

static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	window_with_focus = filer_window;

	return FALSE;
}

static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	/* TODO: Shade the cursor */

	return FALSE;
}

/* Move the cursor to the next selected item in direction 'dir'
 * (+1 or -1).
 */
static void next_selected(FilerWindow *filer_window, int dir)
{
	Collection 	*collection = filer_window->collection;
	int		to_check = collection->number_of_items;
	int 	   	item = collection->cursor_item;

	g_return_if_fail(dir == 1 || dir == -1);

	if (to_check > 0 && item == -1)
	{
		/* Cursor not currently on */
		if (dir == 1)
			item = 0;
		else
			item = collection->number_of_items - 1;

		if (collection->items[item].selected)
			goto found;
	}

	while (--to_check > 0)
	{
		item += dir;

		if (item >= collection->number_of_items)
			item = 0;
		else if (item < 0)
			item = collection->number_of_items - 1;

		if (collection->items[item].selected)
			goto found;
	}

	gdk_beep();
	return;
found:
	collection_set_cursor_item(collection, item);
}

/* Handle keys that can't be bound with the menu */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	switch (event->keyval)
	{
		case GDK_ISO_Left_Tab:
			next_selected(filer_window, -1);
			break;
		case GDK_Tab:
			next_selected(filer_window, 1);
			break;
		case GDK_BackSpace:
			change_to_parent(filer_window);
			break;
		default:
			return FALSE;
	}

	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
	return TRUE;
}

void change_to_parent(FilerWindow *filer_window)
{
	char	*copy;
	char	*slash;

	if (filer_window->path[0] == '/' && filer_window->path[1] == '\0')
		return;		/* Already in the root */
	
	copy = g_strdup(filer_window->path);
	slash = strrchr(copy, '/');

	if (slash)
	{
		*slash = '\0';
		filer_change_to(filer_window,
				*copy ? copy : "/",
				slash + 1);
	}
	else
		g_warning("No / in directory path!\n");

	g_free(copy);

}

/* Make filer_window display path. When finished, highlight item 'from', or
 * the first item if from is NULL. If there is currently no cursor then
 * simply wink 'from' (if not NULL).
 */
void filer_change_to(FilerWindow *filer_window, char *path, char *from)
{
	char	*from_dup;
	char	*real_path = pathdup(path);

	g_return_if_fail(filer_window != NULL);
	
	if (o_unique_filer_windows)
	{
		FilerWindow *fw;
		
		fw = find_filer_window(real_path, filer_window);
		if (fw)
			gtk_widget_destroy(fw->window);
	}

	from_dup = from && *from ? g_strdup(from) : NULL;

	detach(filer_window);
	g_free(filer_window->path);
	filer_window->path = real_path;

	filer_window->directory = g_fscache_lookup(dir_cache,
						   filer_window->path);
	if (filer_window->directory)
	{
		g_free(filer_window->auto_select);
		filer_window->had_cursor =
			filer_window->collection->cursor_item != -1
			|| filer_window->had_cursor;
		filer_window->auto_select = from_dup;

		filer_set_title(filer_window);
		collection_set_cursor_item(filer_window->collection, -1);
		attach(filer_window);

		if (filer_window->mini_type == MINI_PATH)
			gtk_idle_add((GtkFunction) minibuffer_show_cb,
					filer_window);
	}
	else
	{
		char	*error;

		g_free(from_dup);
		error = g_strdup_printf(_("Directory '%s' is not accessible"),
				path);
		delayed_error(PROJECT, error);
		g_free(error);
		gtk_widget_destroy(filer_window->window);
	}
}

void filer_open_parent(FilerWindow *filer_window)
{
	char	*copy;
	char	*slash;

	if (filer_window->path[0] == '/' && filer_window->path[1] == '\0')
		return;		/* Already in the root */
	
	copy = g_strdup(filer_window->path);
	slash = strrchr(copy, '/');

	if (slash)
	{
		*slash = '\0';
		filer_opendir(*copy ? copy : "/", PANEL_NO);
	}
	else
		g_warning("No / in directory path!\n");

	g_free(copy);
}

int selected_item_number(Collection *collection)
{
	int	i;
	
	g_return_val_if_fail(collection != NULL, -1);
	g_return_val_if_fail(IS_COLLECTION(collection), -1);
	g_return_val_if_fail(collection->number_selected == 1, -1);

	for (i = 0; i < collection->number_of_items; i++)
		if (collection->items[i].selected)
			return i;

	g_warning("selected_item: number_selected is wrong\n");

	return -1;
}

DirItem *selected_item(Collection *collection)
{
	int	item;

	item = selected_item_number(collection);

	if (item > -1)
		return (DirItem *) collection->items[item].data;
	return NULL;
}

static int filer_confirm_close(GtkWidget *widget, GdkEvent *event,
				FilerWindow *window)
{
	/* TODO: We can open lots of these - very irritating! */
	return get_choice(_("Close panel?"),
		      _("You have tried to close a panel via the window "
			"manager - I usually find that this is accidental... "
			"really close?"),
			2, _("Remove"), _("Cancel")) != 0;
}

/* Append all the URIs in the selection to the string */
static void create_uri_list(FilerWindow *filer_window, GString *string)
{
	Collection *collection = filer_window->collection;
	GString	*leader;
	int i, num_selected;

	leader = g_string_new("file://");
	if (!o_no_hostnames)
		g_string_append(leader, our_host_name());
	g_string_append(leader, filer_window->path);
	if (leader->str[leader->len - 1] != '/')
		g_string_append_c(leader, '/');

	num_selected = collection->number_selected;

	for (i = 0; num_selected > 0; i++)
	{
		if (collection->items[i].selected)
		{
			DirItem *item = (DirItem *) collection->items[i].data;
			
			g_string_append(string, leader->str);
			g_string_append(string, item->leafname);
			g_string_append(string, "\r\n");
			num_selected--;
		}
	}

	g_string_free(leader, TRUE);
}

static void start_drag_selection(Collection *collection,
				 GdkEventMotion *event,
				 int number_selected,
				 FilerWindow *filer_window)
{
	GtkWidget	*widget = (GtkWidget *) collection;

	if (number_selected == 1)
	{
		DirItem	*item;

		item = selected_item(collection);

		drag_one_item(widget, event,
			make_path(filer_window->path, item->leafname)->str,
			item,
			filer_window->mini_type == MINI_RUN_ACTION);
	}
	else
	{
		GString *uris;
	
		uris = g_string_new(NULL);
		create_uri_list(filer_window, uris);
		drag_selection(widget, event, uris->str);
		g_string_free(uris, TRUE);
	}
}

/* Creates and shows a new filer window.
 * panel_type should normally be PANEL_NO (for a normal window).
 */
FilerWindow *filer_opendir(char *path, PanelType panel_type)
{
	GtkWidget	*hbox, *scrollbar, *collection;
	FilerWindow	*filer_window;
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
	};
	char		*real_path;
	
	/* Get the real pathname of the directory and copy it */
	real_path = pathdup(path);

	/* If the user doesn't want duplicate windows then check
	 * for an existing one and close it if found.
	 */
	if (o_unique_filer_windows && panel_type == PANEL_NO)
	{
		FilerWindow *fw;
		
		fw = find_filer_window(real_path, NULL);
		
		if (fw)
		{
			    /* TODO: this should bring the window to the front
			     * at the same coordinates.
			     */
			    gtk_widget_hide(fw->window);
			    g_free(real_path);
			    gtk_widget_show(fw->window);
			    return fw;
		}
	}

	filer_window = g_new(FilerWindow, 1);
	filer_window->minibuffer = NULL;
	filer_window->minibuffer_label = NULL;
	filer_window->minibuffer_area = NULL;
	filer_window->temp_show_hidden = FALSE;
	filer_window->path = real_path;
	filer_window->scanning = FALSE;
	filer_window->had_cursor = FALSE;
	filer_window->auto_select = NULL;
	filer_window->mini_type = MINI_NONE;

	/* Finds the entry for this directory in the dir cache, creating
	 * a new one if needed. This does not cause a scan to start,
	 * so if a new entry is created then it will be empty.
	 */
	filer_window->directory = g_fscache_lookup(dir_cache,
						   filer_window->path);
	if (!filer_window->directory)
	{
		char	*error;

		error = g_strdup_printf(_("Directory '%s' not found."), path);
		delayed_error(PROJECT, error);
		g_free(error);
		g_free(filer_window->path);
		g_free(filer_window);
		return NULL;
	}

	filer_window->show_hidden = last_show_hidden;
	filer_window->panel_type = panel_type;
	filer_window->temp_item_selected = FALSE;
	filer_window->sort_fn = last_sort_fn;
	filer_window->flags = (FilerFlags) 0;
	filer_window->details_type = DETAILS_SUMMARY;
	filer_window->display_style = UNKNOWN_STYLE;

	/* Create the top-level window widget */
	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	filer_set_title(filer_window);

	/* The collection is the area that actually displays the files */
	collection = collection_new(NULL);
	gtk_object_set_data(GTK_OBJECT(collection),
			"filer_window", filer_window);
	filer_window->collection = COLLECTION(collection);

	gtk_widget_add_events(filer_window->window, GDK_ENTER_NOTIFY);
	gtk_signal_connect(GTK_OBJECT(filer_window->window),
			"enter-notify-event",
			GTK_SIGNAL_FUNC(pointer_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_in_event",
			GTK_SIGNAL_FUNC(focus_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_out_event",
			GTK_SIGNAL_FUNC(focus_out), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);

	gtk_signal_connect(GTK_OBJECT(filer_window->collection), "open_item",
			open_item, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "show_menu",
			show_menu, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "gain_selection",
			gain_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "lose_selection",
			lose_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_selection",
			start_drag_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_data_get",
			drag_data_get, NULL);
	gtk_signal_connect(GTK_OBJECT(collection), "selection_clear_event",
			GTK_SIGNAL_FUNC(collection_lose_selection), NULL);
	gtk_signal_connect(GTK_OBJECT(collection), "selection_get",
			GTK_SIGNAL_FUNC(selection_get), NULL);
	gtk_selection_add_targets(collection, GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));

	display_set_layout(filer_window, last_layout);
	drag_set_dest(filer_window);

	/* Add decorations appropriate to the window's type */
	if (panel_type)
	{
		int		swidth, sheight, iwidth, iheight;
		GtkWidget	*frame, *win = filer_window->window;

		gtk_window_set_wmclass(GTK_WINDOW(win), "ROX-Panel",
				PROJECT);
		collection_set_panel(filer_window->collection, TRUE);
		gtk_signal_connect(GTK_OBJECT(filer_window->window),
				"delete_event",
				GTK_SIGNAL_FUNC(filer_confirm_close),
				filer_window);

		gdk_window_get_size(GDK_ROOT_PARENT(), &swidth, &sheight);
		iwidth = filer_window->collection->item_width;
		iheight = filer_window->collection->item_height;
		
		{
			int	height = iheight + PANEL_BORDER;
			int	y = panel_type == PANEL_TOP 
					? 0
					: sheight - height - PANEL_BORDER;

			gtk_widget_set_usize(collection, swidth, height);
			gtk_widget_set_uposition(win, 0, y);
		}

		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
		gtk_container_add(GTK_CONTAINER(frame), collection);
		gtk_container_add(GTK_CONTAINER(win), frame);

		gtk_widget_show_all(frame);
		gtk_widget_realize(win);
		if (override_redirect)
			gdk_window_set_override_redirect(win->window, TRUE);
		make_panel_window(win->window);
	}
	else
	{
		GtkWidget	*vbox;
		int		col_height = ROW_HEIGHT_LARGE * 3;

		gtk_signal_connect(GTK_OBJECT(collection),
				"key_press_event",
				GTK_SIGNAL_FUNC(key_press_event), filer_window);
		gtk_window_set_default_size(GTK_WINDOW(filer_window->window),
			filer_window->display_style == LARGE_ICONS ? 400 : 512,
			o_toolbar == TOOLBAR_NONE ? col_height:
			o_toolbar == TOOLBAR_NORMAL ? col_height + 24 :
			col_height + 38);

		hbox = gtk_hbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(filer_window->window),
					hbox);

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
		
		if (o_toolbar != TOOLBAR_NONE)
		{
			GtkWidget *toolbar;
			
			toolbar = toolbar_new(filer_window);
			gtk_box_pack_start(GTK_BOX(vbox), toolbar,
					FALSE, TRUE, 0);
			gtk_widget_show_all(toolbar);
		}

		gtk_box_pack_start(GTK_BOX(vbox), collection, TRUE, TRUE, 0);

		create_minibuffer(filer_window);
		gtk_box_pack_start(GTK_BOX(vbox), filer_window->minibuffer_area,
					FALSE, TRUE, 0);

		scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
		gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);
		gtk_accel_group_attach(filer_keys,
				GTK_OBJECT(filer_window->window));
		gtk_window_set_focus(GTK_WINDOW(filer_window->window),
				collection);

		gtk_widget_show(hbox);
		gtk_widget_show(vbox);
		gtk_widget_show(scrollbar);
		gtk_widget_show(collection);
	}

	gtk_widget_realize(filer_window->window);

	/* The collection is created empty and then attach() is called, which
	 * links the filer window to the entry in the directory cache we
	 * looked up / created above.
	 *
	 * The attach() function will immediately callback to the filer window
	 * to deliver a list of all known entries in the directory (so,
	 * collection->number_of_items may be valid after the call to
	 * attach() returns).
	 *
	 * BUT, if the directory was not in the cache (because it hadn't been
	 * opened it before) then the cached dir will be empty and nothing gets
	 * added until a while later when some entries are actually available.
	 */

	attach(filer_window);

	/* Make the window visible */
	number_of_windows++;
	all_filer_windows = g_list_prepend(all_filer_windows, filer_window);
	gtk_widget_show(filer_window->window);

	return filer_window;
}

static gint clear_scanning_display(FilerWindow *filer_window)
{
	if (filer_exists(filer_window))
		filer_set_title(filer_window);
	return FALSE;
}

static void set_scanning_display(FilerWindow *filer_window, gboolean scanning)
{
	if (scanning == filer_window->scanning)
		return;
	filer_window->scanning = scanning;

	if (scanning)
		filer_set_title(filer_window);
	else
		gtk_timeout_add(300, (GtkFunction) clear_scanning_display,
				filer_window);
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options(void)
{
	GtkWidget	*vbox;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	toggle_new_window_on_1 =
		gtk_check_button_new_with_label(
			_("New window on button 1 (RISC OS style)"));
	OPTION_TIP(toggle_new_window_on_1,
			"Clicking with mouse button 1 (usually the "
			"left button) opens a directory in a new window "
			"with this turned on. Clicking with the button-2 "
			"(middle) will reuse the current window.");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_new_window_on_1,
			FALSE, TRUE, 0);

	toggle_menu_on_2 =
		gtk_check_button_new_with_label(
			_("Menu on button 2 (RISC OS style)"));
	OPTION_TIP(toggle_menu_on_2,
			"Use button 2, the middle button (click both buttons "
			"at once on two button mice), to pop up the menu. "
			"If off, use button 3 (right) instead.");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_menu_on_2, FALSE, TRUE, 0);

	toggle_single_click =
		gtk_check_button_new_with_label(_("Single-click nagivation"));
	OPTION_TIP(toggle_single_click,
			"Clicking on an item opens it with this on. Hold down "
			"Control to select the item instead. If off, clicking "
			"once selects an item; double click to open things.");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_single_click, FALSE, TRUE, 0);

	toggle_unique_filer_windows =
		gtk_check_button_new_with_label(_("Unique windows"));
	OPTION_TIP(toggle_unique_filer_windows,
			"If you open a directory and that directory is "
			"already displayed in another window, then this "
			"option causes the other window to be closed.");
	gtk_box_pack_start(GTK_BOX(vbox), toggle_unique_filer_windows,
			FALSE, TRUE, 0);

	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_new_window_on_1),
			o_new_window_on_1);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_menu_on_2),
			collection_menu_button == 2 ? 1 : 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_single_click),
			o_single_click);
	gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(toggle_unique_filer_windows),
			o_unique_filer_windows);
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	o_new_window_on_1 = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_new_window_on_1));

	collection_menu_button = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_menu_on_2)) ? 2 : 3;

	o_single_click = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_single_click));

	o_unique_filer_windows = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_unique_filer_windows));

	collection_single_click = o_single_click ? TRUE : FALSE;
}

static void save_options()
{
	option_write("filer_new_window_on_1", o_new_window_on_1 ? "1" : "0");
	option_write("filer_menu_on_2",
			collection_menu_button == 2 ? "1" : "0");
	option_write("filer_single_click", o_single_click ? "1" : "0");
	option_write("filer_unique_windows",
			o_unique_filer_windows ? "1" : "0");
}

static char *filer_new_window_on_1(char *data)
{
	o_new_window_on_1 = atoi(data) != 0;
	return NULL;
}

static char *filer_menu_on_2(char *data)
{
	collection_menu_button = atoi(data) != 0 ? 2 : 3;
	return NULL;
}

static char *filer_single_click(char *data)
{
	o_single_click = atoi(data) != 0;
	collection_single_click = o_single_click ? TRUE : FALSE;
	return NULL;
}

static char *filer_unique_windows(char *data)
{
	o_unique_filer_windows = atoi(data) != 0;
	return NULL;
}

/* Note that filer_window may not exist after this call. */
void filer_update_dir(FilerWindow *filer_window, gboolean warning)
{
	if (may_rescan(filer_window, warning))
		dir_update(filer_window->directory, filer_window->path);
}

/* Refresh the various caches even if we don't think we need to */
void full_refresh(void)
{
	mount_update(TRUE);
}

/* See whether a filer window with a given path already exists
 * and is different from diff.
 */
static FilerWindow *find_filer_window(char *path, FilerWindow *diff)
{
	GList	*next = all_filer_windows;

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		if (filer_window->panel_type == PANEL_NO &&
			filer_window != diff &&
		    	strcmp(path, filer_window->path) == 0)
		{
			return filer_window;
		}

		next = next->next;
	}
	
	return NULL;
}

/* This path has been mounted/umounted/deleted some files - update all dirs */
void filer_check_mounted(char *path)
{
	GList	*next = all_filer_windows;
	char	*slash;
	int	len;

	len = strlen(path);

	while (next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;

		next = next->next;

		if (strncmp(path, filer_window->path, len) == 0)
		{
			char	s = filer_window->path[len];

			if (s == '/' || s == '\0')
				filer_update_dir(filer_window, FALSE);
		}
	}

	slash = strrchr(path, '/');
	if (slash && slash != path)
	{
		guchar	*parent;

		parent = g_strndup(path, slash - path);

		refresh_dirs(parent);

		g_free(parent);
	}

	pinboard_may_update(path);
}

/* Like minibuffer_show(), except that:
 * - It returns FALSE (to be used from an idle callback)
 * - It checks that the filer window still exists.
 */
static gboolean minibuffer_show_cb(FilerWindow *filer_window)
{
	if (filer_exists(filer_window))
		minibuffer_show(filer_window, MINI_PATH);
	return FALSE;
}

gboolean filer_exists(FilerWindow *filer_window)
{
	GList	*next;

	for (next = all_filer_windows; next; next = next->next)
	{
		FilerWindow *fw = (FilerWindow *) next->data;

		if (fw == filer_window)
			return TRUE;
	}

	return FALSE;
}

static void filer_set_title(FilerWindow *filer_window)
{
	if (filer_window->scanning)
	{
		guchar	*title;

		title = g_strdup_printf(_("%s (Scanning)"), filer_window->path);
		gtk_window_set_title(GTK_WINDOW(filer_window->window),
				title);
		g_free(title);
	}
	else
		gtk_window_set_title(GTK_WINDOW(filer_window->window),
				filer_window->path);
}

/* Reconnect to the same directory (used when the Show Hidden option is
 * toggled).
 */
void filer_detach_rescan(FilerWindow *filer_window)
{
	Directory *dir = filer_window->directory;
	
	g_fscache_data_ref(dir_cache, dir);
	detach(filer_window);
	filer_window->directory = dir;
	attach(filer_window);
}
