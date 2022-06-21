/*
	Copyright (C) 2006  Serge "(515)" Ziryukin, Forest "LordHavoc" Hale

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

*/

//[515]: png implemented into DP ONLY FOR TESTING 2d stuff with csqc
// so delete this bullshit :D
//
//LordHavoc: rewrote most of this.

#include "quakedef.h"
#include "image.h"
#include "image_png.h"


#define PNG_SKIP_SETJMP_CHECK
#include <png.h>

/*
=================================================================

	PNG decompression

=================================================================
*/

// this struct is only used for status information during loading
static struct
{
	const unsigned char	*tmpBuf;
	int		tmpBuflength;
	int		tmpi;
	//int		FBgColor;
	//int		FTransparent;
	unsigned int	FRowBytes;
	//double	FGamma;
	//double	FScreenGamma;
	unsigned char	**FRowPtrs;
	unsigned char	*Data;
	//char	*Title;
	//char	*Author;
	//char	*Description;
	int		BitDepth;
	int		BytesPerPixel;
	int		ColorType;
	png_uint_32	Height; // retarded libpng 1.2 pngconf.h uses long (64bit/32bit depending on arch)
	png_uint_32	Width; // retarded libpng 1.2 pngconf.h uses long (64bit/32bit depending on arch)
	int		Interlace;
	int		Compression;
	int		Filter;
	//double	LastModified;
	//int		Transparent;
	qfile_t *outfile;
} my_png;

//LordHavoc: removed __cdecl prefix, added overrun protection, and rewrote this to be more efficient
static void PNG_fReadData(png_structp png, unsigned char *data, size_t length)
{
	size_t l;
	l = my_png.tmpBuflength - my_png.tmpi;
	if (l < length)
	{
		Con_Printf("PNG_fReadData: overrun by %i bytes\n", (int)(length - l));
		// a read going past the end of the file, fill in the remaining bytes
		// with 0 just to be consistent
		memset(data + l, 0, length - l);
		length = l;
	}
	memcpy(data, my_png.tmpBuf + my_png.tmpi, length);
	my_png.tmpi += (int)length;
	//Com_HexDumpToConsole(data, (int)length);
}

static void PNG_fWriteData(png_structp png, unsigned char *data, size_t length)
{
	FS_Write(my_png.outfile, data, length);
}

static void PNG_fFlushData(png_structp png)
{
}

static void PNG_error_fn(png_structp png, const char *message)
{
	Con_Printf("PNG_LoadImage: error: %s\n", message);
}

static void PNG_warning_fn(png_structp png, const char *message)
{
	Con_DPrintf("PNG_LoadImage: warning: %s\n", message);
}

unsigned char *PNG_LoadImage_BGRA (const unsigned char *raw, int filesize, int *miplevel)
{
	unsigned int c;
	unsigned int	y;
	png_structp png;
	png_infop pnginfo;
	unsigned char *imagedata = NULL;
	unsigned char ioBuffer[8192];

	// FIXME: register an error handler so that abort() won't be called on error

	if(png_sig_cmp((png_bytep)raw, 0, filesize))
		return NULL;
	png = (void *)png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, PNG_error_fn, PNG_warning_fn);
	if(!png)
		return NULL;

	// this must be memset before the setjmp error handler, because it relies
	// on the fields in this struct for cleanup
	memset(&my_png, 0, sizeof(my_png));

	// NOTE: this relies on jmp_buf being the first thing in the png structure
	// created by libpng! (this is correct for libpng 1.2.x)
	if (setjmp(png_jmpbuf(png)))
	{
		if (my_png.Data)
			Mem_Free(my_png.Data);
		my_png.Data = NULL;
		if (my_png.FRowPtrs)
			Mem_Free(my_png.FRowPtrs);
		my_png.FRowPtrs = NULL;
		png_destroy_read_struct(&png, &pnginfo, 0);
		return NULL;
	}
	//

	pnginfo = png_create_info_struct(png);
	if(!pnginfo)
	{
		png_destroy_read_struct(&png, &pnginfo, 0);
		return NULL;
	}
	png_set_sig_bytes(png, 0);

	my_png.tmpBuf = raw;
	my_png.tmpBuflength = filesize;
	my_png.tmpi = 0;
	//my_png.Data		= NULL;
	//my_png.FRowPtrs	= NULL;
	//my_png.Height		= 0;
	//my_png.Width		= 0;
	my_png.ColorType	= PNG_COLOR_TYPE_RGB;
	//my_png.Interlace	= 0;
	//my_png.Compression	= 0;
	//my_png.Filter		= 0;
	png_set_read_fn(png, ioBuffer, PNG_fReadData);
	png_read_info(png, pnginfo);
	png_get_IHDR(png, pnginfo, &my_png.Width, &my_png.Height,&my_png.BitDepth, &my_png.ColorType, &my_png.Interlace, &my_png.Compression, &my_png.Filter);

	if (my_png.ColorType == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (my_png.ColorType == PNG_COLOR_TYPE_GRAY || my_png.ColorType == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if (png_get_valid(png, pnginfo, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (my_png.BitDepth == 8 && !(my_png.ColorType  & PNG_COLOR_MASK_ALPHA))
		png_set_filler(png, 255, 1);
	if (( my_png.ColorType == PNG_COLOR_TYPE_GRAY) || (my_png.ColorType == PNG_COLOR_TYPE_GRAY_ALPHA ))
		png_set_gray_to_rgb(png);
	if (my_png.BitDepth < 8)
		png_set_expand(png);

	png_read_update_info(png, pnginfo);

	my_png.FRowBytes = png_get_rowbytes(png, pnginfo);
	my_png.BytesPerPixel = png_get_channels(png, pnginfo);

	my_png.FRowPtrs = (unsigned char **)Mem_Alloc(tempmempool, my_png.Height * sizeof(*my_png.FRowPtrs));
	if (my_png.FRowPtrs)
	{
		imagedata = (unsigned char *)Mem_Alloc(tempmempool, my_png.Height * my_png.FRowBytes);
		if(imagedata)
		{
			my_png.Data = imagedata;
			for(y = 0;y < my_png.Height;y++)
				my_png.FRowPtrs[y] = my_png.Data + y * my_png.FRowBytes;
			png_read_image(png, my_png.FRowPtrs);
		}
		else
		{
			Con_Printf("PNG_LoadImage : not enough memory\n");
			png_destroy_read_struct(&png, &pnginfo, 0);
			Mem_Free(my_png.FRowPtrs);
			return NULL;
		}
		Mem_Free(my_png.FRowPtrs);
		my_png.FRowPtrs = NULL;
	}
	else
	{
		Con_Printf("PNG_LoadImage : not enough memory\n");
		png_destroy_read_struct(&png, &pnginfo, 0);
		return NULL;
	}

	png_read_end(png, pnginfo);
	png_destroy_read_struct(&png, &pnginfo, 0);

	image_width = (int)my_png.Width;
	image_height = (int)my_png.Height;

	if (my_png.BitDepth != 8)
	{
		Con_Printf ("PNG_LoadImage : bad color depth\n");
		Mem_Free(imagedata);
		return NULL;
	}

	// swizzle RGBA to BGRA
	for (y = 0;y < (unsigned int)(image_width*image_height*4);y += 4)
	{
		c = imagedata[y+0];
		imagedata[y+0] = imagedata[y+2];
		imagedata[y+2] = c;
	}

	return imagedata;
}

/*
=================================================================

	PNG compression

=================================================================
*/

#define Z_BEST_SPEED 1
//#define Z_BEST_COMPRESSION 9

/*
====================
PNG_SaveImage_preflipped

Save a preflipped PNG image to a file
====================
*/
qboolean PNG_SaveImage_preflipped (const char *filename, int width, int height, qboolean has_alpha, unsigned char *data)
{
	unsigned int offset, linesize;
	qfile_t* file = NULL;
	png_structp png;
	png_infop pnginfo;
	unsigned char ioBuffer[8192];
	int passes, i, j;

	png = (void *)png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, PNG_error_fn, PNG_warning_fn);
	if(!png)
		return false;
	pnginfo = (void *)png_create_info_struct(png);
	if(!pnginfo)
	{
		 png_destroy_write_struct(&png, NULL);
		 return false;
	}

	// this must be memset before the setjmp error handler, because it relies
	// on the fields in this struct for cleanup
	memset(&my_png, 0, sizeof(my_png));

	// NOTE: this relies on jmp_buf being the first thing in the png structure
	// created by libpng! (this is correct for libpng 1.2.x)
	if (setjmp(png_jmpbuf(png)))
	{
		png_destroy_write_struct(&png, &pnginfo);
		return false;
	}

	// Open the file
	file = FS_OpenRealFile(filename, "wb", true);
	if (!file)
		return false;
	my_png.outfile = file;
	png_set_write_fn(png, ioBuffer, PNG_fWriteData, PNG_fFlushData);

	//png_set_compression_level(png, Z_BEST_COMPRESSION);
	png_set_compression_level(png, Z_BEST_SPEED);
	png_set_IHDR(png, pnginfo, width, height, 8, has_alpha ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB, PNG_INTERLACE_ADAM7, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_filter(png, 0, PNG_NO_FILTERS);
	png_write_info(png, pnginfo);
	png_set_packing(png);
	png_set_bgr(png);

	passes = png_set_interlace_handling(png);

	linesize = width * (has_alpha ? 4 : 3);
	offset = linesize * (height - 1);
	for(i = 0; i < passes; ++i)
		for(j = 0; j < height; ++j)
			png_write_row(png, &data[offset - j * linesize]);

	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &pnginfo);

	FS_Close (file);

	return true;
}
