#include <oggz/oggz.h>
#include <theora/theora.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>

typedef struct ogvdecodestream_s
{
	th_info theora_info;
	th_comment theora_comment;
	th_setup_info *theora_setup_info;
	th_dec_ctx *theora_decoder;
	th_ycbcr_buffer theora_frame_buffer;
	qfile_t *file;
	OGGZ *oggz;
	long video_serialno;
	long audio_serialno;
	qboolean video_stream_available, audio_stream_available;
	qboolean video_header_readed;
	qboolean theora_frame_ready;
	int sndchan;
}
ogvdecodestream_t;

static unsigned int ogv_getwidth(void *stream)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	return s->theora_info.pic_width;
}

static unsigned int ogv_getheight(void *stream)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	return s->theora_info.pic_height;
}

static double ogv_getaspectratio(void *stream)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	return s->theora_info.aspect_numerator * s->theora_info.pic_width / (s->theora_info.aspect_denominator * s->theora_info.pic_height);
}

static double ogv_getframerate(void *stream)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	double framerate = s->theora_info.fps_numerator / s->theora_info.fps_denominator;
	return framerate;
}

static size_t ogv_read(void *stream, void *buf, size_t n)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	return FS_Read(s->file, buf, n);
}

static int ogv_seek(void *stream, long offset, int whence)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	return FS_Seek(s->file, offset, whence);
}

static long ogv_tell (void *stream)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	return FS_Tell(s->file);
}

static void ogv_close(void *stream);
static int ogv_packet(OGGZ * oggz, oggz_packet *zp, long serialno, void *stream);

static int ogv_video(void *stream, void *imagedata, unsigned int Rmask, unsigned int Gmask, unsigned int Bmask, unsigned int bytesperpixel, int imagebytesperrow)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	unsigned i, j, ihalf, jhalf, ystart, cbcrstart;
	unsigned char *bgra = imagedata;
	int p4 = 0;
	short y, cb, cr, b, g, r;
	while (!s->theora_frame_ready && oggz_read(s->oggz, 1024) > 0)
		;
	//oggz_set_read_callback(s->oggz, -1, ogv_packet, s);
	if (!s->theora_frame_ready)
	{
		//Con_Printf("No more frames, finish\n");
		return 1;
	}
	s->theora_frame_ready = false;
	for (i = 0; i < s->theora_info.pic_height; i++) {
		ihalf = i / 2;
		ystart = i * s->theora_frame_buffer[0].stride;
		cbcrstart = ihalf * s->theora_frame_buffer[1].stride;
		for (j = 0; j < s->theora_info.pic_width; j++) {
			jhalf = j / 2;
			y = s->theora_frame_buffer[0].data[ystart + j];
			cb = s->theora_frame_buffer[1].data[cbcrstart + jhalf];
			cr = s->theora_frame_buffer[2].data[cbcrstart + jhalf];
			r = y + 1.402 * (cr - 128);
			g = y - 0.344136 * (cb - 128) - 0.714136 * (cr - 128);
			b = y + 1.772 * (cb - 128);
			bgra[p4 + 2] = bound(0, r, 255);
			bgra[p4 + 1] = bound(0, g, 255);
			bgra[p4 + 0] = bound(0, b, 255);
			bgra[p4 + 3] = 255;
			p4 += 4;
		}
	}
	oggz_read(s->oggz, 1); //reset state of oggz after callback returning 1
	while (!s->theora_frame_ready && oggz_read(s->oggz, 1024) > 0); //prepare next frame
	return 0;
}

static int ogv_packet(OGGZ * oggz, oggz_packet *zp, long serialno, void *stream)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	ogg_packet *op = &zp->op;
	int n;
	if (s->audio_stream_available && serialno == s->audio_serialno) {
	} else if (s->video_stream_available && serialno == s->video_serialno) {
		if (s->video_header_readed) {
			if (th_decode_packetin(s->theora_decoder, op, NULL) == 0)
			{
				th_decode_ycbcr_out(s->theora_decoder, s->theora_frame_buffer);
				s->theora_frame_ready = true;
				return 1;
			}
		} else {
			n = th_decode_headerin(&s->theora_info, &s->theora_comment, &s->theora_setup_info, op);
			if (n == 0) {
				s->video_header_readed = true;
				s->theora_decoder = th_decode_alloc(&s->theora_info, s->theora_setup_info);
				if (th_decode_packetin(s->theora_decoder, op, NULL) == 0)
				{
					th_decode_ycbcr_out(s->theora_decoder, s->theora_frame_buffer);
					s->theora_frame_ready = true;
					return 1;
				}
			} else if (n < 0)
			{
				Con_Printf("Theora header error\n");
			}
		}
	} else {
		if (s->audio_stream_available && s->video_stream_available)
			return 0; //ignore

		if (!s->video_stream_available)
		{
			if (
					op->packet[1] == 't' &&
					op->packet[2] == 'h' &&
					op->packet[3] == 'e' &&
					op->packet[4] == 'o' &&
					op->packet[5] == 'r' &&
					op->packet[6] == 'a'
			)
			{
				th_info_init(&s->theora_info);
				th_decode_headerin(&s->theora_info, &s->theora_comment, &s->theora_setup_info, op);
				s->video_stream_available = true;
				s->video_serialno = serialno;
			}
		}
	}
	return 0;
}

static void* ogv_open(clvideo_t *video, const char *path, const char **errorstring) {
	ogvdecodestream_t *s;
	int i, n = 0;
	sfx_t *sfx;
	// allocate stream structure
	s = (ogvdecodestream_t *)Z_Malloc(sizeof(ogvdecodestream_t));
	s->video_stream_available = false;
	s->video_header_readed = false;
	s->audio_stream_available = false;
	s->theora_setup_info = NULL;
	s->theora_decoder = NULL;
	s->theora_frame_ready = false;
	s->file = FS_OpenVirtualFile(path, true);
	s->sndchan = -1;
	if (!s->file)
	{
		*errorstring = "Cannot open file";
		goto fail;
	}
	s->oggz = oggz_new(OGGZ_READ | OGGZ_AUTO);
	if (!s->oggz)
	{
		*errorstring = "Cannot create oggz instance";
		goto fail;
	}
	oggz_io_set_read(s->oggz, ogv_read, s);
	oggz_io_set_seek(s->oggz, ogv_seek, s);
	oggz_io_set_tell(s->oggz, ogv_tell, s);
	oggz_set_read_callback(s->oggz, -1, ogv_packet, s);
	video->close = ogv_close;
	video->getwidth = ogv_getwidth;
	video->getheight = ogv_getheight;
	video->getframerate = ogv_getframerate;
	video->decodeframe = ogv_video;
	video->getaspectratio = ogv_getaspectratio;
	for (i = 0; i < 100000 && !s->video_header_readed; i++)
	{
		if ((n = oggz_read(s->oggz, 1024)) <= 0)
			break;
	}
	//oggz_set_read_callback(s->oggz, -1, ogv_packet, s);
	if (!s->video_header_readed)
	{
		*errorstring = "Video stream not found";
		goto fail;
	}
	while (!s->theora_frame_ready && oggz_read(s->oggz, 1024) > 0);
	if (s->theora_frame_ready) oggz_read(s->oggz, 1); //reset state of oggz after callback returning 1
	sfx = S_PrecacheSound(path, false, false);
	if (sfx != NULL)
	{
		s->sndchan = S_StartSound (-1, 0, sfx, vec3_origin, 1.0f, 0);
	}
	else
		s->sndchan = -1;
	return s;
fail:
	ogv_close(s);
	return NULL;
}

static void ogv_close(void *stream)
{
	ogvdecodestream_t *s = (ogvdecodestream_t *)stream;
	if (s == NULL)
		return;

	if (s->file)
		FS_Close(s->file);

	if (s->oggz)
		oggz_close(s->oggz);

	if (s->theora_decoder)
		th_decode_free(s->theora_decoder);

	if (s->theora_setup_info)
		th_setup_free(s->theora_setup_info);

	if (s->sndchan != -1)
		S_StopChannel(s->sndchan, true, true);

	s->sndchan = -1;
	Z_Free(s);
}
