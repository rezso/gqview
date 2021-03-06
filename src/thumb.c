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
#include "thumb.h"

#include "cache.h"
#include "image-load.h"
#include "pixbuf_util.h"
#include "thumb_standard.h"
#include "ui_fileops.h"

#include <utime.h>


static void thumb_loader_error_cb(ImageLoader *il, gpointer data);
static void thumb_loader_setup(ThumbLoader *tl, gchar *path);

static gint normalize_thumb(gint *width, gint *height, gint max_w, gint max_h);


/*
 *-----------------------------------------------------------------------------
 * thumbnail routines: creation, caching, and maintenance (public)
 *-----------------------------------------------------------------------------
 */

static gint thumb_loader_save_to_cache(ThumbLoader *tl)
{
	gchar *cache_dir;
	gint success = FALSE;
	mode_t mode = 0755;

	if (!tl || !tl->pixbuf) return FALSE;

	cache_dir = cache_get_location(CACHE_TYPE_THUMB, tl->path, FALSE, &mode);

	if (cache_ensure_dir_exists(cache_dir, mode))
		{
		gchar *cache_path;
		gchar *pathl;

		cache_path = g_strconcat(cache_dir, "/", filename_from_path(tl->path),
					 GQVIEW_CACHE_EXT_THUMB, NULL);

		if (debug) printf("Saving thumb: %s\n", cache_path);

		pathl = path_from_utf8(cache_path);
		success = pixbuf_to_file_as_png(tl->pixbuf, pathl);
		if (success)
			{
			struct utimbuf ut;
			/* set thumb time to that of source file */

			ut.actime = ut.modtime = filetime(tl->path);
			if (ut.modtime > 0)
				{
				utime(pathl, &ut);
				}
			}
		else
			{
			if (debug) printf("Saving failed: %s\n", pathl);
			}

		g_free(pathl);
		g_free(cache_path);
		}

	g_free(cache_dir);

	return success;
}

static gint thumb_loader_mark_failure(ThumbLoader *tl)
{
	gchar *cache_dir;
	gint success = FALSE;
	mode_t mode = 0755;
	
	if (!tl) return FALSE;

	cache_dir = cache_get_location(CACHE_TYPE_THUMB, tl->path, FALSE, &mode);

	if (cache_ensure_dir_exists(cache_dir, mode))
		{
		gchar *cache_path;
		gchar *pathl;
		FILE *f;

		cache_path = g_strconcat(cache_dir, "/", filename_from_path(tl->path),
					 GQVIEW_CACHE_EXT_THUMB, NULL);

		if (debug) printf("marking thumb failure: %s\n", cache_path);

		pathl = path_from_utf8(cache_path);
		f = fopen(pathl, "w");
		if (f)
			{
			struct utimbuf ut;

			fclose (f);

			ut.actime = ut.modtime = filetime(tl->path);
			if (ut.modtime > 0)
				{
				utime(pathl, &ut);
				}

			success = TRUE;
			}

		g_free(pathl);
		g_free(cache_path);
		}

	g_free(cache_dir);
	return success;
}

static void thumb_loader_percent_cb(ImageLoader *il, gdouble percent, gpointer data)
{
	ThumbLoader *tl = data;

	tl->percent_done = percent;

	if (tl->func_progress) tl->func_progress(tl, tl->data);
}

static void thumb_loader_done_cb(ImageLoader *il, gpointer data)
{
	ThumbLoader *tl = data;
	GdkPixbuf *pixbuf;
	gint pw, ph;
	gint save;

	if (debug) printf("thumb done: %s\n", tl->path);

	pixbuf = image_loader_get_pixbuf(tl->il);
	if (!pixbuf)
		{
		if (debug) printf("...but no pixbuf: %s\n", tl->path);
		thumb_loader_error_cb(tl->il, tl);
		return;
		}

	pw = gdk_pixbuf_get_width(pixbuf);
	ph = gdk_pixbuf_get_height(pixbuf);

	if (tl->cache_hit && pw != tl->max_w && ph != tl->max_h)
		{
		/* requested thumbnail size may have changed, load original */
		if (debug) printf("thumbnail size mismatch, regenerating: %s\n", tl->path);
		tl->cache_hit = FALSE;

		thumb_loader_setup(tl, tl->path);

		if (!image_loader_start(tl->il, thumb_loader_done_cb, tl))
			{
			image_loader_free(tl->il);
			tl->il = NULL;

			if (debug) printf("regeneration failure: %s\n", tl->path);
			thumb_loader_error_cb(tl->il, tl);
			}
		return;
		}

	/* scale ?? */

	if (pw > tl->max_w || ph > tl->max_h)
		{
		gint w, h;

		if (((double)tl->max_w / pw) < ((double)tl->max_h / ph))
			{
			w = tl->max_w;
			h = (double)w / pw * ph;
			if (h < 1) h = 1;
			}
		else
			{
			h = tl->max_h;
			w = (double)h / ph * pw;
			if (w < 1) w = 1;
			}

		tl->pixbuf = gdk_pixbuf_scale_simple(pixbuf, w, h, (GdkInterpType)thumbnail_quality);
		save = TRUE;
		}
	else
		{
		tl->pixbuf = pixbuf;
		g_object_ref(tl->pixbuf);
		save = il->shrunk;
		}

	/* save it ? */
	if (tl->cache_enable && save)
		{
		thumb_loader_save_to_cache(tl);
		}

	if (tl->func_done) tl->func_done(tl, tl->data);
}

static void thumb_loader_error_cb(ImageLoader *il, gpointer data)
{
	ThumbLoader *tl = data;

	/* if at least some of the image is available, go to done_cb */
	if (image_loader_get_pixbuf(tl->il) != NULL)
		{
		thumb_loader_done_cb(il, data);
		return;
		}

	if (debug) printf("thumb error: %s\n", tl->path);

	image_loader_free(tl->il);
	tl->il = NULL;

	if (tl->func_error) tl->func_error(tl, tl->data);
}

static gint thumb_loader_done_delay_cb(gpointer data)
{
	ThumbLoader *tl = data;

	tl->idle_done_id = -1;

	if (tl->func_done) tl->func_done(tl, tl->data);

	return FALSE;
}

static void thumb_loader_delay_done(ThumbLoader *tl)
{
	if (tl->idle_done_id == -1) tl->idle_done_id = g_idle_add(thumb_loader_done_delay_cb, tl);
}

static void thumb_loader_setup(ThumbLoader *tl, gchar *path)
{
	image_loader_free(tl->il);
	tl->il = image_loader_new(path);

	if (thumbnail_fast)
		{
		/* this will speed up jpegs by up to 3x in some cases */
		image_loader_set_requested_size(tl->il, tl->max_w, tl->max_h);
		}

	image_loader_set_error_func(tl->il, thumb_loader_error_cb, tl);
	if (tl->func_progress) image_loader_set_percent_func(tl->il, thumb_loader_percent_cb, tl);
}

void thumb_loader_set_callbacks(ThumbLoader *tl,
				ThumbLoaderFunc func_done,
				ThumbLoaderFunc func_error,
				ThumbLoaderFunc func_progress,
				gpointer data)
{
	if (!tl) return;

	if (tl->standard_loader)
		{
		thumb_loader_std_set_callbacks((ThumbLoaderStd *)tl,
					       (ThumbLoaderStdFunc) func_done,
					       (ThumbLoaderStdFunc) func_error,
					       (ThumbLoaderStdFunc) func_progress,
					       data);
		return;
		}

	tl->func_done = func_done;
	tl->func_error = func_error;
	tl->func_progress = func_progress;

	tl->data = data;
}

void thumb_loader_set_cache(ThumbLoader *tl, gint enable_cache, gint local, gint retry_failed)
{
        if (!tl) return;

	if (tl->standard_loader)
		{
		thumb_loader_std_set_cache((ThumbLoaderStd *)tl, enable_cache, local, retry_failed);
		return;
		}

	tl->cache_enable = enable_cache;
}


gint thumb_loader_start(ThumbLoader *tl, const gchar *path)
{
	gchar *cache_path = NULL;

	if (!tl) return FALSE;

	if (tl->standard_loader)
		{
		return thumb_loader_std_start((ThumbLoaderStd *)tl, path);
		}

	if (!tl->path && !path) return FALSE;

	if (!tl->path) tl->path = g_strdup(path);

	if (tl->cache_enable)
		{
		cache_path = cache_find_location(CACHE_TYPE_THUMB, tl->path);

		if (cache_path)
			{
			if (cache_time_valid(cache_path, tl->path))
				{
				if (debug) printf("Found in cache:%s\n", tl->path);

				if (filesize(cache_path) == 0)
					{
					if (debug) printf("Broken image mark found:%s\n", cache_path);
					g_free(cache_path);
					return FALSE;
					}

				if (debug) printf("Cache location:%s\n", cache_path);
				}
			else
				{
				g_free(cache_path);
				cache_path = NULL;
				}
			}
		}

	if (cache_path)
		{
		thumb_loader_setup(tl, cache_path);
		g_free(cache_path);
		tl->cache_hit = TRUE;
		}
	else
		{
		thumb_loader_setup(tl, tl->path);
		}

	if (!image_loader_start(tl->il, thumb_loader_done_cb, tl))
		{
		/* try from original if cache attempt */
		if (tl->cache_hit)
			{
			tl->cache_hit = FALSE;
			print_term(_("Thumbnail image in cache failed to load, trying to recreate.\n"));

			thumb_loader_setup(tl, tl->path);
			if (image_loader_start(tl->il, thumb_loader_done_cb, tl)) return TRUE;
			}
		/* mark failed thumbnail in cache with 0 byte file */
		if (tl->cache_enable)
			{
			thumb_loader_mark_failure(tl);
			}
		
		image_loader_free(tl->il);
		tl->il = NULL;
		return FALSE;
		}

	return TRUE;
}


GdkPixbuf *thumb_loader_get_pixbuf(ThumbLoader *tl, gint with_fallback)
{
	GdkPixbuf *pixbuf;

	if (tl && tl->standard_loader)
		{
		return thumb_loader_std_get_pixbuf((ThumbLoaderStd *)tl, with_fallback);
		}

	if (tl && tl->pixbuf)
		{
		pixbuf = tl->pixbuf;
		g_object_ref(pixbuf);
		}
	else if (with_fallback)
		{
		gint w, h;

		pixbuf = pixbuf_inline(PIXBUF_INLINE_BROKEN);
		w = gdk_pixbuf_get_width(pixbuf);
		h = gdk_pixbuf_get_height(pixbuf);
		if ((w > tl->max_w || h > tl->max_h) &&
		    normalize_thumb(&w, &h, tl->max_w, tl->max_h))
			{
			GdkPixbuf *tmp;

			tmp = pixbuf;
			pixbuf = gdk_pixbuf_scale_simple(tmp, w, h, GDK_INTERP_NEAREST);
			g_object_unref(tmp);
			}
		}
	else
		{
		pixbuf = NULL;
		}

	return pixbuf;
}


ThumbLoader *thumb_loader_new(gint width, gint height)
{
	ThumbLoader *tl;

	if (thumbnail_spec_standard)
		{
		return (ThumbLoader *)thumb_loader_std_new(width, height);
		}

	tl = g_new0(ThumbLoader, 1);
	tl->standard_loader = FALSE;
	tl->path = NULL;
	tl->cache_enable = enable_thumb_caching;
	tl->cache_hit = FALSE;
	tl->percent_done = 0.0;
	tl->max_w = width;
	tl->max_h = height;

	tl->il = NULL;

	tl->idle_done_id = -1;

	return tl;
}

void thumb_loader_free(ThumbLoader *tl)
{
	if (!tl) return;

	if (tl->standard_loader)
		{
		thumb_loader_std_free((ThumbLoaderStd *)tl);
		return;
		}

	if (tl->pixbuf) g_object_unref(tl->pixbuf);
	image_loader_free(tl->il);
	g_free(tl->path);

	if (tl->idle_done_id != -1) g_source_remove(tl->idle_done_id);

	g_free(tl);
}

static gint normalize_thumb(gint *width, gint *height, gint max_w, gint max_h)
{
	gdouble scale;
	gint new_w, new_h;

	scale = MIN((gdouble) max_w / *width, (gdouble) max_h / *height);
	new_w = *width * scale;
	new_h = *height * scale;

	if (new_w != *width || new_h != *height)
		{
		*width = new_w;
		*height = new_h;
		return TRUE;
		}

	return FALSE;
}

static void free_rgb_buffer(guchar *pixels, gpointer data)
{
	g_free(pixels);
}

