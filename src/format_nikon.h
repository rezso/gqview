/*
 *  GQView
 *  (C) 2005 John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */

#ifndef __FORMAT_NIKON_H
#define __FORMAT_NIKON_H


#include "exif.h"

gint format_nikon_raw(unsigned char *data, const guint len,
		      guint *image_offset, guint *exif_offset);

#define FORMAT_RAW_NIKON { "nef", \
			   FORMAT_RAW_MATCH_TIFF_MAKE, 0, "NIKON CORPORATION", 17, \
			   FORMAT_RAW_EXIF_TIFF, NULL, \
			   "Nikon raw", format_nikon_raw }

/* If your format is basically just TIFF with an embedded jpeg,
 * then avoid duplicating code and just stick it here and use the existing nikon parse.
 */
#define FORMAT_RAW_PENTAX { "pef", \
			    FORMAT_RAW_MATCH_TIFF_MAKE, 0, "PENTAX Corporation", 18, \
			    FORMAT_RAW_EXIF_TIFF, NULL, \
			    "Pentax raw", format_nikon_raw }


gint format_nikon_makernote(ExifData *exif, unsigned char *tiff, guint offset,
			    guint size, ExifByteOrder bo);

#define FORMAT_EXIF_NIKON { FORMAT_EXIF_MATCH_MAKERNOTE, "Nikon\x00", 6, "Nikon", format_nikon_makernote }, \
			  { FORMAT_EXIF_MATCH_MAKE,      "NIKON",     5, "Nikon", format_nikon_makernote }


#endif

