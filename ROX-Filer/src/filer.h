/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _FILER_H
#define _FILER_H

#include <gtk/gtk.h>
#include "collection.h"
#include "pixmaps.h"
#include <sys/types.h>
#include <dirent.h>
#include "mount.h"
#include "dir.h"

extern GdkFont	   *fixed_font;

typedef struct _FilerWindow FilerWindow;
typedef enum {LEFT, RIGHT, TOP, BOTTOM} Side;
typedef enum {UNKNOWN_STYLE, LARGE, FULL_INFO} DisplayStyle;

typedef enum
{
	FILER_NEEDS_RESCAN	= 0x01, /* Call may_rescan after scanning */
	FILER_UPDATING		= 0x02, /* (scanning) items may already exist */
} FilerFlags;

#include "type.h"

struct _FilerWindow
{
	GtkWidget	*window;
	char		*path;		/* pathname */
	Collection	*collection;
	gboolean	panel;
	gboolean	temp_item_selected;
	gboolean	show_hidden;
	FilerFlags	flags;
	Side		panel_side;
	time_t		m_time;		/* m-time at last scan */
	int 		(*sort_fn)(const void *a, const void *b);
	DisplayStyle	display_style;

	Directory	*directory;
};

extern FilerWindow 	*window_with_focus;
extern GHashTable	*child_to_filer;

/* Prototypes */
void filer_init();
void filer_opendir(char *path, gboolean panel, Side panel_side);
void update_dir(FilerWindow *filer_window);
void scan_dir(FilerWindow *filer_window);
DirItem *selected_item(Collection *collection);
void change_to_parent(FilerWindow *filer_window);
void filer_style_set(FilerWindow *filer_window, DisplayStyle style);
char *details(DirItem *item);

#endif /* _FILER_H */
