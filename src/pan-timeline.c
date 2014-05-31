/*
 * GQview
 * (C) 2006 John Ellis
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */


#include "gqview.h"
#include "pan-types.h"


void pan_timeline_compute(PanWindow *pw, const gchar *path, gint *width, gint *height)
{
	GList *list;
	GList *work;
	gint x, y;
	time_t tc;
	gint total;
	gint count;
	PanItem *pi_month = NULL;
	PanItem *pi_day = NULL;
	gint month_start;
	gint day_start;
	gint x_width;
	gint y_height;

	list = pan_list_tree(path, SORT_NONE, TRUE, pw->ignore_symlinks);

	if (pw->cache_list && pw->exif_date_enable)
		{
		pw->cache_list = filelist_sort(pw->cache_list, SORT_NAME, TRUE);
		list = filelist_sort(list, SORT_NAME, TRUE);
		pan_cache_sync_date(pw, list);
		}

	pw->cache_list = filelist_sort(pw->cache_list, SORT_TIME, TRUE);
	list = filelist_sort(list, SORT_TIME, TRUE);

	*width = PAN_BOX_BORDER * 2;
	*height = PAN_BOX_BORDER * 2;

	x = 0;
	y = 0;
	month_start = y;
	day_start = month_start;
	x_width = 0;
	y_height = 0;
	tc = 0;
	total = 0;
	count = 0;
	work = list;
	while (work)
		{
		FileData *fd;
		PanItem *pi;

		fd = work->data;
		work = work->next;

		if (!pan_date_compare(fd->date, tc, PAN_DATE_LENGTH_DAY))
			{
			GList *needle;
			gchar *buf;

			if (!pan_date_compare(fd->date, tc, PAN_DATE_LENGTH_MONTH))
				{
				pi_day = NULL;

				if (pi_month)
					{
					x = pi_month->x + pi_month->width + PAN_BOX_BORDER;
					}
				else
					{
					x = PAN_BOX_BORDER;
					}

				y = PAN_BOX_BORDER;

				buf = pan_date_value_string(fd->date, PAN_DATE_LENGTH_MONTH);
				pi = pan_item_text_new(pw, x, y, buf,
						       PAN_TEXT_ATTR_BOLD | PAN_TEXT_ATTR_HEADING,
						       PAN_TEXT_BORDER_SIZE,
						       PAN_TEXT_COLOR, 255);
				g_free(buf);
				y += pi->height;

				pi_month = pan_item_box_new(pw, file_data_new_simple(fd->path),
							    x, y, 0, 0,
							    PAN_BOX_OUTLINE_THICKNESS,
							    PAN_BOX_COLOR, PAN_BOX_ALPHA,
							    PAN_BOX_OUTLINE_COLOR, PAN_BOX_OUTLINE_ALPHA);

				x += PAN_BOX_BORDER;
				y += PAN_BOX_BORDER;
				month_start = y;
				}

			if (pi_day) x = pi_day->x + pi_day->width + PAN_BOX_BORDER;

			tc = fd->date;
			total = 1;
			count = 0;

			needle = work;
			while (needle)
				{
				FileData *nfd;

				nfd = needle->data;
				if (pan_date_compare(nfd->date, tc, PAN_DATE_LENGTH_DAY))
					{
					needle = needle->next;
					total++;
					}
				else
					{
					needle = NULL;
					}
				}

			buf = pan_date_value_string(fd->date, PAN_DATE_LENGTH_WEEK);
			pi = pan_item_text_new(pw, x, y, buf, PAN_TEXT_ATTR_NONE,
					       PAN_TEXT_BORDER_SIZE,
					       PAN_TEXT_COLOR, 255);
			g_free(buf);

			y += pi->height;

			pi_day = pan_item_box_new(pw, file_data_new_simple(fd->path), x, y, 0, 0,
						  PAN_BOX_OUTLINE_THICKNESS,
						  PAN_BOX_COLOR, PAN_BOX_ALPHA,
						  PAN_BOX_OUTLINE_COLOR, PAN_BOX_OUTLINE_ALPHA);

			x += PAN_BOX_BORDER;
			y += PAN_BOX_BORDER;
			day_start = y;
			}

		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			pi = pan_item_image_new(pw, fd, x, y, 10, 10);
			if (pi->width > x_width) x_width = pi->width;
			y_height = pi->height;
			}
		else
			{
			pi = pan_item_thumb_new(pw, fd, x, y);
			x_width = PAN_THUMB_SIZE;
			y_height = PAN_THUMB_SIZE;
			}

		pan_item_size_by_item(pi_day, pi, PAN_BOX_BORDER);
		pan_item_size_by_item(pi_month, pi_day, PAN_BOX_BORDER);

		total--;
		count++;

		if (total > 0 && count < PAN_GROUP_MAX)
			{
			y += y_height + PAN_THUMB_GAP;
			}
		else
			{
			x += x_width + PAN_THUMB_GAP;
			x_width = 0;
			count = 0;

			if (total > 0)
				y = day_start;
			else
				y = month_start;
			}

		pan_item_size_coordinates(pi_month, PAN_BOX_BORDER, width, height);
		}

	g_list_free(list);
}

