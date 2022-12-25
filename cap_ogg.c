#ifndef _MSC_VER
#include <stdint.h>
#endif
#include <sys/types.h>

#include "quakedef.h"
#include "client.h"
#include "cap_ogg.h"
#include "random.h"
#ifdef LINK_TO_LIBVORBIS
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <ogg/ogg.h>
#include <vorbis/vorbisfile.h>
#include <vorbis/vorbisenc.h>
#include <theora/theora.h>
#include <theora/theoraenc.h>
#endif
#ifdef LINK_TO_VPX
#include <vpx/vpx_encoder.h>
#include <vpx/vp8.h>
#include <vpx/vp8cx.h>
#endif
#ifdef CONFIG_VOIP
#include <opus.h>
#endif

// video capture cvars
static cvar_t cl_capturevideo_ogg_theora_vp3compat = {CVAR_SAVE, "cl_capturevideo_ogg_theora_vp3compat", "1", "make VP3 compatible theora streams"};
static cvar_t cl_capturevideo_ogg_theora_quality = {CVAR_SAVE, "cl_capturevideo_ogg_theora_quality", "48", "video quality factor (0 to 63), or -1 to use bitrate only; higher is better; setting both to -1 achieves unlimited quality"};
static cvar_t cl_capturevideo_ogg_theora_bitrate = {CVAR_SAVE, "cl_capturevideo_ogg_theora_bitrate", "-1", "video bitrate (45 to 2000 kbps), or -1 to use quality only; higher is better; setting both to -1 achieves unlimited quality"};
static cvar_t cl_capturevideo_ogg_theora_keyframe_bitrate_multiplier = {CVAR_SAVE, "cl_capturevideo_ogg_theora_keyframe_bitrate_multiplier", "1.5", "how much more bit rate to use for keyframes, specified as a factor of at least 1"};
static cvar_t cl_capturevideo_ogg_theora_keyframe_maxinterval = {CVAR_SAVE, "cl_capturevideo_ogg_theora_keyframe_maxinterval", "64", "maximum keyframe interval (1 to 1000)"};
static cvar_t cl_capturevideo_ogg_theora_keyframe_mininterval = {CVAR_SAVE, "cl_capturevideo_ogg_theora_keyframe_mininterval", "8", "minimum keyframe interval (1 to 1000)"};
static cvar_t cl_capturevideo_ogg_theora_keyframe_auto_threshold = {CVAR_SAVE, "cl_capturevideo_ogg_theora_keyframe_auto_threshold", "80", "threshold for key frame decision (0 to 100)"};
static cvar_t cl_capturevideo_ogg_theora_noise_sensitivity = {CVAR_SAVE, "cl_capturevideo_ogg_theora_noise_sensitivity", "1", "video noise sensitivity (0 to 6); lower is better"};
static cvar_t cl_capturevideo_ogg_theora_sharpness = {CVAR_SAVE, "cl_capturevideo_ogg_theora_sharpness", "0", "sharpness (0 to 2); lower is sharper"};
static cvar_t cl_capturevideo_ogg_vorbis_quality = {CVAR_SAVE, "cl_capturevideo_ogg_vorbis_quality", "3", "audio quality (-1 to 10); higher is better"};
#ifdef LINK_TO_VPX
static cvar_t cl_capturevideo_ogg_vp8 = {CVAR_SAVE, "cl_capturevideo_ogg_vp8", "1", "use vp8 codec to encode video"};
static cvar_t cl_capturevideo_ogg_vp8_bitrate = {CVAR_SAVE, "cl_capturevideo_ogg_vp8_bitrate", "10000", "video bitrate for vp8 encoding"};
static cvar_t cl_capturevideo_ogg_vp8_threads = {CVAR_SAVE, "cl_capturevideo_ogg_vp8_threads", "4", "amount of threads for vp8 encoding"};
static cvar_t cl_capturevideo_ogg_vp8_min_quantizer = {CVAR_SAVE, "cl_capturevideo_ogg_vp8_min_quantizer", "2", "vp8 min quantizer"};
static cvar_t cl_capturevideo_ogg_vp8_max_quantizer = {CVAR_SAVE, "cl_capturevideo_ogg_vp8_max_quantizer", "56", "vp8 max quantizer"};
static cvar_t cl_capturevideo_ogg_vp8_end_usage = {CVAR_SAVE, "cl_capturevideo_ogg_vp8_end_usage", "1", "vp8 end usage (0 - VBR, 1 - CBR, 2 - CQ, 3 - Q)"};
static cvar_t cl_capturevideo_ogg_vp8_deadline = {CVAR_SAVE, "cl_capturevideo_ogg_vp8_deadline", "1", "vp8 deadline (0 - best quality, 1 - realtime quality , 2 - good quality)"};
#endif
#ifdef CONFIG_VOIP
static cvar_t cl_capturevideo_ogg_opus = {CVAR_SAVE, "cl_capturevideo_ogg_opus", "1", "use opus codec to encode audio"};
#endif

#ifndef LINK_TO_LIBVORBIS
// ogg.h stuff
#ifdef _MSC_VER
typedef __int16 ogg_int16_t;
typedef unsigned __int16 ogg_uint16_t;
typedef __int32 ogg_int32_t;
typedef unsigned __int32 ogg_uint32_t;
typedef __int64 ogg_int64_t;
#else
typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
#endif

typedef struct {
  long endbyte;
  int  endbit;

  unsigned char *buffer;
  unsigned char *ptr;
  long storage;
} oggpack_buffer;

/* ogg_page is used to encapsulate the data in one Ogg bitstream page *****/

typedef struct {
  unsigned char *header;
  long header_len;
  unsigned char *body;
  long body_len;
} ogg_page;

/* ogg_stream_state contains the current encode/decode state of a logical
   Ogg bitstream **********************************************************/

typedef struct {
  unsigned char   *body_data;    /* bytes from packet bodies */
  long    body_storage;          /* storage elements allocated */
  long    body_fill;             /* elements stored; fill mark */
  long    body_returned;         /* elements of fill returned */


  int     *lacing_vals;      /* The values that will go to the segment table */
  ogg_int64_t *granule_vals; /* granulepos values for headers. Not compact
				this way, but it is simple coupled to the
				lacing fifo */
  long    lacing_storage;
  long    lacing_fill;
  long    lacing_packet;
  long    lacing_returned;

  unsigned char    header[282];      /* working space for header encode */
  int              header_fill;

  int     e_o_s;          /* set when we have buffered the last packet in the
                             logical bitstream */
  int     b_o_s;          /* set after we've written the initial page
                             of a logical bitstream */
  long    serialno;
  long    pageno;
  ogg_int64_t  packetno;      /* sequence number for decode; the framing
                             knows where there's a hole in the data,
                             but we need coupling so that the codec
                             (which is in a seperate abstraction
                             layer) also knows about the gap */
  ogg_int64_t   granulepos;

} ogg_stream_state;

/* ogg_packet is used to encapsulate the data and metadata belonging
   to a single raw Ogg/Vorbis packet *************************************/

typedef struct {
  unsigned char *packet;
  long  bytes;
  long  b_o_s;
  long  e_o_s;

  ogg_int64_t  granulepos;

  ogg_int64_t  packetno;     /* sequence number for decode; the framing
				knows where there's a hole in the data,
				but we need coupling so that the codec
				(which is in a seperate abstraction
				layer) also knows about the gap */
} ogg_packet;

typedef struct {
  unsigned char *data;
  int storage;
  int fill;
  int returned;

  int unsynced;
  int headerbytes;
  int bodybytes;
} ogg_sync_state;

/* Ogg BITSTREAM PRIMITIVES: encoding **************************/

static int      (*qogg_stream_packetin) (ogg_stream_state *os, ogg_packet *op);
static int      (*qogg_stream_pageout) (ogg_stream_state *os, ogg_page *og);
static int      (*qogg_stream_flush) (ogg_stream_state *os, ogg_page *og);

/* Ogg BITSTREAM PRIMITIVES: general ***************************/

static int      (*qogg_stream_init) (ogg_stream_state *os,int serialno);
static int      (*qogg_stream_clear) (ogg_stream_state *os);
static ogg_int64_t  (*qogg_page_granulepos) (ogg_page *og);

// end of ogg.h stuff

// vorbis/codec.h stuff
typedef struct vorbis_info{
  int version;
  int channels;
  long rate;

  /* The below bitrate declarations are *hints*.
     Combinations of the three values carry the following implications:

     all three set to the same value:
       implies a fixed rate bitstream
     only nominal set:
       implies a VBR stream that averages the nominal bitrate.  No hard
       upper/lower limit
     upper and or lower set:
       implies a VBR bitstream that obeys the bitrate limits. nominal
       may also be set to give a nominal rate.
     none set:
       the coder does not care to speculate.
  */

  long bitrate_upper;
  long bitrate_nominal;
  long bitrate_lower;
  long bitrate_window;

  void *codec_setup;
} vorbis_info;

/* vorbis_dsp_state buffers the current vorbis audio
   analysis/synthesis state.  The DSP state belongs to a specific
   logical bitstream ****************************************************/
typedef struct vorbis_dsp_state{
  int analysisp;
  vorbis_info *vi;

  float **pcm;
  float **pcmret;
  int      pcm_storage;
  int      pcm_current;
  int      pcm_returned;

  int  preextrapolate;
  int  eofflag;

  long lW;
  long W;
  long nW;
  long centerW;

  ogg_int64_t granulepos;
  ogg_int64_t sequence;

  ogg_int64_t glue_bits;
  ogg_int64_t time_bits;
  ogg_int64_t floor_bits;
  ogg_int64_t res_bits;

  void       *backend_state;
} vorbis_dsp_state;

typedef struct vorbis_block{
  /* necessary stream state for linking to the framing abstraction */
  float  **pcm;       /* this is a pointer into local storage */
  oggpack_buffer opb;

  long  lW;
  long  W;
  long  nW;
  int   pcmend;
  int   mode;

  int         eofflag;
  ogg_int64_t granulepos;
  ogg_int64_t sequence;
  vorbis_dsp_state *vd; /* For read-only access of configuration */

  /* local storage to avoid remallocing; it's up to the mapping to
     structure it */
  void               *localstore;
  long                localtop;
  long                localalloc;
  long                totaluse;
  struct alloc_chain *reap;

  /* bitmetrics for the frame */
  long glue_bits;
  long time_bits;
  long floor_bits;
  long res_bits;

  void *internal;

} vorbis_block;

/* vorbis_block is a single block of data to be processed as part of
the analysis/synthesis stream; it belongs to a specific logical
bitstream, but is independant from other vorbis_blocks belonging to
that logical bitstream. *************************************************/

struct alloc_chain{
  void *ptr;
  struct alloc_chain *next;
};

/* vorbis_info contains all the setup information specific to the
   specific compression/decompression mode in progress (eg,
   psychoacoustic settings, channel setup, options, codebook
   etc). vorbis_info and substructures are in backends.h.
*********************************************************************/

/* the comments are not part of vorbis_info so that vorbis_info can be
   static storage */
typedef struct vorbis_comment{
  /* unlimited user comment fields.  libvorbis writes 'libvorbis'
     whatever vendor is set to in encode */
  char **user_comments;
  int   *comment_lengths;
  int    comments;
  char  *vendor;

} vorbis_comment;


/* libvorbis encodes in two abstraction layers; first we perform DSP
   and produce a packet (see docs/analysis.txt).  The packet is then
   coded into a framed OggSquish bitstream by the second layer (see
   docs/framing.txt).  Decode is the reverse process; we sync/frame
   the bitstream and extract individual packets, then decode the
   packet back into PCM audio.

   The extra framing/packetizing is used in streaming formats, such as
   files.  Over the net (such as with UDP), the framing and
   packetization aren't necessary as they're provided by the transport
   and the streaming layer is not used */

/* Vorbis PRIMITIVES: general ***************************************/

static void     (*qvorbis_info_init) (vorbis_info *vi);
static void     (*qvorbis_info_clear) (vorbis_info *vi);
static void     (*qvorbis_comment_init) (vorbis_comment *vc);
static void     (*qvorbis_comment_clear) (vorbis_comment *vc);

static int      (*qvorbis_block_init) (vorbis_dsp_state *v, vorbis_block *vb);
static int      (*qvorbis_block_clear) (vorbis_block *vb);
static void     (*qvorbis_dsp_clear) (vorbis_dsp_state *v);
static double   (*qvorbis_granule_time) (vorbis_dsp_state *v,
				    ogg_int64_t granulepos);

/* Vorbis PRIMITIVES: analysis/DSP layer ****************************/

static int      (*qvorbis_analysis_init) (vorbis_dsp_state *v,vorbis_info *vi);
static int      (*qvorbis_commentheader_out) (vorbis_comment *vc, ogg_packet *op);
static int      (*qvorbis_analysis_headerout) (vorbis_dsp_state *v,
					  vorbis_comment *vc,
					  ogg_packet *op,
					  ogg_packet *op_comm,
					  ogg_packet *op_code);
static float ** (*qvorbis_analysis_buffer) (vorbis_dsp_state *v,int vals);
static int      (*qvorbis_analysis_wrote) (vorbis_dsp_state *v,int vals);
static int      (*qvorbis_analysis_blockout) (vorbis_dsp_state *v,vorbis_block *vb);
static int      (*qvorbis_analysis) (vorbis_block *vb,ogg_packet *op);

static int      (*qvorbis_bitrate_addblock) (vorbis_block *vb);
static int      (*qvorbis_bitrate_flushpacket) (vorbis_dsp_state *vd,
					   ogg_packet *op);

// end of vorbis/codec.h stuff

// vorbisenc.h stuff
static int (*qvorbis_encode_init_vbr) (vorbis_info *vi,
				  long channels,
				  long rate,

				  float base_quality /* quality level from 0. (lo) to 1. (hi) */
				  );
// end of vorbisenc.h stuff

// theora.h stuff

#define TH_ENCCTL_SET_VP3_COMPATIBLE (10)

typedef struct {
    int   y_width;      /**< Width of the Y' luminance plane */
    int   y_height;     /**< Height of the luminance plane */
    int   y_stride;     /**< Offset in bytes between successive rows */

    int   uv_width;     /**< Width of the Cb and Cr chroma planes */
    int   uv_height;    /**< Height of the chroma planes */
    int   uv_stride;    /**< Offset between successive chroma rows */
    unsigned char *y;   /**< Pointer to start of luminance data */
    unsigned char *u;   /**< Pointer to start of Cb data */
    unsigned char *v;   /**< Pointer to start of Cr data */

} yuv_buffer;

/**
 * A Colorspace.
 */
typedef enum {
  OC_CS_UNSPECIFIED,    /**< The colorspace is unknown or unspecified */
  OC_CS_ITU_REC_470M,   /**< This is the best option for 'NTSC' content */
  OC_CS_ITU_REC_470BG,  /**< This is the best option for 'PAL' content */
  OC_CS_NSPACES         /**< This marks the end of the defined colorspaces */
} theora_colorspace;

/**
 * A Chroma subsampling
 *
 * These enumerate the available chroma subsampling options supported
 * by the theora format. See Section 4.4 of the specification for
 * exact definitions.
 */
typedef enum {
  OC_PF_420,    /**< Chroma subsampling by 2 in each direction (4:2:0) */
  OC_PF_RSVD,   /**< Reserved value */
  OC_PF_422,    /**< Horizonatal chroma subsampling by 2 (4:2:2) */
  OC_PF_444     /**< No chroma subsampling at all (4:4:4) */
} theora_pixelformat;
/**
 * Theora bitstream info.
 * Contains the basic playback parameters for a stream,
 * corresponding to the initial 'info' header packet.
 *
 * Encoded theora frames must be a multiple of 16 in width and height.
 * To handle other frame sizes, a crop rectangle is specified in
 * frame_height and frame_width, offset_x and * offset_y. The offset
 * and size should still be a multiple of 2 to avoid chroma sampling
 * shifts. Offset values in this structure are measured from the
 * upper left of the image.
 *
 * Frame rate, in frames per second, is stored as a rational
 * fraction. Aspect ratio is also stored as a rational fraction, and
 * refers to the aspect ratio of the frame pixels, not of the
 * overall frame itself.
 *
 * See <a href="http://svn.xiph.org/trunk/theora/examples/encoder_example.c">
 * examples/encoder_example.c</a> for usage examples of the
 * other paramters and good default settings for the encoder parameters.
 */
typedef struct {
  ogg_uint32_t  width;      /**< encoded frame width  */
  ogg_uint32_t  height;     /**< encoded frame height */
  ogg_uint32_t  frame_width;    /**< display frame width  */
  ogg_uint32_t  frame_height;   /**< display frame height */
  ogg_uint32_t  offset_x;   /**< horizontal offset of the displayed frame */
  ogg_uint32_t  offset_y;   /**< vertical offset of the displayed frame */
  ogg_uint32_t  fps_numerator;      /**< frame rate numerator **/
  ogg_uint32_t  fps_denominator;    /**< frame rate denominator **/
  ogg_uint32_t  aspect_numerator;   /**< pixel aspect ratio numerator */
  ogg_uint32_t  aspect_denominator; /**< pixel aspect ratio denominator */
  theora_colorspace colorspace;     /**< colorspace */
  int           target_bitrate;     /**< nominal bitrate in bits per second */
  int           quality;  /**< Nominal quality setting, 0-63 */
  int           quick_p;  /**< Quick encode/decode */

  /* decode only */
  unsigned char version_major;
  unsigned char version_minor;
  unsigned char version_subminor;

  void *codec_setup;

  /* encode only */
  int           dropframes_p;
  int           keyframe_auto_p;
  ogg_uint32_t  keyframe_frequency;
  ogg_uint32_t  keyframe_frequency_force;  /* also used for decode init to
                                              get granpos shift correct */
  ogg_uint32_t  keyframe_data_target_bitrate;
  ogg_int32_t   keyframe_auto_threshold;
  ogg_uint32_t  keyframe_mindistance;
  ogg_int32_t   noise_sensitivity;
  ogg_int32_t   sharpness;

  theora_pixelformat pixelformat;   /**< chroma subsampling mode to expect */

} theora_info;

/** Codec internal state and context.
 */
typedef struct{
  theora_info *i;
  ogg_int64_t granulepos;

  void *internal_encode;
  void *internal_decode;

} theora_state;

/**
 * Comment header metadata.
 *
 * This structure holds the in-stream metadata corresponding to
 * the 'comment' header packet.
 *
 * Meta data is stored as a series of (tag, value) pairs, in
 * length-encoded string vectors. The first occurence of the
 * '=' character delimits the tag and value. A particular tag
 * may occur more than once. The character set encoding for
 * the strings is always UTF-8, but the tag names are limited
 * to case-insensitive ASCII. See the spec for details.
 *
 * In filling in this structure, qtheora_decode_header() will
 * null-terminate the user_comment strings for safety. However,
 * the bitstream format itself treats them as 8-bit clean,
 * and so the length array should be treated as authoritative
 * for their length.
 */
typedef struct theora_comment{
  char **user_comments;         /**< An array of comment string vectors */
  int   *comment_lengths;       /**< An array of corresponding string vector lengths in bytes */
  int    comments;              /**< The total number of comment string vectors */
  char  *vendor;                /**< The vendor string identifying the encoder, null terminated */

} theora_comment;
static int (*qtheora_encode_init) (theora_state *th, theora_info *ti);
static int (*qtheora_encode_YUVin) (theora_state *t, yuv_buffer *yuv);
static int (*qtheora_encode_packetout) ( theora_state *t, int last_p,
                                    ogg_packet *op);
static int (*qtheora_encode_header) (theora_state *t, ogg_packet *op);
static int (*qtheora_encode_comment) (theora_comment *tc, ogg_packet *op);
static int (*qtheora_encode_tables) (theora_state *t, ogg_packet *op);
static void (*qtheora_info_init) (theora_info *c);
static void (*qtheora_info_clear) (theora_info *c);
static void (*qtheora_clear) (theora_state *t);
static void (*qtheora_comment_init) (theora_comment *tc);
static void  (*qtheora_comment_clear) (theora_comment *tc);
static double (*qtheora_granule_time) (theora_state *th,ogg_int64_t granulepos);
static int (*qtheora_control) (theora_state *th,int req,void *buf,size_t buf_sz);
// end of theora.h stuff

static dllfunction_t oggfuncs[] =
{
	{"ogg_stream_packetin", (void **) &qogg_stream_packetin},
	{"ogg_stream_pageout", (void **) &qogg_stream_pageout},
	{"ogg_stream_flush", (void **) &qogg_stream_flush},
	{"ogg_stream_init", (void **) &qogg_stream_init},
	{"ogg_stream_clear", (void **) &qogg_stream_clear},
	{"ogg_page_granulepos", (void **) &qogg_page_granulepos},
	{NULL, NULL}
};

static dllfunction_t vorbisencfuncs[] =
{
	{"vorbis_encode_init_vbr", (void **) &qvorbis_encode_init_vbr},
	{NULL, NULL}
};

static dllfunction_t vorbisfuncs[] =
{
	{"vorbis_info_init", (void **) &qvorbis_info_init},
	{"vorbis_info_clear", (void **) &qvorbis_info_clear},
	{"vorbis_comment_init", (void **) &qvorbis_comment_init},
	{"vorbis_comment_clear", (void **) &qvorbis_comment_clear},
	{"vorbis_block_init", (void **) &qvorbis_block_init},
	{"vorbis_block_clear", (void **) &qvorbis_block_clear},
	{"vorbis_dsp_clear", (void **) &qvorbis_dsp_clear},
	{"vorbis_analysis_init", (void **) &qvorbis_analysis_init},
	{"vorbis_commentheader_out", (void **) &qvorbis_commentheader_out},
	{"vorbis_analysis_headerout", (void **) &qvorbis_analysis_headerout},
	{"vorbis_analysis_buffer", (void **) &qvorbis_analysis_buffer},
	{"vorbis_analysis_wrote", (void **) &qvorbis_analysis_wrote},
	{"vorbis_analysis_blockout", (void **) &qvorbis_analysis_blockout},
	{"vorbis_analysis", (void **) &qvorbis_analysis},
	{"vorbis_bitrate_addblock", (void **) &qvorbis_bitrate_addblock},
	{"vorbis_bitrate_flushpacket", (void **) &qvorbis_bitrate_flushpacket},
	{"vorbis_granule_time", (void **) &qvorbis_granule_time},
	{NULL, NULL}
};

static dllfunction_t theorafuncs[] =
{
	{"theora_info_init", (void **) &qtheora_info_init},
	{"theora_info_clear", (void **) &qtheora_info_clear},
	{"theora_comment_init", (void **) &qtheora_comment_init},
	{"theora_comment_clear", (void **) &qtheora_comment_clear},
	{"theora_encode_init", (void **) &qtheora_encode_init},
	{"theora_encode_YUVin", (void **) &qtheora_encode_YUVin},
	{"theora_encode_packetout", (void **) &qtheora_encode_packetout},
	{"theora_encode_header", (void **) &qtheora_encode_header},
	{"theora_encode_comment", (void **) &qtheora_encode_comment},
	{"theora_encode_tables", (void **) &qtheora_encode_tables},
	{"theora_clear", (void **) &qtheora_clear},
	{"theora_granule_time", (void **) &qtheora_granule_time},
	{"theora_control", (void **) &qtheora_control},
	{NULL, NULL}
};

static dllhandle_t og_dll = NULL, vo_dll = NULL, ve_dll = NULL, th_dll = NULL;

static qboolean SCR_CaptureVideo_Ogg_OpenLibrary(void)
{
	const char* dllnames_og [] =
	{
#if defined(WIN32)
		"libogg-0.dll",
		"libogg.dll",
		"ogg.dll",
#elif defined(MACOSX)
		"libogg.dylib",
#else
		"libogg.so.0",
		"libogg.so",
#endif
		NULL
	};
	const char* dllnames_vo [] =
	{
#if defined(WIN32)
		"libvorbis-0.dll",
		"libvorbis.dll",
		"vorbis.dll",
#elif defined(MACOSX)
		"libvorbis.dylib",
#else
		"libvorbis.so.0",
		"libvorbis.so",
#endif
		NULL
	};
	const char* dllnames_ve [] =
	{
#if defined(WIN32)
		"libvorbisenc-2.dll",
		"libvorbisenc.dll",
		"vorbisenc.dll",
#elif defined(MACOSX)
		"libvorbisenc.dylib",
#else
		"libvorbisenc.so.2",
		"libvorbisenc.so",
#endif
		NULL
	};
	const char* dllnames_th [] =
	{
#if defined(WIN32)
		"libtheora-0.dll",
		"libtheora.dll",
		"theora.dll",
#elif defined(MACOSX)
		"libtheora.dylib",
#else
		"libtheora.so.0",
		"libtheora.so",
#endif
		NULL
	};

	return
		Sys_LoadLibrary (dllnames_og, &og_dll, oggfuncs)
		&&
		Sys_LoadLibrary (dllnames_th, &th_dll, theorafuncs)
		&&
		Sys_LoadLibrary (dllnames_vo, &vo_dll, vorbisfuncs)
		&&
		Sys_LoadLibrary (dllnames_ve, &ve_dll, vorbisencfuncs);
}

#else
#define qtheora_encode_init theora_encode_init
#define qtheora_encode_YUVin theora_encode_YUVin
#define qtheora_encode_packetout theora_encode_packetout
#define qtheora_encode_header theora_encode_header
#define qtheora_encode_comment theora_encode_comment
#define qtheora_encode_tables theora_encode_tables
#define qtheora_info_init theora_info_init
#define qtheora_info_clear theora_info_clear
#define qtheora_clear theora_clear
#define qtheora_comment_init theora_comment_init
#define qtheora_comment_clear theora_comment_clear
#define qtheora_granule_time theora_granule_time
#define qtheora_control theora_control
#define qvorbis_info_init vorbis_info_init
#define qvorbis_info_clear vorbis_info_clear
#define qvorbis_comment_init vorbis_comment_init
#define qvorbis_comment_clear vorbis_comment_clear
#define qvorbis_block_init vorbis_block_init
#define qvorbis_block_clear vorbis_block_clear
#define qvorbis_dsp_clear vorbis_dsp_clear
#define qvorbis_granule_time vorbis_granule_time
#define qvorbis_analysis_init vorbis_analysis_init
#define qvorbis_commentheader_out vorbis_commentheader_out
#define qvorbis_analysis_headerout vorbis_analysis_headerout
#define qvorbis_analysis_buffer vorbis_analysis_buffer
#define qvorbis_analysis_wrote vorbis_analysis_wrote
#define qvorbis_analysis_blockout vorbis_analysis_blockout
#define qvorbis_analysis vorbis_analysis
#define qvorbis_bitrate_addblock vorbis_bitrate_addblock
#define qvorbis_bitrate_flushpacket vorbis_bitrate_flushpacket
#define qvorbis_encode_init_vbr vorbis_encode_init_vbr
#define qogg_stream_packetin ogg_stream_packetin
#define qogg_stream_pageout ogg_stream_pageout
#define qogg_stream_flush ogg_stream_flush
#define qogg_stream_init ogg_stream_init
#define qogg_stream_clear ogg_stream_clear
#define qogg_page_granulepos ogg_page_granulepos
#endif
void SCR_CaptureVideo_Ogg_Init(void)
{
#ifndef LINK_TO_LIBVORBIS
	SCR_CaptureVideo_Ogg_OpenLibrary();
#endif
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_vp3compat);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_quality);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_bitrate);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_keyframe_bitrate_multiplier);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_keyframe_maxinterval);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_keyframe_mininterval);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_keyframe_auto_threshold);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_theora_noise_sensitivity);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vorbis_quality);
	#ifdef LINK_TO_VPX
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vp8);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vp8_bitrate);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vp8_threads);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vp8_min_quantizer);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vp8_max_quantizer);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vp8_end_usage);
	Cvar_RegisterVariable(&cl_capturevideo_ogg_vp8_deadline);
	#endif
	#ifdef CONFIG_VOIP
	Cvar_RegisterVariable(&cl_capturevideo_ogg_opus);
	#endif
}

qboolean SCR_CaptureVideo_Ogg_Available(void)
{
#ifndef LINK_TO_LIBVORBIS
	return og_dll && th_dll && vo_dll && ve_dll;
#else
	return true;
#endif
}

void SCR_CaptureVideo_Ogg_CloseDLL(void)
{
#ifndef LINK_TO_LIBVORBIS
	Sys_UnloadLibrary (&ve_dll);
	Sys_UnloadLibrary (&vo_dll);
	Sys_UnloadLibrary (&th_dll);
	Sys_UnloadLibrary (&og_dll);
#endif
}

// this struct should not be needed
// however, libogg appears to pull the ogg_page's data element away from our
// feet before we get to write the data due to interleaving
// so this struct is used to keep the page data around until it actually gets
// written
typedef struct allocatedoggpage_s
{
	size_t len;
	double time;
	unsigned char data[65307];
	// this number is from RFC 3533. In case libogg writes more, we'll have to increase this
	// but we'll get a Host_Error in this case so we can track it down
}
allocatedoggpage_t;

typedef struct capturevideostate_ogg_formatspecific_s
{
	ogg_stream_state videostream, audiostream;
	int serial1, serial2;
	theora_state ts;
	vorbis_dsp_state vd;
	vorbis_block vb;
	vorbis_info vi;
	yuv_buffer yuv[2];
	int yuvi;
	int lastnum;
	int channels;

	allocatedoggpage_t videopage, audiopage;
	#ifdef LINK_TO_VPX
	int vp8;
	vpx_codec_ctx_t vpx_codec;
	vpx_image_t vpx_image;
	ogg_int64_t vpx_packetno;
	vpx_codec_pts_t vpx_frame;
	int64_t vpx_last_granule;
	#endif
	#ifdef CONFIG_VOIP
	int opus;
	int opus_sample_size;
	#define OPUS_BUFFER_SIZE 4096
	opus_int16 opus_buffer[OPUS_BUFFER_SIZE];
	int opus_buffer_len;
	OpusEncoder *opus_encoder;
	ogg_int64_t opus_packetno;
	ogg_int64_t opus_last_granule;
	#endif
}
capturevideostate_ogg_formatspecific_t;
#define LOAD_FORMATSPECIFIC_OGG() capturevideostate_ogg_formatspecific_t *format = (capturevideostate_ogg_formatspecific_t *) cls.capturevideo.formatspecific

static void SCR_CaptureVideo_Ogg_Interleave(void)
{
	LOAD_FORMATSPECIFIC_OGG();
	ogg_page pg;

	if(!cls.capturevideo.soundrate)
	{
		while(qogg_stream_pageout(&format->videostream, &pg) > 0)
		{
			FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
			FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
		}
		return;
	}

	for(;;)
	{
		// first: make sure we have a page of both types
		if(!format->videopage.len)
			if(qogg_stream_pageout(&format->videostream, &pg) > 0)
			{
				format->videopage.len = pg.header_len + pg.body_len;
				#ifdef LINK_TO_VPX
				if (format->vp8)
				{
					format->videopage.time = (qogg_page_granulepos(&pg) >> 32) * ((double)cls.capturevideo.framestep / (double)cls.capturevideo.framerate);
				}
				else
				#endif
				{
					format->videopage.time = qtheora_granule_time(&format->ts, qogg_page_granulepos(&pg));
				}
				if(format->videopage.len > sizeof(format->videopage.data))
					Sys_Error("video page too long");
				memcpy(format->videopage.data, pg.header, pg.header_len);
				memcpy(format->videopage.data + pg.header_len, pg.body, pg.body_len);
			}
		if(!format->audiopage.len)
			if(qogg_stream_pageout(&format->audiostream, &pg) > 0)
			{
				format->audiopage.len = pg.header_len + pg.body_len;
				#ifdef CONFIG_VOIP
				if (format->opus)
					format->audiopage.time = (double)qogg_page_granulepos(&pg) / (double)cls.capturevideo.soundrate;
				else
				#endif
					format->audiopage.time = qvorbis_granule_time(&format->vd, qogg_page_granulepos(&pg));
				if(format->audiopage.len > sizeof(format->audiopage.data))
					Sys_Error("audio page too long");
				memcpy(format->audiopage.data, pg.header, pg.header_len);
				memcpy(format->audiopage.data + pg.header_len, pg.body, pg.body_len);
			}

		if(format->videopage.len && format->audiopage.len)
		{
			// output the page that ends first
			if(format->videopage.time < format->audiopage.time)
			{
				FS_Write(cls.capturevideo.videofile, format->videopage.data, format->videopage.len);
				format->videopage.len = 0;
			}
			else
			{
				FS_Write(cls.capturevideo.videofile, format->audiopage.data, format->audiopage.len);
				format->audiopage.len = 0;
			}
		}
		else
			break;
	}
}

static void SCR_CaptureVideo_Ogg_FlushInterleaving(void)
{
	LOAD_FORMATSPECIFIC_OGG();

	if(cls.capturevideo.soundrate)
	if(format->audiopage.len)
	{
		FS_Write(cls.capturevideo.videofile, format->audiopage.data, format->audiopage.len);
		format->audiopage.len = 0;
	}

	if(format->videopage.len)
	{
		FS_Write(cls.capturevideo.videofile, format->videopage.data, format->videopage.len);
		format->videopage.len = 0;
	}
}

static void SCR_CaptureVideo_Ogg_EndVideo(void)
{
	LOAD_FORMATSPECIFIC_OGG();
	ogg_page pg;
	ogg_packet pt;

	if(format->yuvi >= 0)
	{
		// send the previous (and last) frame
		#ifdef LINK_TO_VPX
		if (format->vp8)
		{
			int64_t pts, invcnt, dist;
			int visible;
			const vpx_codec_cx_pkt_t *pkt;
			vpx_codec_iter_t vpx_iter = NULL;
			if (format->lastnum)
			{
				unsigned long deadline = (cl_capturevideo_ogg_vp8_deadline.integer == 1 ? VPX_DL_REALTIME : (cl_capturevideo_ogg_vp8_deadline.integer == 2 ? VPX_DL_GOOD_QUALITY : VPX_DL_BEST_QUALITY));
				memcpy(format->vpx_image.planes[0], format->yuv[format->yuvi].y, format->yuv[format->yuvi].y_width *  format->yuv[format->yuvi].y_height);
				memcpy(format->vpx_image.planes[1], format->yuv[format->yuvi].u, format->yuv[format->yuvi].uv_width * format->yuv[format->yuvi].uv_height);
				memcpy(format->vpx_image.planes[2], format->yuv[format->yuvi].v, format->yuv[format->yuvi].uv_width * format->yuv[format->yuvi].uv_height);
				vpx_codec_control(&format->vpx_codec, VP8E_SET_FRAME_FLAGS, 0);
				vpx_codec_encode(&format->vpx_codec, &format->vpx_image, format->vpx_frame, format->lastnum, 0, deadline);
				vpx_codec_encode(&format->vpx_codec, NULL, format->vpx_frame, format->lastnum, 0, deadline);
				format->vpx_frame += format->lastnum;
				format->lastnum = 0;
			}
			pkt = vpx_codec_get_cx_data(&format->vpx_codec, &vpx_iter);
			while (pkt)
			{
				//pack to ogg
				pt.packet = pkt->data.frame.buf;
				pt.bytes = pkt->data.frame.sz;
				pt.b_o_s = 0;
				pt.packetno = format->vpx_packetno;
				format->vpx_packetno++;

				//visible = !(pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE);
				visible = ((((unsigned char *)pkt->data.frame.buf)[0] >> 4) & 1);
				pts     = pkt->data.frame.pts + pkt->data.frame.duration;
				invcnt  = (format->vpx_last_granule >> 30) & 3;
				invcnt  = visible ? 3 : (invcnt == 3 ? 0 : invcnt + 1);
				dist    = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) ? 0 : ((format->vpx_last_granule >> 3) & 0x07ffffff) + 1;
				pt.granulepos = (pts << 32) | (invcnt << 30) | (dist << 3) | (dist ? 0 : 1);
				format->vpx_last_granule = pt.granulepos;

				pkt = vpx_codec_get_cx_data(&format->vpx_codec, &vpx_iter);
				pt.e_o_s = (pkt ? 1 : 0);
				qogg_stream_packetin(&format->videostream, &pt);
			}
		}
		else
		#endif
		{
			while(format->lastnum-- > 0)
			{
				qtheora_encode_YUVin(&format->ts, &format->yuv[format->yuvi]);

				while(qtheora_encode_packetout(&format->ts, !format->lastnum, &pt))
					qogg_stream_packetin(&format->videostream, &pt);
			}
		}
		SCR_CaptureVideo_Ogg_Interleave();
	}

	if(cls.capturevideo.soundrate)
	{
		#ifdef CONFIG_VOIP
		if (format->opus)
		{
			int encsize;
			unsigned char packet[2048];
			memset(&format->opus_buffer[format->opus_buffer_len], 0, sizeof(format->opus_buffer) - sizeof(opus_int16) * format->opus_buffer_len);
			encsize = opus_encode(format->opus_encoder, format->opus_buffer, format->opus_sample_size, packet, sizeof(packet));
			if (encsize < 0) Sys_Error("Opus encode failed at end frame");
			pt.bytes = encsize;
			pt.packet = packet;
			pt.e_o_s = 1;
			pt.b_o_s = 0;
			pt.granulepos = format->opus_last_granule + format->opus_sample_size; //FIXME
			pt.packetno = format->opus_packetno;
			qogg_stream_packetin(&format->audiostream, &pt);
		}
		else
		#endif
		{
			qvorbis_analysis_wrote(&format->vd, 0);
			while(qvorbis_analysis_blockout(&format->vd, &format->vb) == 1)
			{
				qvorbis_analysis(&format->vb, NULL);
				qvorbis_bitrate_addblock(&format->vb);
				while(qvorbis_bitrate_flushpacket(&format->vd, &pt))
					qogg_stream_packetin(&format->audiostream, &pt);
			}
		}
		SCR_CaptureVideo_Ogg_Interleave();
	}

	SCR_CaptureVideo_Ogg_FlushInterleaving();

	while(qogg_stream_pageout(&format->videostream, &pg) > 0)
	{
		FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
		FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
	}

	if(cls.capturevideo.soundrate)
	{
		while(qogg_stream_pageout(&format->audiostream, &pg) > 0)
		{
			FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
			FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
		}
	}

	while (1) {
		int result = qogg_stream_flush (&format->videostream, &pg);
		if (result < 0)
			Sys_Error("Internal Ogg library error.\n"); // TODO Sys_Error
		if (result <= 0)
			break;
		FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
		FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
	}

	if(cls.capturevideo.soundrate)
	{
		while (1) {
			int result = qogg_stream_flush (&format->audiostream, &pg);
			if (result < 0)
				Sys_Error("Internal Ogg library error.\n"); // TODO Sys_Error
			if (result <= 0)
				break;
			FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
			FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
		}
		qogg_stream_clear(&format->audiostream);
		#ifdef CONFIG_VOIP
		if (format->opus)
		{
			opus_encoder_destroy(format->opus_encoder);
		}
		else
		#endif
		{
			qvorbis_block_clear(&format->vb);
			qvorbis_dsp_clear(&format->vd);
		}
	}

	qogg_stream_clear(&format->videostream);
	#ifdef LINK_TO_VPX
	if (format->vp8)
	{
		vpx_codec_destroy(&format->vpx_codec);
		vpx_img_free(&format->vpx_image);
	}
	else
	#endif
	{
		qtheora_clear(&format->ts);
	}
	if(cls.capturevideo.soundrate)
		#ifdef CONFIG_VOIP
		if (!format->opus)
		#endif
			qvorbis_info_clear(&format->vi);

	Mem_Free(format->yuv[0].y);
	Mem_Free(format->yuv[0].u);
	Mem_Free(format->yuv[0].v);
	Mem_Free(format->yuv[1].y);
	Mem_Free(format->yuv[1].u);
	Mem_Free(format->yuv[1].v);
	Mem_Free(format);

	FS_Close(cls.capturevideo.videofile);
	cls.capturevideo.videofile = NULL;
}

static void SCR_CaptureVideo_Ogg_ConvertFrame_BGRA_to_YUV(void)
{
	LOAD_FORMATSPECIFIC_OGG();
	yuv_buffer *yuv;
	int x, y;
	int blockr, blockg, blockb;
	unsigned char *b;
	int w = cls.capturevideo.width;
	int h = cls.capturevideo.height;
	int inpitch = w*4;

	yuv = &format->yuv[format->yuvi];

	for(y = 0; y < h; ++y)
	{
		for(b = cls.capturevideo.outbuffer + (h-1-y)*w*4, x = 0; x < w; ++x)
		{
			blockr = b[2];
			blockg = b[1];
			blockb = b[0];
			yuv->y[x + yuv->y_stride * y] =
				cls.capturevideo.yuvnormalizetable[0][cls.capturevideo.rgbtoyuvscaletable[0][0][blockr] + cls.capturevideo.rgbtoyuvscaletable[0][1][blockg] + cls.capturevideo.rgbtoyuvscaletable[0][2][blockb]];
			b += 4;
		}

		if ((y & 1) == 0 && y/2 < h/2) // if h is odd, this skips the last row
		{
			for(b = cls.capturevideo.outbuffer + (h-2-y)*w*4, x = 0; x < w/2; ++x)
			{
				blockr = (b[2] + b[6] + b[inpitch+2] + b[inpitch+6]) >> 2;
				blockg = (b[1] + b[5] + b[inpitch+1] + b[inpitch+5]) >> 2;
				blockb = (b[0] + b[4] + b[inpitch+0] + b[inpitch+4]) >> 2;
				yuv->u[x + yuv->uv_stride * (y/2)] =
					cls.capturevideo.yuvnormalizetable[1][cls.capturevideo.rgbtoyuvscaletable[1][0][blockr] + cls.capturevideo.rgbtoyuvscaletable[1][1][blockg] + cls.capturevideo.rgbtoyuvscaletable[1][2][blockb] + 128];
				yuv->v[x + yuv->uv_stride * (y/2)] =
					cls.capturevideo.yuvnormalizetable[2][cls.capturevideo.rgbtoyuvscaletable[2][0][blockr] + cls.capturevideo.rgbtoyuvscaletable[2][1][blockg] + cls.capturevideo.rgbtoyuvscaletable[2][2][blockb] + 128];
				b += 8;
			}
		}
	}
}

static void SCR_CaptureVideo_Ogg_VideoFrames(int num)
{
	LOAD_FORMATSPECIFIC_OGG();
	ogg_packet pt;

	// data is in cls.capturevideo.outbuffer as BGRA and has size width*height

	if(format->yuvi >= 0)
	{
		// send the previous frame
		#ifdef LINK_TO_VPX
		if (format->vp8)
		{
			int64_t pts, invcnt, dist;
			int visible;
			const vpx_codec_cx_pkt_t *pkt;
			vpx_codec_err_t vpx_err;
			vpx_codec_iter_t vpx_iter = NULL;
			if (format->lastnum > 0)
			{
				unsigned long deadline = (cl_capturevideo_ogg_vp8_deadline.integer == 1 ? VPX_DL_REALTIME : (cl_capturevideo_ogg_vp8_deadline.integer == 2 ? VPX_DL_GOOD_QUALITY : VPX_DL_BEST_QUALITY));
				memcpy(format->vpx_image.planes[0], format->yuv[format->yuvi].y, format->yuv[format->yuvi].y_width *  format->yuv[format->yuvi].y_height);
				memcpy(format->vpx_image.planes[1], format->yuv[format->yuvi].u, format->yuv[format->yuvi].uv_width * format->yuv[format->yuvi].uv_height);
				memcpy(format->vpx_image.planes[2], format->yuv[format->yuvi].v, format->yuv[format->yuvi].uv_width * format->yuv[format->yuvi].uv_height);
				if (!format->vpx_last_granule)
					vpx_codec_control(&format->vpx_codec, VP8E_SET_FRAME_FLAGS, VPX_EFLAG_FORCE_KF);
				else
					vpx_codec_control(&format->vpx_codec, VP8E_SET_FRAME_FLAGS, 0);

				if ((vpx_err = vpx_codec_encode(&format->vpx_codec, &format->vpx_image, format->vpx_frame, format->lastnum, 0, deadline)))
				{
					Con_Printf("VP8 compression failed: %s\n", vpx_codec_err_to_string(vpx_err));
				}
				format->vpx_frame += format->lastnum;
				format->lastnum = 0;
			}
			while ((pkt = vpx_codec_get_cx_data(&format->vpx_codec, &vpx_iter)))
			{
				//pack to ogg
				pt.packet = pkt->data.frame.buf;
				pt.bytes = pkt->data.frame.sz;
				pt.e_o_s = 0;
				pt.b_o_s = 0;
				pt.packetno = format->vpx_packetno;
				format->vpx_packetno++;

				//visible = !(pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE);
				visible = ((((unsigned char *)pkt->data.frame.buf)[0] >> 4) & 1);
				pts     = pkt->data.frame.pts + pkt->data.frame.duration;
				invcnt  = (format->vpx_last_granule >> 30) & 3;
				invcnt  = visible ? 3 : (invcnt == 3 ? 0 : invcnt + 1);
				dist    = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) ? 0 : ((format->vpx_last_granule >> 3) & 0x07ffffff) + 1;
				if (invcnt >> 2) Sys_Error("SCR_CaptureVideo_Ogg_VideoFrames: Wrong invcnt value\n");
				pt.granulepos = (pts << 32) | (invcnt << 30) | (dist << 3) | (dist ? 0 : 1);
				format->vpx_last_granule = pt.granulepos;
				qogg_stream_packetin(&format->videostream, &pt);
			}
		}
		else
		#endif
		{
			while(format->lastnum-- > 0)
			{
				qtheora_encode_YUVin(&format->ts, &format->yuv[format->yuvi]);

				while(qtheora_encode_packetout(&format->ts, false, &pt))
				{
					qogg_stream_packetin(&format->videostream, &pt);
				}
			}
		}
		SCR_CaptureVideo_Ogg_Interleave();
	}

	format->yuvi = (format->yuvi + 1) % 2;
	SCR_CaptureVideo_Ogg_ConvertFrame_BGRA_to_YUV();
	format->lastnum = num;

	// TODO maybe send num-1 frames from here already
}

typedef int channelmapping_t[8];
channelmapping_t mapping[8] =
{
	{ 0, -1, -1, -1, -1, -1, -1, -1 }, // mono
	{ 0, 1, -1, -1, -1, -1, -1, -1 }, // stereo
	{ 0, 1, 2, -1, -1, -1, -1, -1 }, // L C R
	{ 0, 1, 2, 3, -1, -1, -1, -1 }, // surround40
	{ 0, 2, 3, 4, 1, -1, -1, -1 }, // FL FC FR RL RR
	{ 0, 2, 3, 4, 1, 5, -1, -1 }, // surround51
	{ 0, 2, 3, 4, 1, 5, 6, -1 }, // (not defined by vorbis spec)
	{ 0, 2, 3, 4, 1, 5, 6, 7 } // surround71 (not defined by vorbis spec)
};

static void SCR_CaptureVideo_Ogg_SoundFrame(const portable_sampleframe_t *paintbuffer, size_t length)
{
	LOAD_FORMATSPECIFIC_OGG();
	ogg_packet pt;
	size_t i;
	int j;
	#ifdef CONFIG_VOIP
	if (format->opus)
	{
		size_t copied = 0;
		size_t copy_chunk;
		int encsize;
		unsigned char packet[4096];
		while (copied < length) {
			copy_chunk = min(length, (size_t)(OPUS_BUFFER_SIZE - format->opus_buffer_len) / cls.capturevideo.soundchannels);
			for(j = 0; j < cls.capturevideo.soundchannels; ++j)
			{
				for(i = 0; i < copy_chunk; ++i)
					format->opus_buffer[format->opus_buffer_len + j + i * cls.capturevideo.soundchannels] = paintbuffer[i + copied].sample[j] * 32768.0f;
			}
			format->opus_buffer_len += copy_chunk * cls.capturevideo.soundchannels;
			length -= copy_chunk;
			while (format->opus_buffer_len > format->opus_sample_size * cls.capturevideo.soundchannels)
			{
				encsize = opus_encode(format->opus_encoder, format->opus_buffer, format->opus_sample_size, packet, sizeof(packet));
				if (encsize < 0) Sys_Error("SCR_CaptureVideo_Ogg_SoundFrame: opus_encode failed");
				pt.bytes = encsize;
				pt.packet = packet;
				pt.e_o_s = 0;
				pt.b_o_s = 0;
				pt.granulepos = format->opus_last_granule + format->opus_sample_size;
				format->opus_last_granule = pt.granulepos;
				pt.packetno = format->opus_packetno;
				format->opus_packetno++;
				qogg_stream_packetin(&format->audiostream, &pt);
				memmove(format->opus_buffer, &format->opus_buffer[format->opus_sample_size * cls.capturevideo.soundchannels], (format->opus_buffer_len - format->opus_sample_size * cls.capturevideo.soundchannels) * sizeof(opus_int16)); //shifting data in buffer
				format->opus_buffer_len -= format->opus_sample_size * cls.capturevideo.soundchannels;
				copied += format->opus_sample_size;
			}
		}
	}
	else
	#endif
	{
		float **vorbis_buffer;
		int *map = mapping[bound(1, cls.capturevideo.soundchannels, 8) - 1];

		vorbis_buffer = qvorbis_analysis_buffer(&format->vd, (int)length);
		for(j = 0; j < cls.capturevideo.soundchannels; ++j)
		{
			float *b = vorbis_buffer[map[j]];
			for(i = 0; i < length; ++i)
				b[i] = paintbuffer[i].sample[j];
		}
		qvorbis_analysis_wrote(&format->vd, (int)length);

		while(qvorbis_analysis_blockout(&format->vd, &format->vb) == 1)
		{
			qvorbis_analysis(&format->vb, NULL);
			qvorbis_bitrate_addblock(&format->vb);

			while(qvorbis_bitrate_flushpacket(&format->vd, &pt))
				qogg_stream_packetin(&format->audiostream, &pt);
		}
	}
	SCR_CaptureVideo_Ogg_Interleave();
}

void SCR_CaptureVideo_Ogg_BeginVideo(void)
{
	char vabuf[1024];
	cls.capturevideo.format = CAPTUREVIDEOFORMAT_OGG;
	cls.capturevideo.formatextension = "ogv";
	cls.capturevideo.videofile = FS_OpenRealFile(va(vabuf, sizeof(vabuf), "%s.%s", cls.capturevideo.basename, cls.capturevideo.formatextension), "wb", false);
	cls.capturevideo.endvideo = SCR_CaptureVideo_Ogg_EndVideo;
	cls.capturevideo.videoframes = SCR_CaptureVideo_Ogg_VideoFrames;
	cls.capturevideo.soundframe = SCR_CaptureVideo_Ogg_SoundFrame;
	cls.capturevideo.formatspecific = Mem_Alloc(tempmempool, sizeof(capturevideostate_ogg_formatspecific_t));
	{
		LOAD_FORMATSPECIFIC_OGG();
		int num, denom, i;
		ogg_page pg;
		ogg_packet pt, pt2, pt3;
		vorbis_comment vc;
		theora_comment tc;
		theora_info ti;
		int vp3compat;

		format->serial1 = xrand();
		qogg_stream_init(&format->videostream, format->serial1);

		#ifdef LINK_TO_VPX
		format->vp8 = cl_capturevideo_ogg_vp8.integer;
		#endif
		#ifdef CONFIG_VOIP
		format->opus = cl_capturevideo_ogg_opus.integer && (cls.capturevideo.soundchannels <= 2);
		#endif
		if(cls.capturevideo.soundrate)
		{
			do
			{
				format->serial2 = xrand();
			}
			while(format->serial1 == format->serial2);
			qogg_stream_init(&format->audiostream, format->serial2);
		}

		format->videopage.len = format->audiopage.len = 0;
		format->yuvi = -1; // -1: no frame valid yet, write into 0
		#ifdef LINK_TO_VPX
		if (format->vp8)
		{
			vpx_codec_err_t vpx_err;
			vpx_codec_enc_cfg_t cfg;
			for(i = 0; i < 2; ++i)
			{
				format->yuv[i].y_width = cls.capturevideo.width;
				format->yuv[i].y_height = cls.capturevideo.height;
				format->yuv[i].y_stride = cls.capturevideo.width;
				format->yuv[i].uv_width = cls.capturevideo.width / 2;
				format->yuv[i].uv_height = cls.capturevideo.height / 2;
				format->yuv[i].uv_stride = cls.capturevideo.width / 2;
				format->yuv[i].y = (unsigned char *) Mem_Alloc(tempmempool, format->yuv[i].y_stride * format->yuv[i].y_height);
				format->yuv[i].u = (unsigned char *) Mem_Alloc(tempmempool, format->yuv[i].uv_stride * format->yuv[i].uv_height);
				format->yuv[i].v = (unsigned char *) Mem_Alloc(tempmempool, format->yuv[i].uv_stride * format->yuv[i].uv_height);
			}
			format->vpx_frame = 0;
			format->vpx_packetno = 1;
			format->vpx_last_granule = 0;
			vpx_err = vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &cfg, 0);
			if (vpx_err)
			{
				Sys_Error("Failed to get VPX config: %s\n", vpx_codec_err_to_string(vpx_err));
				//FIXME: error handling
			}
			cfg.g_w = cls.capturevideo.width;
			cfg.g_h = cls.capturevideo.height;
			cfg.g_threads = max(1, cl_capturevideo_ogg_vp8_threads.integer);
			//cfg.rc_dropframe_thresh = 0;
			switch (cl_capturevideo_ogg_vp8_end_usage.integer)
			{
			case 0:
				cfg.rc_end_usage = VPX_VBR;
				break;
			case 1:
				cfg.rc_end_usage = VPX_CBR;
				break;
			case 2:
				cfg.rc_end_usage = VPX_CQ;
				break;
			default:
				cfg.rc_end_usage = VPX_Q;
				break;
			}
			//cfg.rc_resize_allowed = 0;
			cfg.rc_min_quantizer = cl_capturevideo_ogg_vp8_min_quantizer.integer;
			cfg.rc_max_quantizer = cl_capturevideo_ogg_vp8_max_quantizer.integer;
			//cfg.rc_undershoot_pct = 100;
			//cfg.rc_overshoot_pct = 15;
			//cfg.rc_buf_initial_sz = 500;
			//cfg.rc_buf_optimal_sz = 600;
			//cfg.rc_buf_sz = 1000;
			//cfg.g_error_resilient = 1; /* Enable error resilient mode */
			//cfg.g_lag_in_frames = 0;
			cfg.kf_mode = VPX_KF_AUTO;
			//cfg.kf_min_dist = 3000;
			//cfg.kf_max_dist = 3000;
			cfg.rc_target_bitrate = cl_capturevideo_ogg_vp8_bitrate.integer; /* Set target bitrate */
			FindFraction(cls.capturevideo.framerate / cls.capturevideo.framestep, &num, &denom, 1000);
			cfg.g_timebase.num = num;
			cfg.g_timebase.den = denom;
			if (!(vpx_img_alloc(&format->vpx_image, VPX_IMG_FMT_YV12, cfg.g_w, cfg.g_h, 32)))
			{
				Sys_Error("Failed to allocate VPX image\n");
			}
			if ((vpx_err = vpx_codec_enc_init(&format->vpx_codec, vpx_codec_vp8_cx(), &cfg, VPX_ENCODER_ABI_VERSION)))
			{
				Sys_Error("Failed to init VPX codec: %s\n", vpx_codec_err_to_string(vpx_err));
			}
		}
		else
		#endif
		{
			qtheora_info_init(&ti);
			ti.frame_width = cls.capturevideo.width;
			ti.frame_height = cls.capturevideo.height;
			ti.width = (ti.frame_width + 15) & ~15;
			ti.height = (ti.frame_height + 15) & ~15;
			//ti.offset_x = ((ti.width - ti.frame_width) / 2) & ~1;
			//ti.offset_y = ((ti.height - ti.frame_height) / 2) & ~1;
			for(i = 0; i < 2; ++i)
			{
				format->yuv[i].y_width = ti.width;
				format->yuv[i].y_height = ti.height;
				format->yuv[i].y_stride = ti.width;
				format->yuv[i].uv_width = ti.width / 2;
				format->yuv[i].uv_height = ti.height / 2;
				format->yuv[i].uv_stride = ti.width / 2;
				format->yuv[i].y = (unsigned char *) Mem_Alloc(tempmempool, format->yuv[i].y_stride * format->yuv[i].y_height);
				format->yuv[i].u = (unsigned char *) Mem_Alloc(tempmempool, format->yuv[i].uv_stride * format->yuv[i].uv_height);
				format->yuv[i].v = (unsigned char *) Mem_Alloc(tempmempool, format->yuv[i].uv_stride * format->yuv[i].uv_height);
			}

			FindFraction(cls.capturevideo.framerate / cls.capturevideo.framestep, &num, &denom, 1001);
			ti.fps_numerator = num;
			ti.fps_denominator = denom;

			FindFraction(1 / vid_pixelheight.value, &num, &denom, 1000);
			ti.aspect_numerator = num;
			ti.aspect_denominator = denom;

			ti.colorspace = OC_CS_UNSPECIFIED;
			ti.pixelformat = OC_PF_420;

			ti.quick_p = true; // http://mlblog.osdir.com/multimedia.ogg.theora.general/2004-07/index.shtml
			ti.dropframes_p = false;

			ti.target_bitrate = cl_capturevideo_ogg_theora_bitrate.integer * 1000;
			ti.quality = cl_capturevideo_ogg_theora_quality.integer;

			if(ti.target_bitrate <= 0)
			{
				ti.target_bitrate = -1;
				ti.keyframe_data_target_bitrate = (unsigned int)-1;
			}
			else
			{
				ti.keyframe_data_target_bitrate = (int) (ti.target_bitrate * max(1, cl_capturevideo_ogg_theora_keyframe_bitrate_multiplier.value));

				if(ti.target_bitrate < 45000 || ti.target_bitrate > 2000000)
					Con_DPrintf("WARNING: requesting an odd bitrate for theora (sensible values range from 45 to 2000 kbps)\n");
			}

			if(ti.quality < 0 || ti.quality > 63)
			{
				ti.quality = 63;
				if(ti.target_bitrate <= 0)
				{
					ti.target_bitrate = 0x7FFFFFFF;
					ti.keyframe_data_target_bitrate = 0x7FFFFFFF;
				}
			}

			// this -1 magic is because ti.keyframe_frequency and ti.keyframe_mindistance use different metrics
			ti.keyframe_frequency = bound(1, cl_capturevideo_ogg_theora_keyframe_maxinterval.integer, 1000);
			ti.keyframe_mindistance = bound(1, cl_capturevideo_ogg_theora_keyframe_mininterval.integer, (int) ti.keyframe_frequency) - 1;
			ti.noise_sensitivity = bound(0, cl_capturevideo_ogg_theora_noise_sensitivity.integer, 6);
			ti.sharpness = bound(0, cl_capturevideo_ogg_theora_sharpness.integer, 2);
			ti.keyframe_auto_threshold = bound(0, cl_capturevideo_ogg_theora_keyframe_auto_threshold.integer, 100);

			ti.keyframe_frequency_force = ti.keyframe_frequency;
			ti.keyframe_auto_p = (ti.keyframe_frequency != ti.keyframe_mindistance + 1);

			qtheora_encode_init(&format->ts, &ti);
			qtheora_info_clear(&ti);

			if(cl_capturevideo_ogg_theora_vp3compat.integer)
			{
				vp3compat = 1;
				qtheora_control(&format->ts, TH_ENCCTL_SET_VP3_COMPATIBLE, &vp3compat, sizeof(vp3compat));
				if(!vp3compat)
					Con_DPrintf("Warning: theora stream is not fully VP3 compatible\n");
			}
		}

		// vorbis?
		if(cls.capturevideo.soundrate)
		{
			#ifdef CONFIG_VOIP
			if (format->opus)
			{
				int err;
				format->opus_encoder = opus_encoder_create(cls.capturevideo.soundrate, cls.capturevideo.soundchannels, OPUS_APPLICATION_AUDIO, &err);
				format->opus_sample_size = cls.capturevideo.soundrate / 100;
				format->opus_packetno = 2;
				format->opus_last_granule = 0;
				if (format->opus_sample_size * cls.capturevideo.soundchannels > OPUS_BUFFER_SIZE)
					Sys_Error("SCR_CaptureVideo_Ogg_BeginVideo: Opus buffer size is not enough\n");
				format->opus_buffer_len = 0;
				if (!format->opus_encoder)
					Sys_Error("SCR_CaptureVideo_Ogg_BeginVideo: Opus encoder creation error\n");
				err = opus_encoder_ctl(format->opus_encoder, OPUS_SET_BITRATE(256000));
				if (err)
					Sys_Error("SCR_CaptureVideo_Ogg_BeginVideo: Opus bitrate setup failed\n");
			}
			else
			#endif
			{
				qvorbis_info_init(&format->vi);
				qvorbis_encode_init_vbr(&format->vi, cls.capturevideo.soundchannels, cls.capturevideo.soundrate, bound(-1, cl_capturevideo_ogg_vorbis_quality.value, 10) * 0.099);
				qvorbis_comment_init(&vc);
				qvorbis_analysis_init(&format->vd, &format->vi);
				qvorbis_block_init(&format->vd, &format->vb);
			}
		}
		#ifdef LINK_TO_VPX
		if (format->vp8)
		{
			unsigned char vp8_header[26];
			pt.bytes = 26;
			pt.packet = vp8_header;
			pt.b_o_s = 1;
			pt.e_o_s = 0;
			pt.granulepos = 0;
			pt.packetno = 0;
			vp8_header[0] = 0x4f;
			vp8_header[1] = 'V';
			vp8_header[2] = 'P';
			vp8_header[3] = '8';
			vp8_header[4] = '0';
			vp8_header[5] = 1;
			vp8_header[6] = 1;
			vp8_header[7] = 0;
			//16 bit big endian width
			vp8_header[8] = cls.capturevideo.width / 256;
			vp8_header[9] = cls.capturevideo.width % 256;
			//16 bit big endian heigth
			vp8_header[10] = cls.capturevideo.height / 256;
			vp8_header[11] = cls.capturevideo.height % 256;
			//aspect ratio
			FindFraction(1 / vid_pixelheight.value, &num, &denom, 1000);
			vp8_header[12] = (num >> 16) % 256;
			vp8_header[13] = (num >> 8) % 256;
			vp8_header[14] = num % 256;
			vp8_header[15] = (denom >> 16) % 256;
			vp8_header[16] = (denom >> 8) % 256;
			vp8_header[17] = denom;
			//frame rate
			FindFraction(cls.capturevideo.framerate / cls.capturevideo.framestep, &num, &denom, 1001);
			vp8_header[18] = (num >> 24) % 256;
			vp8_header[19] = (num >> 16) % 256;
			vp8_header[20] = (num >> 8) % 256;
			vp8_header[21] = num % 256;
			vp8_header[22] = (denom >> 24) % 256;
			vp8_header[23] = (denom >> 16) % 256;
			vp8_header[24] = (denom >> 8) % 256;
			vp8_header[25] = denom % 256;
			qogg_stream_packetin (&format->videostream, &pt);
			if (qogg_stream_pageout (&format->videostream, &pg) != 1)
				Sys_Error("Internal Ogg library error.\n");
			FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
			FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
		}
		else
		#endif
		{
			qtheora_comment_init(&tc);

			/* create the remaining theora headers */
			qtheora_encode_header(&format->ts, &pt);
			qogg_stream_packetin(&format->videostream, &pt);
			if (qogg_stream_pageout (&format->videostream, &pg) != 1)
				Sys_Error("Internal Ogg library error.\n");
			FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
			FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);

			qtheora_encode_comment(&tc, &pt);
			qogg_stream_packetin(&format->videostream, &pt);
			qtheora_encode_tables(&format->ts, &pt);
			qogg_stream_packetin (&format->videostream, &pt);

			qtheora_comment_clear(&tc);
		}
		if(cls.capturevideo.soundrate)
		{
			#ifdef CONFIG_VOIP
			if (format->opus)
			{
				unsigned char header_data[19];
				header_data[0] = 'O';
				header_data[1] = 'p';
				header_data[2] = 'u';
				header_data[3] = 's';
				header_data[4] = 'H';
				header_data[5] = 'e';
				header_data[6] = 'a';
				header_data[7] = 'd';
				header_data[8] = 1; //Version
				header_data[9] = cls.capturevideo.soundchannels; //Channels
				header_data[10] = 0; //Preskip
				header_data[11] = 0;
				header_data[12] = (cls.capturevideo.soundrate >> 24) % 256; //Sample rate
				header_data[13] = (cls.capturevideo.soundrate >> 16) % 256;
				header_data[14] = (cls.capturevideo.soundrate >> 8) % 256;
				header_data[15] = cls.capturevideo.soundrate % 256;
				header_data[16] = 0; //Gain
				header_data[17] = 0;
				//header_data[18] = (cls.capturevideo.soundchannels > 8 ? 255 : cls.capturevideo.soundchannels > 2);
				header_data[18] = 0; // stereo only yet
				pt.packet = header_data;
				pt.bytes = sizeof(header_data);
				pt.b_o_s = 1;
				pt.e_o_s = 0;
				pt.granulepos = 0;
				pt.packetno = 0;
				qogg_stream_packetin(&format->audiostream, &pt);
				if (qogg_stream_pageout (&format->audiostream, &pg) != 1)
					Sys_Error("Internal Ogg library error.\n");
				FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
				FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
				header_data[0] = 'O';
				header_data[1] = 'p';
				header_data[2] = 'u';
				header_data[3] = 's';
				header_data[4] = 'T';
				header_data[5] = 'a';
				header_data[6] = 'g';
				header_data[7] = 's';
				header_data[8] = 0;
				header_data[9] = 0;
				header_data[10] = 0;
				header_data[11] = 0;
				pt.packet = header_data;
				pt.bytes = 12;
				pt.b_o_s = 0;
				pt.e_o_s = 0;
				pt.granulepos = 0;
				pt.packetno = 1;
				qogg_stream_packetin(&format->audiostream, &pt);
			}
			else
			#endif
			{
				qvorbis_analysis_headerout(&format->vd, &vc, &pt, &pt2, &pt3);
				qogg_stream_packetin(&format->audiostream, &pt);
				if (qogg_stream_pageout (&format->audiostream, &pg) != 1)
					Sys_Error("Internal Ogg library error.\n");
				FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
				FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);

				qogg_stream_packetin(&format->audiostream, &pt2);
				qogg_stream_packetin(&format->audiostream, &pt3);

				qvorbis_comment_clear(&vc);
			}
		}

		for(;;)
		{
			int result = qogg_stream_flush (&format->videostream, &pg);
			if (result < 0)
				Sys_Error("Internal Ogg library error.\n"); // TODO Sys_Error
			if (result <= 0)
				break;
			FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
			FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
		}

		if(cls.capturevideo.soundrate)
		for(;;)
		{
			int result = qogg_stream_flush (&format->audiostream, &pg);
			if (result < 0)
				Sys_Error("Internal Ogg library error.\n"); // TODO Sys_Error
			if (result <= 0)
				break;
			FS_Write(cls.capturevideo.videofile, pg.header, pg.header_len);
			FS_Write(cls.capturevideo.videofile, pg.body, pg.body_len);
		}
	}
}
