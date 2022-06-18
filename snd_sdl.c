/*
Copyright (C) 2004 Andreas Kirsch

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#include <math.h>
#include <SDL.h>
#ifdef CONFIG_VOIP
#include <opus.h>
#endif

#include "quakedef.h"
#include "sound.h"

#include "snd_main.h"


static unsigned int sdlaudiotime = 0;
static int audio_device = 0;
#ifdef CONFIG_VOIP
static int audio_device_capture = 0;
#define VOIP_FREQ 16000
#define VOIP_WIDTH 2
#define VOIP_CHANNELS 1
#define VOIP_OPUS_BITRATE 48000
#endif


// Note: SDL calls SDL_LockAudio() right before this function, so no need to lock the audio data here
static void Buffer_Callback (void *userdata, Uint8 *stream, int len)
{
	unsigned int factor, RequestedFrames, MaxFrames, FrameCount;
	unsigned int StartOffset, EndOffset;

	factor = snd_renderbuffer->format.channels * snd_renderbuffer->format.width;
	if ((unsigned int)len % factor != 0)
		Sys_Error("SDL sound: invalid buffer length passed to Buffer_Callback (%d bytes)\n", len);

	RequestedFrames = (unsigned int)len / factor;

	if (SndSys_LockRenderBuffer())
	{
		if (snd_usethreadedmixing)
		{
			S_MixToBuffer(stream, RequestedFrames);
			if (snd_blocked)
				memset(stream, snd_renderbuffer->format.width == 1 ? 0x80 : 0, len);
			SndSys_UnlockRenderBuffer();
			return;
		}
		// Transfert up to a chunk of samples from snd_renderbuffer to stream
		MaxFrames = snd_renderbuffer->endframe - snd_renderbuffer->startframe;
		if (MaxFrames > RequestedFrames)
			FrameCount = RequestedFrames;
		else
			FrameCount = MaxFrames;

		if (FrameCount < RequestedFrames)
		{
			memset(stream, 0, len);
			if (developer_insane.integer && vid_activewindow)
				Con_DPrintf("SDL sound: %u sample frames missing\n", RequestedFrames - FrameCount);
		}
		StartOffset = snd_renderbuffer->startframe % snd_renderbuffer->maxframes;
		EndOffset = (snd_renderbuffer->startframe + FrameCount) % snd_renderbuffer->maxframes;
		if (StartOffset > EndOffset)  // if the buffer wraps
		{
			unsigned int PartialLength1, PartialLength2;

			PartialLength1 = (snd_renderbuffer->maxframes - StartOffset) * factor;
			memcpy(stream, &snd_renderbuffer->ring[StartOffset * factor], PartialLength1);

			PartialLength2 = FrameCount * factor - PartialLength1;
			memcpy(&stream[PartialLength1], &snd_renderbuffer->ring[0], PartialLength2);
		}
		else
			memcpy(stream, &snd_renderbuffer->ring[StartOffset * factor], FrameCount * factor);

		snd_renderbuffer->startframe += FrameCount;

		sdlaudiotime += RequestedFrames;

		SndSys_UnlockRenderBuffer();
	}
}

#ifdef CONFIG_VOIP
#define CAPTURE_BUFFER_SIZE 65536
static char *capture_buffer;
static volatile int capture_buffer_begin;
static volatile int capture_buffer_filled;
static long int capture_buffer_pos;
static qboolean snd_voip_active;
static sfx_t *snd_echo;
static OpusEncoder *opus_encoder;
static char opus_encoder_id;
static unsigned int opus_encoder_seq;
static void Buffer_Capture_Callback (void *userdata, Uint8 *stream, int len)
{
	int buffer_free = CAPTURE_BUFFER_SIZE - capture_buffer_filled;
	//len = (len / (VOIP_WIDTH * VOIP_CHANNELS)) * VOIP_WIDTH * VOIP_CHANNELS;
	if (len > buffer_free)
	{
		len = buffer_free;
		Con_DPrintf("Capture sound buffer truncated\n");
	}
	memcpy(capture_buffer + capture_buffer_filled, stream, len);
	capture_buffer_filled += len;
	if (!snd_echo && snd_voip_active && cls.state == ca_connected)
	{
		while (capture_buffer_filled - capture_buffer_begin > 1920)
		{
			unsigned char packet[1036];
			int encsize;
			encsize = opus_encode(opus_encoder, (opus_int16*)(capture_buffer + capture_buffer_begin), 960, (unsigned char*)&packet[12], 1024);
			packet[0] = 'V';
			packet[1] = 'O';
			packet[2] = 'I';
			packet[3] = 'P';
			packet[4] = '\0'; //client num, filled by server
			packet[5] = '\0';
			packet[6] = opus_encoder_id;
			packet[7] = '\0';
			packet[8] = opus_encoder_seq & 0xFF;
			packet[9] = (opus_encoder_seq >> 8) & 0xFF;
			packet[10] = (opus_encoder_seq >> 16) & 0xFF;
			packet[11] = (opus_encoder_seq >> 24) & 0xFF;
			if (encsize < 0)
			{
				Con_Printf("Opus encode failed %i\n", encsize);
				return;
			}
			opus_encoder_seq++;
			capture_buffer_begin += 1920;
			NetConn_Write(cls.connect_mysocket, packet, 12 + encsize, &cls.connect_address);
			if (capture_buffer_begin > CAPTURE_BUFFER_SIZE / 2) {
				memcpy(capture_buffer, capture_buffer + capture_buffer_begin, capture_buffer_filled - capture_buffer_begin);
				capture_buffer_filled -= capture_buffer_begin;
				capture_buffer_begin = 0;
			}
		}
	}
}

static void Echo_GetSamplesFloat(channel_t *ch, sfx_t *sfx, int firstsampleframe, int numsampleframes, float *outsamplesfloat)
{
	int i, len;
	int bytesrequired;
	int shift;
	shift = firstsampleframe - capture_buffer_pos;
	if (-shift * sfx->format.width * sfx->format.channels > capture_buffer_begin )
	{
		//too far in past
		capture_buffer_pos = firstsampleframe;
		Con_DPrintf("Captured sound samples missed (too far in past)\n");
		return;
	}
	SDL_LockAudioDevice(audio_device_capture);
	capture_buffer_begin += shift * sfx->format.width * sfx->format.channels;
	capture_buffer_pos += shift;
	if (capture_buffer_begin >= capture_buffer_filled)
	{
		capture_buffer_begin = capture_buffer_filled;
		capture_buffer_pos = firstsampleframe + numsampleframes;
		return;
	}
	len = numsampleframes * sfx->format.channels;
	bytesrequired = len * sfx->format.width;
	if (bytesrequired > capture_buffer_filled - capture_buffer_begin) {
		bytesrequired = capture_buffer_filled - capture_buffer_begin;
		len = bytesrequired / sfx->format.width;
		numsampleframes = len / sfx->format.channels;
		Con_DPrintf("Captured sound samples missed (too far in future)\n");
	}
	if (sfx->format.width == 2)
	{
		const short *bufs = (const short *)capture_buffer + (capture_buffer_begin / 2);
		for (i = 0;i < len;i++)
			outsamplesfloat[i] = bufs[i] * (1.0f / 32768.0f);
	}
	else
	{
		const signed char *bufb = (const signed char *)capture_buffer + capture_buffer_begin;
		for (i = 0;i < len;i++)
			outsamplesfloat[i] = bufb[i] * (1.0f / 128.0f);
	}
	capture_buffer_begin += bytesrequired;
	capture_buffer_pos += numsampleframes;
	if (capture_buffer_begin > CAPTURE_BUFFER_SIZE / 2 + 64) {
		memcpy(capture_buffer, capture_buffer + capture_buffer_begin, capture_buffer_filled - capture_buffer_begin);
		capture_buffer_filled -= (capture_buffer_begin - 64);
		capture_buffer_begin = 64;
	}
	SDL_UnlockAudioDevice(audio_device_capture);
}

static void Echo_FreeSfx(sfx_t *sfx)
{
	snd_echo = NULL;
}

static const snd_fetcher_t echo_fetcher = { Echo_GetSamplesFloat, NULL, Echo_FreeSfx };

void SndSys_Echo_Start(void)
{
	if (!audio_device_capture)
	{
		Con_Print("Audio capture device not available\n");
		return;
	}
	if (snd_voip_active)
	{
		Con_Print("Audio capture device busy\n");
		return;
	}
	if (snd_echo) return;
	SndSys_LockRenderBuffer();
	snd_echo = S_FindName("*echostream");
	snd_echo->format.speed = VOIP_FREQ;
	snd_echo->format.width = VOIP_WIDTH;
	snd_echo->format.channels = VOIP_CHANNELS;
	snd_echo->fetcher = &echo_fetcher;
	snd_echo->total_length = 99999999;
	snd_echo->volume_mult = 1;
	snd_echo->flags = SFXFLAG_VOIP;
	if (!capture_buffer)
		capture_buffer = Mem_Alloc(snd_mempool, CAPTURE_BUFFER_SIZE);
	memset(capture_buffer, VOIP_WIDTH == 1 ? 0x80 : 0, CAPTURE_BUFFER_SIZE);
	capture_buffer_filled = 16384;
	capture_buffer_begin = 0;
	S_StartSound_StartPosition_Flags(-2, 0, snd_echo, (float[3]){0, 0, 0}, 1, 0, 0, CHANNELFLAG_FULLVOLUME | CHANNELFLAG_LOCALSOUND, 1.0f);
	snd_echo->loopstart = 100000000;
	SndSys_UnlockRenderBuffer();
	SDL_PauseAudioDevice(audio_device_capture, 0);
	return;
}

void S_FreeSfx (sfx_t *sfx, qboolean force);
void SndSys_Echo_Stop(void)
{
	if (!audio_device_capture) return;
	if (!snd_echo)
	{
		Con_Print("No echo to stop\n");
		return;
	}
	SDL_PauseAudioDevice(audio_device_capture, 1);
	SndSys_LockRenderBuffer();
	capture_buffer_begin = 0;
	capture_buffer_filled = 0;
	capture_buffer_pos = 0;
	S_FreeSfx(snd_echo, 1);
	snd_echo = NULL;
	SndSys_UnlockRenderBuffer();
}

void SndSys_VOIP_Start(void)
{
	if (snd_voip_active) return;
	if (!capture_buffer)
		capture_buffer = Mem_Alloc(snd_mempool, CAPTURE_BUFFER_SIZE);

	if (snd_echo)
	{
		Con_Print("Audio capture device busy\n");
		return;
	}
	if (!opus_encoder)
	{
		int err;
		opus_encoder = opus_encoder_create(VOIP_FREQ, VOIP_CHANNELS, OPUS_APPLICATION_AUDIO, &err);
		opus_encoder_id++;
		opus_encoder_seq = 0;
		if (opus_encoder)
		{
			err = opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(VOIP_OPUS_BITRATE));
			if (err)
			{
				Con_Printf("Opus encoder bitrate setup failed\n");
				opus_encoder_destroy(opus_encoder);
				opus_encoder = NULL;
				return;
			}
		}
		else
		{
			Con_Printf("Opus encoder creation failed\n");
			return;
		}
	}
	capture_buffer_begin = 0;
	capture_buffer_filled = 0;
	snd_voip_active = true;
	SDL_PauseAudioDevice(audio_device_capture, 0);
}

void SndSys_VOIP_Stop(void)
{
	if (!snd_voip_active) return;
	SDL_PauseAudioDevice(audio_device_capture, 1);
	snd_voip_active = false;
	if (opus_encoder)
	{
		opus_encoder_destroy(opus_encoder);
		opus_encoder = NULL;
	}
}

static sfx_t *snd_voip_speakers[128];
static char *voip_buffer[128];
static int voip_buffer_begin[128];
static int voip_buffer_filled[128];
static long int voip_buffer_pos[128];

static void VOIP_GetSamplesFloat(channel_t *ch, sfx_t *sfx, int firstsampleframe, int numsampleframes, float *outsamplesfloat)
{
	int client = (sfx_t**)sfx->fetcher_data - snd_voip_speakers;
	char *buffer = voip_buffer[client];
	int *buffer_begin = &voip_buffer_begin[client];
	int *buffer_filled = &voip_buffer_filled[client];
	long int *buffer_pos = &voip_buffer_pos[client];
	int i, len;
	int bytesrequired;
	int shift;
	shift = firstsampleframe - *buffer_pos;
	if (-shift * sfx->format.width * sfx->format.channels > *buffer_begin )
	{
		//too far in past
		*buffer_pos = firstsampleframe;
		Con_DPrintf("VOIP sound samples missed (too far in past)\n");
		return;
	}
	*buffer_begin += shift * sfx->format.width * sfx->format.channels;
	if (*buffer_begin >= *buffer_filled)
	{
		*buffer_begin = *buffer_filled;
		*buffer_pos = firstsampleframe + numsampleframes;
		if (sfx->total_length == 99999999)
		{
			Con_DPrintf("Schedule voip sfx remove\n");
			sfx->total_length = firstsampleframe;
		}
		return;
	}
	*buffer_pos += shift;
	len = numsampleframes * sfx->format.channels;
	bytesrequired = len * sfx->format.width;
	if (bytesrequired > *buffer_filled - *buffer_begin) {
		bytesrequired = *buffer_filled - *buffer_begin;
		len = bytesrequired / sfx->format.width;
		numsampleframes = len / sfx->format.channels;
		Con_DPrintf("VOIP sound samples missed (too far in future)\n");
	}
	if (sfx->format.width == 2)
	{
		const short *bufs = (const short *)buffer + (*buffer_begin / 2);
		for (i = 0;i < len;i++)
			outsamplesfloat[i] = bufs[i] * (1.0f / 32768.0f);
	}
	else
	{
		const signed char *bufb = (const signed char *)buffer + *buffer_begin;
		for (i = 0;i < len;i++)
			outsamplesfloat[i] = bufb[i] * (1.0f / 128.0f);
	}
	*buffer_begin += bytesrequired;
	*buffer_pos += numsampleframes;
	if (*buffer_begin > CAPTURE_BUFFER_SIZE / 2 + 64) {
		memcpy(buffer, buffer + *buffer_begin, *buffer_filled - *buffer_begin);
		*buffer_filled -= (*buffer_begin - 64);
		*buffer_begin = 64;
	}
}

static OpusDecoder *opus_decoder[128];
static void VOIP_FreeSfx(sfx_t *sfx)
{
	int client = (sfx_t**)sfx->fetcher_data - snd_voip_speakers;
	*((sfx_t**)sfx->fetcher_data) = NULL;
	if (opus_decoder[client])
	{
		opus_decoder_destroy(opus_decoder[client]);
		opus_decoder[client] = NULL;
	}
}

static snd_fetcher_t voip_fetcher = { VOIP_GetSamplesFloat, NULL, VOIP_FreeSfx };

static unsigned int opus_decoder_seq[128];
static char opus_decoder_id[128];

void SndSys_VOIP_Received(unsigned char *packet, int len, int client)
{
	int buffer_free;
	char *buffer;
	int *buffer_filled;
	char packet_decoded[1920];
	unsigned int seq;
	if (client < 0 || client > 128)
	{
		Con_Printf("Bad VOIP packet\n");
		return;
	}
	if (len <= 8)
		return;
	
	if (!voip_buffer[client]) voip_buffer[client] = Mem_Alloc(snd_mempool, CAPTURE_BUFFER_SIZE);
	SndSys_LockRenderBuffer();
	if (opus_decoder_id[client] != packet[0])
	{
		if (opus_decoder[client])
		{
			opus_decoder_destroy(opus_decoder[client]);
			opus_decoder[client] = NULL;
		}
		opus_decoder_id[client] = packet[0];
	}
	if (!opus_decoder[client])
	{
		int err;
		opus_decoder[client] = opus_decoder_create(VOIP_FREQ, VOIP_CHANNELS, &err);
		if (!opus_decoder[client])
		{
			SndSys_UnlockRenderBuffer();
			Con_Printf("Opus decoder creation failed\n");
			return;
		}
		opus_decoder_seq[client] = 0;
	}
	if (!snd_voip_speakers[client])
	{
		char stream_name[32];
		sfx_t *sfx;
		dpsnprintf(stream_name, 32, "*voipstream/%i", client);
		sfx = S_FindName(stream_name);
		sfx->format.speed = VOIP_FREQ;
		sfx->format.width = VOIP_WIDTH;
		sfx->format.channels = VOIP_CHANNELS;
		sfx->fetcher = &voip_fetcher;
		sfx->fetcher_data = &snd_voip_speakers[client];
		sfx->total_length = 99999999;
		sfx->volume_mult = 1;
		sfx->flags = SFXFLAG_VOIP;
		snd_voip_speakers[client] = sfx;
		S_StartSound_StartPosition_Flags(-3 - client, 0, sfx, (float[3]){0, 0, 0}, 1, 0, 0, CHANNELFLAG_FULLVOLUME | CHANNELFLAG_LOCALSOUND, 1.0f);
		sfx->loopstart = 100000000;
	}
	else
	{
		snd_voip_speakers[client]->total_length = 99999999;
	}
	SndSys_UnlockRenderBuffer();
	buffer = voip_buffer[client];
	buffer_filled = &voip_buffer_filled[client];
	buffer_free = CAPTURE_BUFFER_SIZE - *buffer_filled;
	seq = packet[2] + packet[3] * 256 + packet[4] * 65536 + packet[5] * 16777216;
	if (seq < opus_decoder_seq[client])
	{
		Con_DPrintf("Wrong ordered packet dropped\n");
		return;
	}
	if (seq > opus_decoder_seq[client])
	{
		Con_DPrintf("Opus frame missed\n");
		if (seq - opus_decoder_seq[client] > 10)
			opus_decoder_seq[client] = seq - 10;
		while (seq > opus_decoder_seq[client])
		{
			len = opus_decode(opus_decoder[client], NULL, 0, (opus_int16*)packet_decoded, 960, 0);
			if (len > 0)
			{
				len *= 2;
				if (len > buffer_free)
					len = buffer_free;
				memcpy(buffer + *buffer_filled, packet_decoded, len);
				*buffer_filled += len;
				buffer_free -= len;
			}
			opus_decoder_seq[client]++;
		}
	}
	opus_decoder_seq[client]++;
	len -= 6;
	packet += 6;
	len = opus_decode(opus_decoder[client], (unsigned char*)packet, len, (opus_int16*)packet_decoded, 960, 0);
	if (len < 0)
	{
		Con_Printf("Opus decode failed\n");
		return;
	}
	len *= 2;
	if (len > buffer_free)
	{
		len = buffer_free;
		Con_DPrintf("VOIP sound buffer truncated\n");
	}
	memcpy(buffer + *buffer_filled, packet_decoded, len);
	*buffer_filled += len;
}
#endif

/*
====================
SndSys_Init

Create "snd_renderbuffer" with the proper sound format if the call is successful
May return a suggested format if the requested format isn't available
====================
*/
qboolean SndSys_Init (const snd_format_t* requested, snd_format_t* suggested)
{
	unsigned int buffersize;
	SDL_AudioSpec wantspec;
	SDL_AudioSpec obtainspec;

	snd_threaded = false;

	Con_DPrint ("SndSys_Init: using the SDL module\n");

	// Init the SDL Audio subsystem
	if( SDL_InitSubSystem( SDL_INIT_AUDIO ) ) {
		Con_Print( "Initializing the SDL Audio subsystem failed!\n" );
		return false;
	}

	buffersize = (unsigned int)ceil((double)requested->speed / 25.0); // 2048 bytes on 24kHz to 48kHz

	// Init the SDL Audio subsystem
	wantspec.callback = Buffer_Callback;
	wantspec.userdata = NULL;
	wantspec.freq = requested->speed;
	wantspec.format = ((requested->width == 1) ? AUDIO_U8 : AUDIO_S16SYS);
	wantspec.channels = requested->channels;
	wantspec.samples = CeilPowerOf2(buffersize);  // needs to be a power of 2 on some platforms.

	Con_Printf("Wanted audio Specification:\n"
				"\tChannels  : %i\n"
				"\tFormat    : 0x%X\n"
				"\tFrequency : %i\n"
				"\tSamples   : %i\n",
				wantspec.channels, wantspec.format, wantspec.freq, wantspec.samples);

	if ((audio_device = SDL_OpenAudioDevice(NULL, 0, &wantspec, &obtainspec, 0)) == 0)
	{
		Con_Printf( "Failed to open the audio device! (%s)\n", SDL_GetError() );
		return false;
	}
	Con_Printf("Obtained audio specification:\n"
				"    Channels  : %i\n"
				"    Format    : 0x%X\n"
				"    Frequency : %i\n"
				"    Samples   : %i\n",
				obtainspec.channels, obtainspec.format, obtainspec.freq, obtainspec.samples);

	// If we haven't obtained what we wanted
	if (wantspec.freq != obtainspec.freq ||
		wantspec.format != obtainspec.format ||
		wantspec.channels != obtainspec.channels)
	{
		SDL_CloseAudioDevice(audio_device);
		// Pass the obtained format as a suggested format
		if (suggested != NULL)
		{
			suggested->speed = obtainspec.freq;
			// FIXME: check the format more carefully. There are plenty of unsupported cases
			suggested->width = ((obtainspec.format == AUDIO_U8) ? 1 : 2);
			suggested->channels = obtainspec.channels;
		}

		return false;
	}
	#ifdef CONFIG_VOIP
	wantspec.callback = Buffer_Capture_Callback;
	wantspec.userdata = NULL;
	wantspec.freq = VOIP_FREQ;
	wantspec.format = (VOIP_WIDTH == 2 ? AUDIO_S16SYS : AUDIO_U8);
	wantspec.channels = VOIP_CHANNELS;
	wantspec.samples = 2048;  // needs to be a power of 2 on some platforms.
	snd_voip_active = false;
	if ((audio_device_capture = SDL_OpenAudioDevice(NULL, 1, &wantspec, &obtainspec, 0)) == 0)
	{
		Con_Printf( "Failed to open the audio capture device! (%s)\n", SDL_GetError() );
	}
	else
	{
		Con_Printf("Obtained audio capture specification:\n"
					"   Channels  : %i\n"
					"   Format    : 0x%X\n"
					"   Frequency : %i\n"
					"   Samples   : %i\n",
					obtainspec.channels, obtainspec.format, obtainspec.freq, obtainspec.samples);
		SDL_PauseAudioDevice(audio_device_capture, 1);
	}
	#endif

	snd_threaded = true;

	snd_renderbuffer = Snd_CreateRingBuffer(requested, 0, NULL);
	if (snd_channellayout.integer == SND_CHANNELLAYOUT_AUTO)
		Cvar_SetValueQuick (&snd_channellayout, SND_CHANNELLAYOUT_STANDARD);

	sdlaudiotime = 0;
	SDL_PauseAudioDevice(audio_device, 0);
	return true;
}


/*
====================
SndSys_Shutdown

Stop the sound card, delete "snd_renderbuffer" and free its other resources
====================
*/
void SndSys_Shutdown(void)
{
	if (audio_device > 0) {
		SDL_CloseAudioDevice(audio_device);
		audio_device = 0;
	}
	#ifdef CONFIG_VOIP
	if (audio_device_capture > 0) {
		SDL_CloseAudioDevice(audio_device_capture);
		audio_device_capture = 0;
	}
	if (opus_encoder)
		opus_encoder_destroy(opus_encoder);
	#endif
	if (snd_renderbuffer != NULL)
	{
		Mem_Free(snd_renderbuffer->ring);
		Mem_Free(snd_renderbuffer);
		snd_renderbuffer = NULL;
	}
}


/*
====================
SndSys_Submit

Submit the contents of "snd_renderbuffer" to the sound card
====================
*/
void SndSys_Submit (void)
{
	// Nothing to do here (this sound module is callback-based)
}


/*
====================
SndSys_GetSoundTime

Returns the number of sample frames consumed since the sound started
====================
*/
unsigned int SndSys_GetSoundTime (void)
{
	return sdlaudiotime;
}


/*
====================
SndSys_LockRenderBuffer

Get the exclusive lock on "snd_renderbuffer"
====================
*/
qboolean SndSys_LockRenderBuffer (void)
{
	SDL_LockAudioDevice(audio_device);
	return true;
}


/*
====================
SndSys_UnlockRenderBuffer

Release the exclusive lock on "snd_renderbuffer"
====================
*/
void SndSys_UnlockRenderBuffer (void)
{
	SDL_UnlockAudioDevice(audio_device);
}

/*
====================
SndSys_SendKeyEvents

Send keyboard events originating from the sound system (e.g. MIDI)
====================
*/
void SndSys_SendKeyEvents(void)
{
	// not supported
}
