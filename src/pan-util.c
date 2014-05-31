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

#include "ui_fileops.h"


/*
 *-----------------------------------------------------------------------------
 * date functions
 *-----------------------------------------------------------------------------
 */

gint pan_date_compare(time_t a, time_t b, PanDateLengthType length)
{
	struct tm ta;
	struct tm tb;

	if (length == PAN_DATE_LENGTH_EXACT) return (a == b);

	if (!localtime_r(&a, &ta) ||
	    !localtime_r(&b, &tb)) return FALSE;

	if (ta.tm_year != tb.tm_year) return FALSE;
	if (length == PAN_DATE_LENGTH_YEAR) return TRUE;

	if (ta.tm_mon != tb.tm_mon) return FALSE;
	if (length == PAN_DATE_LENGTH_MONTH) return TRUE;

	if (length == PAN_DATE_LENGTH_WEEK) return (ta.tm_yday / 7 == tb.tm_yday / 7);

	if (ta.tm_mday != tb.tm_mday) return FALSE;
	if (length == PAN_DATE_LENGTH_DAY) return TRUE;

	return (ta.tm_hour == tb.tm_hour);
}

gint pan_date_value(time_t d, PanDateLengthType length)
{
	struct tm td;

	if (!localtime_r(&d, &td)) return -1;

	switch (length)
		{
		case PAN_DATE_LENGTH_DAY:
			return td.tm_mday;
			break;
		case PAN_DATE_LENGTH_WEEK:
			return td.tm_wday;
			break;
		case PAN_DATE_LENGTH_MONTH:
			return td.tm_mon + 1;
			break;
		case PAN_DATE_LENGTH_YEAR:
			return td.tm_year + 1900;
			break;
		case PAN_DATE_LENGTH_EXACT:
		default:
			break;
		}

	return -1;
}

gchar *pan_date_value_string(time_t d, PanDateLengthType length)
{
	struct tm td;
	gchar buf[128];
	gchar *format = NULL;

	if (!localtime_r(&d, &td)) return g_strdup("");

	switch (length)
		{
		case PAN_DATE_LENGTH_DAY:
			return g_strdup_printf("%d", td.tm_mday);
			break;
		case PAN_DATE_LENGTH_WEEK:
			format = "%A %e";
			break;
		case PAN_DATE_LENGTH_MONTH:
			format = "%B %Y";
			break;
		case PAN_DATE_LENGTH_YEAR:
			return g_strdup_printf("%d", td.tm_year + 1900);
			break;
		case PAN_DATE_LENGTH_EXACT:
		default:
			return g_strdup(text_from_time(d));
			break;
		}


	if (format && strftime(buf, sizeof(buf), format, &td) > 0)
		{
		gchar *ret = g_locale_to_utf8(buf, -1, NULL, NULL, NULL);
		if (ret) return ret;
		}

	return g_strdup("");
}

time_t pan_date_to_time(gint year, gint month, gint day)
{
	struct tm lt;

	lt.tm_sec = 0;
	lt.tm_min = 0;
	lt.tm_hour = 0;
	lt.tm_mday = (day >= 1 && day <= 31) ? day : 1;
	lt.tm_mon = (month >= 1 && month <= 12) ? month - 1 : 0;
	lt.tm_year = year - 1900;
	lt.tm_isdst = 0;

	return mktime(&lt);
}


/*
 *-----------------------------------------------------------------------------
 * folder validation
 *-----------------------------------------------------------------------------
 */

gint pan_is_link_loop(const gchar *s)
{
	gchar *sl;
	struct stat st;
	gint ret = FALSE;

	sl = path_from_utf8(s);

	if (lstat(sl, &st) == 0 && S_ISLNK(st.st_mode))
		{
		gchar *buf;
		gint l;

		buf = g_malloc(st.st_size + 1);
		l = readlink(sl, buf, st.st_size);
		if (l == st.st_size)
			{
			buf[l] = '\0';

			parse_out_relatives(buf);
			l = strlen(buf);

			parse_out_relatives(sl);

			if (buf[0] == '/')
				{
				if (strncmp(sl, buf, l) == 0 &&
				    (sl[l] == '\0' || sl[l] == '/' || l == 1)) ret = TRUE;
				}
			else
				{
				gchar *link_path;

				link_path = concat_dir_and_file(sl, buf);
				parse_out_relatives(link_path);

				if (strncmp(sl, link_path, l) == 0 &&
				    (sl[l] == '\0' || sl[l] == '/' || l == 1)) ret = TRUE;

				g_free(link_path);
				}
			}

		g_free(buf);
		}

	g_free(sl);

	return ret;
}

gint pan_is_ignored(const gchar *s, gint ignore_symlinks)
{
	struct stat st;
	const gchar *n;

	if (!lstat_utf8(s, &st)) return TRUE;

#if 0
	/* normal filesystems have directories with some size or block allocation,
	 * special filesystems (like linux /proc) set both to zero.
	 * enable this check if you enable listing the root "/" folder
	 */
	if (st.st_size == 0 && st.st_blocks == 0) return TRUE;
#endif

	if (S_ISLNK(st.st_mode) && (ignore_symlinks || pan_is_link_loop(s))) return TRUE;

	n = filename_from_path(s);
	if (n && strcmp(n, GQVIEW_RC_DIR) == 0) return TRUE;

	return FALSE;
}

GList *pan_list_tree(const gchar *path, SortType sort, gint ascend,
		     gint ignore_symlinks)
{
	GList *flist = NULL;
	GList *dlist = NULL;
	GList *result;
	GList *folders;

	filelist_read(path, &flist, &dlist);
	if (sort != SORT_NONE)
		{
		flist = filelist_sort(flist, sort, ascend);
		dlist = filelist_sort(dlist, sort, ascend);
		}

	result = flist;
	folders = dlist;
	while (folders)
		{
		FileData *fd;

		fd = folders->data;
		folders = g_list_remove(folders, fd);

		if (!pan_is_ignored(fd->path, ignore_symlinks) &&
		    filelist_read(fd->path, &flist, &dlist))
			{
			if (sort != SORT_NONE)
				{
				flist = filelist_sort(flist, sort, ascend);
				dlist = filelist_sort(dlist, sort, ascend);
				}

			result = g_list_concat(result, flist);
			folders = g_list_concat(dlist, folders);
			}

		file_data_free(fd);
		}

	return result;
}

