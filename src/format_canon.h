/*
 *  GQView
 *  (C) 2005 John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 *
 *
 * Code to add support for Canon CR2 and CRW files, version 0.2
 *
 * Developed by Daniel M. German, dmgerman at uvic.ca 
 *
 * you can find the sources for this patch at http://turingmachine.org/~dmg/libdcraw/gqview/
 *
 */

#ifndef __FORMAT_CANON_H
#define __FORMAT_CANON_H


#include "exif.h"


gint format_canon_raw_crw(unsigned char *data, const guint len,
			  guint *image_offset, guint *exif_offset);

gint format_canon_raw_cr2(unsigned char *data, const guint len,
			  guint *image_offset, guint *exif_offset);

#define FORMAT_RAW_CANON { "crw", \
			   FORMAT_RAW_MATCH_MAGIC,     6, "HEAPCCDR", 8, \
			   FORMAT_RAW_EXIF_NONE, NULL, \
			   "Canon crw", format_canon_raw_crw }, \
			 { "cr2", \
			   FORMAT_RAW_MATCH_TIFF_MAKE, 0, "Canon", 5, \
			   FORMAT_RAW_EXIF_TIFF, NULL, \
			   "Canon cr2", format_canon_raw_cr2 }


gint format_canon_makernote(ExifData *exif, unsigned char *tiff, guint offset,
			    guint size, ExifByteOrder bo);

#define FORMAT_EXIF_CANON { FORMAT_EXIF_MATCH_MAKE, "Canon", 5, "Canon", format_canon_makernote }


#endif


