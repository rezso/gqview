/*
 * GQview
 * (C) 2005 John Ellis
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */


#ifndef CACHE_LOADER_H
#define CACHE_LOADER_H


#include "cache.h"
#include "image-load.h"


typedef struct _CacheLoader CacheLoader;

typedef void (* CacheLoaderDoneFunc)(CacheLoader *cl, gint error, gpointer data);


typedef enum {
	CACHE_LOADER_NONE	= 0,
	CACHE_LOADER_DIMENSIONS	= 1 << 0,
	CACHE_LOADER_DATE	= 1 << 1,
	CACHE_LOADER_MD5SUM	= 1 << 2,
	CACHE_LOADER_SIMILARITY	= 1 << 3
} CacheDataType;

struct _CacheLoader {
	gchar *path;
	CacheData *cd;

	CacheDataType todo_mask;
	CacheDataType done_mask;

	CacheLoaderDoneFunc done_func;
	gpointer done_data;

	gint error;

	ImageLoader *il;
	gint idle_id;
};


CacheLoader *cache_loader_new(const gchar *path, CacheDataType load_mask,
			      CacheLoaderDoneFunc done_func, gpointer done_data);

void cache_loader_free(CacheLoader *cl);


#endif


