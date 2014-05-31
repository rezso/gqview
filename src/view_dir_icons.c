/*
 * GQview
 * (C) 2004 John Ellis
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */

#include "gqview.h"
#include "view_dir_icons.h"

#include "cellrenderericon.h"
#include "dnd.h"
#include "dupe.h"
#include "filelist.h"
#include "layout.h"
#include "layout_image.h"
#include "layout_util.h"
#include "utilops.h"
#include "ui_bookmark.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "ui_tree_edit.h"
#include "thumb.h"

#include <gdk/gdkkeysyms.h> /* for keyboard values */


#define VDLIST_PAD 4

#define THUMB_BORDER_PADDING 2

enum {
	DIR_COLUMN_POINTER = 0,
	DIR_COLUMN_ICON,
	DIR_COLUMN_NAME,
	DIR_COLUMN_COLOR,
	DIR_COLUMN_COUNT
};


static void vdicons_popup_destroy_cb(GtkWidget *widget, gpointer data);
static gint vdicons_auto_scroll_notify_cb(GtkWidget *widget, gint x, gint y, gpointer data);
static gint vdicons_thumb_next(ViewDirIcons *vdi);

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

static gint vdicons_find_row(ViewDirIcons *vdi, FileData *fd, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	gint valid;
	gint row = 0;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));
	valid = gtk_tree_model_get_iter_first(store, iter);
	while (valid)
		{
		FileData *fd_n;
		gtk_tree_model_get(GTK_TREE_MODEL(store), iter, DIR_COLUMN_POINTER, &fd_n, -1);
		if (fd_n == fd) return row;

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), iter);
		row++;
		}

	return -1;
}

static gint vdicons_rename_row_cb(TreeEditData *td, const gchar *old, const gchar *new, gpointer data)
{
	ViewDirIcons *vdi = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	FileData *fd;
	gchar *old_path;
	gchar *new_path;
	gchar *base;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));
	if (!gtk_tree_model_get_iter(store, &iter, td->path)) return FALSE;
	gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
	if (!fd) return FALSE;
	
	old_path = g_strdup(fd->path);

	base = remove_level_from_path(old_path);
	new_path = concat_dir_and_file(base, new);
	g_free(base);

	if (file_util_rename_dir(old_path, new_path, vdi->listview))
		{
		if (vdi->layout && strcmp(vdi->path, old_path) == 0)
			{
			layout_set_path(vdi->layout, new_path);
			}
		else
			{
			vdicons_refresh(vdi);
			}
		}

	g_free(old_path);
	g_free(new_path);
	return FALSE;
}

static void vdicons_rename_by_row(ViewDirIcons *vdi, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	if (vdicons_find_row(vdi, fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));
	tpath = gtk_tree_model_get_path(store, &iter);

	tree_edit_by_path(GTK_TREE_VIEW(vdi->listview), tpath, 0, fd->name,
			  vdicons_rename_row_cb, vdi);
	gtk_tree_path_free(tpath);
}

static FileData *vdicons_row_by_path(ViewDirIcons *vdi, const gchar *path, gint *row)
{
	GList *work;
	gint n;

	if (!path)
		{
		if (row) *row = -1;
		return NULL;
		}

	n = 0;
	work = vdi->list;
	while (work)
		{
		FileData *fd = work->data;
		if (strcmp(fd->path, path) == 0)
			{
			if (row) *row = n;
			return fd;
			}
		work = work->next;
		n++;
		}

	if (row) *row = -1;
	return NULL;
}

static void vdicons_color_set(ViewDirIcons *vdi, FileData *fd, gint color_set)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	if (vdicons_find_row(vdi, fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));
	gtk_list_store_set(GTK_LIST_STORE(store), &iter, DIR_COLUMN_COLOR, color_set, -1);
}

/*
 *-----------------------------------------------------------------------------
 * drop menu (from dnd)
 *-----------------------------------------------------------------------------
 */

static void vdicons_drop_menu_copy_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	const gchar *path;
	GList *list;

	if (!vdi->drop_fd) return;

	path = vdi->drop_fd->path;
	list = vdi->drop_list;
	vdi->drop_list = NULL;

	file_util_copy_simple(list, path);
}

static void vdicons_drop_menu_move_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	const gchar *path;
	GList *list;

	if (!vdi->drop_fd) return;

	path = vdi->drop_fd->path;
	list = vdi->drop_list;

	vdi->drop_list = NULL;

	file_util_move_simple(list, path);
}

static GtkWidget *vdicons_drop_menu(ViewDirIcons *vdi, gint active)
{
	GtkWidget *menu;

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vdicons_popup_destroy_cb), vdi);

	menu_item_add_stock_sensitive(menu, _("_Copy"), GTK_STOCK_COPY, active,
				      G_CALLBACK(vdicons_drop_menu_copy_cb), vdi);
	menu_item_add_sensitive(menu, _("_Move"), active, G_CALLBACK(vdicons_drop_menu_move_cb), vdi);

	menu_item_add_divider(menu);
	menu_item_add_stock(menu, _("Cancel"), GTK_STOCK_CANCEL, NULL, vdi);

	return menu;
}

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */ 

static void vdicons_pop_menu_up_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	gchar *path;

	if (!vdi->path || strcmp(vdi->path, "/") == 0) return;
	path = remove_level_from_path(vdi->path);

	if (vdi->select_func)
		{
		vdi->select_func(vdi, path, vdi->select_data);
		}

	g_free(path);
}

static void vdicons_pop_menu_slide_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	gchar *path;

	if (!vdi->layout || !vdi->click_fd) return;

	path = g_strdup(vdi->click_fd->path);

	layout_set_path(vdi->layout, path);
	layout_select_none(vdi->layout);
	layout_image_slideshow_stop(vdi->layout);
	layout_image_slideshow_start(vdi->layout);

	g_free(path);
}

static void vdicons_pop_menu_slide_rec_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	gchar *path;
	GList *list;

	if (!vdi->layout || !vdi->click_fd) return;

	path = g_strdup(vdi->click_fd->path);

	list = path_list_recursive(path);

	layout_image_slideshow_stop(vdi->layout);
	layout_image_slideshow_start_from_list(vdi->layout, list);

	g_free(path);
}

static void vdicons_pop_menu_dupe(ViewDirIcons *vdi, gint recursive)
{
	DupeWindow *dw;
	const gchar *path;
	GList *list = NULL;

	if (!vdi->click_fd) return;
	path = vdi->click_fd->path;

	if (recursive)
		{
		list = g_list_append(list, g_strdup(path));
		}
	else
		{
		path_list(path, &list, NULL);
		list = path_list_filter(list, FALSE);
		}

	dw = dupe_window_new(DUPE_MATCH_NAME);
	dupe_window_add_files(dw, list, recursive);

	path_list_free(list);
}

static void vdicons_pop_menu_dupe_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	vdicons_pop_menu_dupe(vdi, FALSE);
}

static void vdicons_pop_menu_dupe_rec_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	vdicons_pop_menu_dupe(vdi, TRUE);
}

static void vdicons_pop_menu_new_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	gchar *new_path;
	gchar *buf;

	if (!vdi->path) return;

	buf = concat_dir_and_file(vdi->path, _("new_folder"));
	new_path = unique_filename(buf, NULL, NULL, FALSE);
	g_free(buf);
	if (!new_path) return;

	if (!mkdir_utf8(new_path, 0755))
		{
		gchar *text;

		text = g_strdup_printf(_("Unable to create folder:\n%s"), new_path);
		file_util_warning_dialog(_("Error creating folder"), text, GTK_STOCK_DIALOG_ERROR, vdi->listview);
		g_free(text);
		}
	else
		{
		FileData *fd;

		vdicons_refresh(vdi);
		fd = vdicons_row_by_path(vdi, new_path, NULL);

		vdicons_rename_by_row(vdi, fd);
		}

	g_free(new_path);
}

static void vdicons_pop_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;

	vdicons_rename_by_row(vdi, vdi->click_fd);
}

static void vdicons_pop_menu_delete_cb(GtkWidget *widget, gpointer data)
{
	ViewDirTree *vdi = data;

	if (!vdi->click_fd) return;
	file_util_delete_dir(vdi->click_fd->path, vdi->widget);
}

static void vdicons_pop_menu_tree_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;

	if (vdi->layout) layout_views_set(vdi->layout, TRUE, vdi->layout->icon_view);
}

static void vdicons_pop_menu_icons_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;
	if (vdi->layout) layout_views_set(vdi->layout,FALSE, vdi->layout->icon_view);
}

static void vdicons_pop_menu_refresh_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;

	if (vdi->layout) layout_refresh(vdi->layout);
}

static GtkWidget *vdicons_pop_menu(ViewDirIcons *vdi, FileData *fd)
{
	GtkWidget *menu;
	gint active;

	active = (fd != NULL);

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vdicons_popup_destroy_cb), vdi);

	menu_item_add_stock_sensitive(menu, _("_Up to parent"), GTK_STOCK_GO_UP,
				      (vdi->path && strcmp(vdi->path, "/") != 0),
				      G_CALLBACK(vdicons_pop_menu_up_cb), vdi);

	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("_Slideshow"), active,
				G_CALLBACK(vdicons_pop_menu_slide_cb), vdi);
	menu_item_add_sensitive(menu, _("Slideshow recursive"), active,
				G_CALLBACK(vdicons_pop_menu_slide_rec_cb), vdi);

	menu_item_add_divider(menu);
	menu_item_add_stock_sensitive(menu, _("Find _duplicates..."), GTK_STOCK_FIND, active,
				      G_CALLBACK(vdicons_pop_menu_dupe_cb), vdi);
	menu_item_add_stock_sensitive(menu, _("Find duplicates recursive..."), GTK_STOCK_FIND, active,
				      G_CALLBACK(vdicons_pop_menu_dupe_rec_cb), vdi);

	menu_item_add_divider(menu);

	/* check using . (always row 0) */
	active = (vdi->path && access_file(vdi->path , W_OK | X_OK));
	menu_item_add_sensitive(menu, _("_New folder..."), active,
				G_CALLBACK(vdicons_pop_menu_new_cb), vdi);

	/* ignore .. and . */
	active = (active && fd &&
		  strcmp(fd->name, ".") != 0 &&
		  strcmp(fd->name, "..") != 0 &&
		  access_file(fd->path, W_OK | X_OK));
	menu_item_add_sensitive(menu, _("_Rename..."), active,
				G_CALLBACK(vdicons_pop_menu_rename_cb), vdi);

	menu_item_add_divider(menu);
	menu_item_add_check(menu, _("View as _tree"), FALSE,
			    G_CALLBACK(vdicons_pop_menu_tree_cb), vdi);
	menu_item_add_check(menu,_("View as _icons"), TRUE,
			    G_CALLBACK(vdicons_pop_menu_icons_cb), vdi);
	menu_item_add_stock(menu, _("Re_fresh"), GTK_STOCK_REFRESH,
			    G_CALLBACK(vdicons_pop_menu_refresh_cb), vdi);

	return menu;
}

static void vdicons_popup_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;

	vdicons_color_set(vdi, vdi->click_fd, FALSE);
	vdi->click_fd = NULL;
	vdi->popup = NULL;

	vdicons_color_set(vdi, vdi->drop_fd, FALSE);
	path_list_free(vdi->drop_list);
	vdi->drop_list = NULL;
	vdi->drop_fd = NULL;
}

/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

static GtkTargetEntry vdicons_dnd_drop_types[] = {
	{ "text/uri-list", 0, TARGET_URI_LIST }
};
static gint vdicons_dnd_drop_types_count = 1;

static void vdicons_dest_set(ViewDirIcons *vdi, gint enable)
{
	if (enable)
		{
		gtk_drag_dest_set(vdi->listview,
				  GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
				  vdicons_dnd_drop_types, vdicons_dnd_drop_types_count,
				  GDK_ACTION_MOVE | GDK_ACTION_COPY);
		}
	else
		{
		gtk_drag_dest_unset(vdi->listview);
		}
}

static void vdicons_dnd_get(GtkWidget *widget, GdkDragContext *context,
			   GtkSelectionData *selection_data, guint info,
			   guint time, gpointer data)
{
	ViewDirIcons *vdi = data;
	gchar *path;
	GList *list;
	gchar *text = NULL;
	gint length = 0;

	if (!vdi->click_fd) return;
	path = vdi->click_fd->path;

	switch (info)
		{
		case TARGET_URI_LIST:
		case TARGET_TEXT_PLAIN:
			list = g_list_prepend(NULL, path);
			text = uri_text_from_list(list, &length, (info == TARGET_TEXT_PLAIN));
			g_list_free(list);
			break;
		}
	if (text)
		{
		gtk_selection_data_set (selection_data, selection_data->target,
				8, (guchar *)text, length);
		g_free(text);
		}
}

static void vdicons_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewDirIcons *vdi = data;

	vdicons_color_set(vdi, vdi->click_fd, TRUE);
	vdicons_dest_set(vdi, FALSE);
}

static void vdicons_dnd_end(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewDirIcons *vdi = data;

	vdicons_color_set(vdi, vdi->click_fd, FALSE);

	if (context->action == GDK_ACTION_MOVE)
		{
		vdicons_refresh(vdi);
		}
	vdicons_dest_set(vdi, TRUE);
}

static void vdicons_dnd_drop_receive(GtkWidget *widget,
				    GdkDragContext *context, gint x, gint y,
				    GtkSelectionData *selection_data, guint info,
				    guint time, gpointer data)
{
	ViewDirIcons *vdi = data;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	vdi->click_fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x, y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	if (!fd) return;

	if (info == TARGET_URI_LIST)
		{
		GList *list;
		gint active;

		list = uri_list_from_text((gchar *)selection_data->data, TRUE);
		if (!list) return;

		active = access_file(fd->path, W_OK | X_OK);

		vdicons_color_set(vdi, fd, TRUE);
		vdi->popup = vdicons_drop_menu(vdi, active);
		gtk_menu_popup(GTK_MENU(vdi->popup), NULL, NULL, NULL, NULL, 0, time);

		vdi->drop_fd = fd;
		vdi->drop_list = list;
		}
}

static void vdicons_scroll_to_row(ViewDirIcons *vdi, FileData *fd, gfloat y_align)
{
	GtkTreeIter iter;

	if (GTK_WIDGET_REALIZED(vdi->listview) &&
	    vdicons_find_row(vdi, fd, &iter) >= 0)
		{
		GtkTreeModel *store;
		GtkTreePath *tpath;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));
		tpath = gtk_tree_model_get_path(store, &iter);
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(vdi->listview), tpath, NULL, TRUE, y_align, 0.0);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(vdi->listview), tpath, NULL, FALSE);
		gtk_tree_path_free(tpath);

		if (!GTK_WIDGET_HAS_FOCUS(vdi->listview)) gtk_widget_grab_focus(vdi->listview);
		}
}

static void vdicons_drop_update(ViewDirIcons *vdi, gint x, gint y)
{
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vdi->listview), x, y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	if (fd != vdi->drop_fd)
		{
		vdicons_color_set(vdi, vdi->drop_fd, FALSE);
		vdicons_color_set(vdi, fd, TRUE);
		}

	vdi->drop_fd = fd;
}

static void vdicons_dnd_drop_scroll_cancel(ViewDirIcons *vdi)
{
	if (vdi->drop_scroll_id != -1) g_source_remove(vdi->drop_scroll_id);
	vdi->drop_scroll_id = -1;
}

static gint vdicons_auto_scroll_idle_cb(gpointer data)
{
	ViewDirIcons *vdi = data;

	if (vdi->drop_fd)
		{
		GdkWindow *window;
		gint x, y;
		gint w, h;

		window = vdi->listview->window;
		gdk_window_get_pointer(window, &x, &y, NULL);
		gdk_drawable_get_size(window, &w, &h);
		if (x >= 0 && x < w && y >= 0 && y < h)
			{
			vdicons_drop_update(vdi, x, y);
			}
		}

	vdi->drop_scroll_id = -1;
	return FALSE;
}

static gint vdicons_auto_scroll_notify_cb(GtkWidget *widget, gint x, gint y, gpointer data)
{
	ViewDirIcons *vdi = data;

	if (!vdi->drop_fd || vdi->drop_list) return FALSE;

	if (vdi->drop_scroll_id == -1) vdi->drop_scroll_id = g_idle_add(vdicons_auto_scroll_idle_cb, vdi);

	return TRUE;
}

static gint vdicons_dnd_drop_motion(GtkWidget *widget, GdkDragContext *context,
				   gint x, gint y, guint time, gpointer data)
{
	ViewDirIcons *vdi = data;

	vdi->click_fd = NULL;

	if (gtk_drag_get_source_widget(context) == vdi->listview)
		{
		/* from same window */
		gdk_drag_status(context, 0, time);
		return TRUE;
		}
	else
		{
		gdk_drag_status(context, context->suggested_action, time);
		}

	vdicons_drop_update(vdi, x, y);

        if (vdi->drop_fd)
		{
		GtkAdjustment *adj = gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(vdi->listview));
		widget_auto_scroll_start(vdi->listview, adj, -1, -1, vdicons_auto_scroll_notify_cb, vdi);
		}

	return FALSE;
}

static void vdicons_dnd_drop_leave(GtkWidget *widget, GdkDragContext *context, guint time, gpointer data)
{
	ViewDirIcons *vdi = data;

	if (vdi->drop_fd != vdi->click_fd) vdicons_color_set(vdi, vdi->drop_fd, FALSE);

	vdi->drop_fd = NULL;
}

static void vdicons_dnd_init(ViewDirIcons *vdi)
{
	gtk_drag_source_set(vdi->listview, GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			    dnd_file_drag_types, dnd_file_drag_types_count,
			    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
	g_signal_connect(G_OBJECT(vdi->listview), "drag_data_get",
			 G_CALLBACK(vdicons_dnd_get), vdi);
	g_signal_connect(G_OBJECT(vdi->listview), "drag_begin",
			 G_CALLBACK(vdicons_dnd_begin), vdi);
	g_signal_connect(G_OBJECT(vdi->listview), "drag_end",
			 G_CALLBACK(vdicons_dnd_end), vdi);

	vdicons_dest_set(vdi, TRUE);
	g_signal_connect(G_OBJECT(vdi->listview), "drag_data_received",
			 G_CALLBACK(vdicons_dnd_drop_receive), vdi);
	g_signal_connect(G_OBJECT(vdi->listview), "drag_motion",
			 G_CALLBACK(vdicons_dnd_drop_motion), vdi);
	g_signal_connect(G_OBJECT(vdi->listview), "drag_leave",
			 G_CALLBACK(vdicons_dnd_drop_leave), vdi);
}

/*
 *-----------------------------------------------------------------------------
 * main
 *-----------------------------------------------------------------------------
 */ 

static void vdicons_select_row(ViewDirIcons *vdi, FileData *fd)
{
	if (fd && vdi->select_func)
		{
		gchar *path;

		path = g_strdup(fd->path);
		vdi->select_func(vdi, path, vdi->select_data);
		g_free(path);
		}
}

const gchar *vdicons_row_get_path(ViewDirIcons *vdi, gint row)
{
	FileData *fd;

	fd = g_list_nth_data(vdi->list, row);

	if (fd) return fd->path;

	return NULL;
}

static gint vdicons_find_iter(ViewDirIcons *vdi, FileData *fd, GtkTreeIter *iter, gint *column)
{
	GtkTreeModel *store;
	gint n;

	store = gtk_tree_view_get_model( GTK_TREE_VIEW(vdi->listview));

	n = g_list_index(vdi->list, fd);

	if (n<0) return FALSE;

	return gtk_tree_model_iter_nth_child(store, iter, NULL, n);
}

static void vdicons_set_thumb(ViewDirIcons *vdi, FileData *fd, GdkPixbuf *pb)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	if (!vdicons_find_iter(vdi, fd, &iter, NULL)) return;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));

	if (pb) g_object_ref(pb);
	if (fd->pixbuf) g_object_unref(fd->pixbuf);
	fd->pixbuf = pb;

	gtk_list_store_set(GTK_LIST_STORE(store), &iter, DIR_COLUMN_ICON, pb, -1);
}

static void vdicons_thumb_status(ViewDirIcons *vdi, gdouble val, const gchar *text)
{
	if ( vdi->func_thumb_status )
		{
		vdi->func_thumb_status(vdi, val, text, vdi->data_thumb_status);
		}
}

static void vdicons_thumb_cleanup(ViewDirIcons *vdi)
{
	vdicons_thumb_status(vdi,0.0,NULL);

	g_list_free(vdi->thumbs_list);
	vdi->thumbs_list = NULL;
	vdi->thumbs_count = 0;
	vdi->thumbs_running = FALSE;
 
	thumb_loader_free(vdi->thumbs_loader);
	vdi->thumbs_loader = NULL;

	vdi->thumbs_fd = NULL;
}

static void vdicons_thumb_stop(ViewDirIcons *vdi)
{
	if (vdi->thumbs_running) vdicons_thumb_cleanup(vdi);
}

static void vdicons_thumb_update(ViewDirIcons *vdi)
{
	vdicons_thumb_stop(vdi);

	vdicons_thumb_status(vdi, 0.0, _("Loading thumbs..."));
	vdi->thumbs_running = TRUE;

	while (vdicons_thumb_next(vdi));
}

static void vdicons_populate(ViewDirIcons *vdi)
{
	GtkListStore *store;
	GList *work;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview)));
	gtk_list_store_clear(store);

	work = vdi->list;
	while (work)
		{
		FileData *fd;
		GtkTreeIter iter;
		GdkPixbuf *pixbuf;

		fd = work->data;

		if (access_file(fd->path, R_OK | X_OK) && fd->name)
			{
			if (fd->name[0] == '.' && fd->name[1] == '\0')
				{
				pixbuf = vdi->pf->open;
				}
			else if (fd->name[0] == '.' && fd->name[1] == '.' && fd->name[2] == '\0')
				{
				pixbuf = vdi->pf->parent;
				}
			else
				{
				pixbuf = vdi->pf->close;
				}
			}
		else
			{
			pixbuf = vdi->pf->deny;
			}

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
				   DIR_COLUMN_POINTER, fd,
				   DIR_COLUMN_ICON, pixbuf,
				   DIR_COLUMN_NAME, fd->name, -1);

		work = work->next;
		}

	vdi->click_fd = NULL;
	vdi->drop_fd = NULL;

	vdicons_thumb_update(vdi);
}

gint vdicons_set_path(ViewDirIcons *vdi, const gchar *path)
{
	gint ret;
	FileData *fd;
	gchar *old_path = NULL;

	if (!path) return FALSE;
	if (vdi->path && strcmp(path, vdi->path) == 0) return TRUE;

	if (vdi->path)
		{
		gchar *base;

		base = remove_level_from_path(vdi->path);
		if (strcmp(base, path) == 0)
			{
			old_path = g_strdup(filename_from_path(vdi->path));
			}
		g_free(base);
		}

	g_free(vdi->path);
	vdi->path = g_strdup(path);

	filelist_free(vdi->list);
	vdi->list = NULL;

	ret = filelist_read(vdi->path, NULL, &vdi->list);

	vdi->list = filelist_sort(vdi->list, SORT_NAME, TRUE);

	/* add . and .. */

	if (strcmp(vdi->path, "/") != 0)
		{
		fd = g_new0(FileData, 1);
		fd->path = remove_level_from_path(vdi->path);
		fd->name = "..";
		vdi->list = g_list_prepend(vdi->list, fd);
		}
	/*
	fd = g_new0(FileData, 1);
	fd->path = g_strdup(vdi->path);
	fd->name = ".";
	vdi->list = g_list_prepend(vdi->list, fd);
	*/
	vdicons_populate(vdi);

	if (old_path)
		{
		/* scroll to make last path visible */
		FileData *found = NULL;
		GList *work;

		work = vdi->list;
		while (work && !found)
			{
			FileData *fd = work->data;
			if (strcmp(old_path, fd->name) == 0) found = fd;
			work = work->next;
			}

		if (found) vdicons_scroll_to_row(vdi, found, 0.5);

		g_free(old_path);
		return ret;
		}

	if (GTK_WIDGET_REALIZED(vdi->listview))
		{
		gtk_tree_view_scroll_to_point(GTK_TREE_VIEW(vdi->listview), 0, 0);
		}

	return ret;
}

void vdicons_refresh(ViewDirIcons *vdi)
{
	gchar *path;

	path = g_strdup(vdi->path);
	vdi->path = NULL;
	vdicons_set_path(vdi, path);
	g_free(path);
}

static void vdicons_menu_position_cb(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
{
	ViewDirIcons *vdi = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreePath *tpath;
	gint cw, ch;

	if (vdicons_find_row(vdi, vdi->click_fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview));
	tpath = gtk_tree_model_get_path(store, &iter);
	tree_view_get_cell_clamped(GTK_TREE_VIEW(vdi->listview), tpath, 0, TRUE, x, y, &cw, &ch);
	gtk_tree_path_free(tpath);
	*y += ch;
	popup_menu_position_clamp(menu, x, y, 0);
}

static gint vdicons_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	ViewDirIcons *vdi = data;
	GtkTreePath *tpath;
	
	if (event->keyval != GDK_Menu) return FALSE;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(vdi->listview), &tpath, NULL);
	if (tpath)
		{
		GtkTreeModel *store;
		GtkTreeIter iter;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &vdi->click_fd, -1);
		
		gtk_tree_path_free(tpath);
		}
	else
		{
		vdi->click_fd = NULL;
		}

	vdicons_color_set(vdi, vdi->click_fd, TRUE);

	vdi->popup = vdicons_pop_menu(vdi, vdi->click_fd);

	gtk_menu_popup(GTK_MENU(vdi->popup), NULL, NULL, vdicons_menu_position_cb, vdi, 0, GDK_CURRENT_TIME);

	return TRUE;
}

static gint vdicons_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewDirIcons *vdi = data;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, NULL, FALSE);
		gtk_tree_path_free(tpath);
		}

	vdi->click_fd = fd;
	vdicons_color_set(vdi, vdi->click_fd, TRUE);

	if (bevent->button == 3)
		{
		vdi->popup = vdicons_pop_menu(vdi, vdi->click_fd);
		gtk_menu_popup(GTK_MENU(vdi->popup), NULL, NULL, NULL, NULL,
			       bevent->button, bevent->time);
		}

	return TRUE;
}

static gint vdicons_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewDirIcons *vdi = data;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = NULL;

	vdicons_color_set(vdi, vdi->click_fd, FALSE);

	if (bevent->button != 1) return TRUE;

	if ((bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	if (fd && vdi->click_fd == fd)
		{
		vdicons_select_row(vdi, vdi->click_fd);
		}

	return TRUE;
}

static void vdicons_select_cb(GtkTreeView *tview, GtkTreePath *tpath, GtkTreeViewColumn *column, gpointer data)
{
	ViewDirIcons *vdi = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	FileData *fd;

	store = gtk_tree_view_get_model(tview);
	gtk_tree_model_get_iter(store, &iter, tpath);
	gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);

	vdicons_select_row(vdi, fd);
}

static GdkColor *vdicons_color_shifted(GtkWidget *widget)
{
	static GdkColor color;
	static GtkWidget *done = NULL;

	if (done != widget)
		{
		GtkStyle *style;

		style = gtk_widget_get_style(widget);
		memcpy(&color, &style->base[GTK_STATE_NORMAL], sizeof(color));
		shift_color(&color, -1, 0);
		done = widget;
		}

	return &color;
}

static void vdicons_color_cb(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
			    GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	ViewDirIcons *vdi = data;
	gboolean set;

	gtk_tree_model_get(tree_model, iter, DIR_COLUMN_COLOR, &set, -1);
	g_object_set(G_OBJECT(cell),
		     "cell-background-gdk", vdicons_color_shifted(vdi->listview),
		     "cell-background-set", set, NULL);
}

static void vdicons_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewDirIcons *vdi = data;

	if (vdi->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(vdi->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, 0, NULL, vdi);
		gtk_widget_destroy(vdi->popup);
		}

	vdicons_dnd_drop_scroll_cancel(vdi);
	widget_auto_scroll_stop(vdi->listview);

	path_list_free(vdi->drop_list);

	vdicons_thumb_cleanup(vdi);
	folder_icons_free(vdi->pf);

	g_free(vdi->path);
	filelist_free(vdi->list);
	g_free(vdi);
}

ViewDirIcons *vdicons_new(const gchar *path)
{
	ViewDirIcons *vdi;
	GtkListStore *store;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	vdi = g_new0(ViewDirIcons, 1);

	vdi->path = NULL;
	vdi->list = NULL;
	vdi->click_fd = NULL;

	vdi->drop_fd = NULL;
	vdi->drop_list = NULL;

	vdi->drop_scroll_id = -1;

	vdi->popup = NULL;

	vdi->widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(vdi->widget), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vdi->widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	g_signal_connect(G_OBJECT(vdi->widget), "destroy",
			 G_CALLBACK(vdicons_destroy_cb), vdi);

	store = gtk_list_store_new(4, G_TYPE_POINTER, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN);
	vdi->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(vdi->listview), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(vdi->listview), FALSE);
	g_signal_connect(G_OBJECT(vdi->listview), "row_activated",
			 G_CALLBACK(vdicons_select_cb), vdi);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vdi->listview));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	renderer = gqv_cell_renderer_icon_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	g_object_set(G_OBJECT(renderer), "xpad", THUMB_BORDER_PADDING * 2,
		                        "ypad", THUMB_BORDER_PADDING,
					"mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, 
					"show_text", TRUE, NULL);
	g_object_set(G_OBJECT(renderer), "fixed_width", thumb_max_width,
		        		 "fixed_height", thumb_max_height,NULL);

	gtk_tree_view_column_add_attribute(column, renderer, "text", DIR_COLUMN_NAME);
	gtk_tree_view_column_add_attribute(column, renderer, "pixbuf", DIR_COLUMN_ICON);

	gtk_tree_view_append_column(GTK_TREE_VIEW(vdi->listview), column);

	g_signal_connect(G_OBJECT(vdi->listview), "key_press_event",
			   G_CALLBACK(vdicons_press_key_cb), vdi);
	gtk_container_add(GTK_CONTAINER(vdi->widget), vdi->listview);
	gtk_widget_show(vdi->listview);

	vdi->pf = folder_icons_new(vdi->widget, GTK_ICON_SIZE_DIALOG);

	vdicons_dnd_init(vdi);

	g_signal_connect(G_OBJECT(vdi->listview), "button_press_event",
			 G_CALLBACK(vdicons_press_cb), vdi);
	g_signal_connect(G_OBJECT(vdi->listview), "button_release_event",
			 G_CALLBACK(vdicons_release_cb), vdi);

	if (path) vdicons_set_path(vdi, path);

	return vdi;
}

void vdicons_set_select_func(ViewDirIcons *vdi,
			    void (*func)(ViewDirIcons *vdi, const gchar *path, gpointer data), gpointer data)
{
	vdi->select_func = func;
	vdi->select_data = data;
}

void vdicons_set_layout(ViewDirIcons *vdi, LayoutWindow *layout)
{
	vdi->layout = layout;
}

/* thumbnail related functions */


static void vdicons_thumb_do(ViewDirIcons *vdi, ThumbLoader *tl, FileData *fd)
{
	GdkPixbuf *pixbuf;

	if (!fd) return;

	pixbuf = thumb_loader_get_pixbuf(tl, TRUE);
	vdicons_set_thumb(vdi, fd, pixbuf);
	g_object_unref(pixbuf);

	vdicons_thumb_status(vdi, (gdouble)(vdi->thumbs_count) / g_list_length(vdi->list), _("Loading thumbs..."));
}


static void vdicons_thumb_error_cb(ThumbLoader *tl, gpointer data)
{
	ViewDirIcons *vdi = data;

	if (vdi->thumbs_fd && vdi->thumbs_loader==tl)
		{
		vdicons_thumb_do(vdi, tl, vdi->thumbs_fd);
		}

	while (vdicons_thumb_next(vdi));
}


static void vdicons_thumb_done_cb(ThumbLoader *tl, gpointer data)
{
	ViewDirIcons *vdi = data;

	if (vdi->thumbs_fd && vdi->thumbs_loader == tl)
		{
		vdicons_thumb_do(vdi, tl, vdi->thumbs_fd);
		}

	while (vdicons_thumb_next(vdi));
}


static gint vdicons_thumb_next(ViewDirIcons *vdi)
{
	GtkTreePath *tpath;
	FileData *fd = NULL;

	if (!GTK_WIDGET_REALIZED(vdi->listview))
		{
		vdicons_thumb_status(vdi, 0.0, NULL);
		return FALSE;
		}

	if ( gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vdi->listview), 
				               0,0, &tpath, NULL, NULL, NULL) )
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gint valid = TRUE;

		/* try to find the next visible item w/o a thumbnail */
		store = gtk_tree_view_get_model( GTK_TREE_VIEW(vdi->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_path_free(tpath);

		while ( !fd && valid && 
		tree_view_row_get_visibility(GTK_TREE_VIEW(vdi->listview), &iter, FALSE) == 0 )
			{
			FileData *tfd;
			gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &tfd, -1);
			if (tfd && !tfd->pixbuf) fd = tfd;

			valid=gtk_tree_model_iter_next(store, &iter);
			}
		}

	/* otherwise just find the first undone thumbnail */
	if (!fd)
		{
		GList *work = vdi->list;
		while ( work && !fd)
			{
			FileData *fd_p = work->data;
			work = work->next;

			if ( !fd_p->pixbuf) fd = fd_p;
			}
		}

	if (!fd)
		{
		/* done */
		vdicons_thumb_cleanup(vdi);
		return FALSE;
		}

	vdi->thumbs_count++;
	vdi->thumbs_fd = fd;

	thumb_loader_free(vdi->thumbs_loader);

	vdi->thumbs_loader = thumb_loader_new(thumb_max_width, thumb_max_height);
	thumb_loader_set_callbacks(vdi->thumbs_loader,
			vdicons_thumb_done_cb,
			vdicons_thumb_error_cb,
			NULL,
			vdi);

		{
		GDir *dir;
		const gchar *filename;
		filename=NULL;
		gchar *path;
		path=NULL;

		dir = g_dir_open(fd->path, 0, NULL);

		if (dir)
			{
			/* find first dir entry which exists in the filter */
			while ((filename = g_dir_read_name(dir))
			      && !filter_name_exists(filename) );

			path = g_strconcat(fd->path,"/",filename,NULL);
			g_dir_close(dir);
			}


		if ( !thumb_loader_start(vdi->thumbs_loader,path))
			{
			GtkTreeModel *store;
			GtkTreeIter  iter;

			if (debug) printf("thumb loader start failed %s\n", 
					vdi->thumbs_loader->path);

			if (vdicons_find_iter(vdi, fd, &iter, NULL) &&
				(store = gtk_tree_view_get_model(GTK_TREE_VIEW(vdi->listview))))
				{
				gtk_tree_model_get(store, &iter, DIR_COLUMN_ICON, &fd->pixbuf,-1); 
				}
			g_free(path);
			return TRUE;
			}

		if(path) g_free(path);

		}

	return FALSE;
}

