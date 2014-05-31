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

#ifndef VIEW_DIR_LIST_H
#define VIEW_DIR_LIST_H


ViewDirIcons *vdicons_new(const gchar *path);

void vdicons_set_select_func(ViewDirIcons *vdi,
			    void (*func)(ViewDirIcons *vdi, const gchar *path, gpointer data), gpointer data);

void vdicons_set_layout(ViewDirIcons *vdi, LayoutWindow *layout);

gint vdicons_set_path(ViewDirIcons *vdi, const gchar *path);
void vdicons_refresh(ViewDirIcons *vdi);

const gchar *vdicons_list_row_get_path(ViewDirIcons *vdi, gint row);


#endif


