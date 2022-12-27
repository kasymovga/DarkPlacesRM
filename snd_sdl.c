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

#include "quakedef.h"
#include "sound.h"
#ifdef CONFIG_VOIP
#include "snd_voip.h"
static int audio_device_capture = 0;
#endif

#include "snd_main.h"


static unsigned int sdlaudiotime = 0;
static int audio_device = 0;

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
static void Buffer_Capture_Callback (void *userdata, Uint8 *stream, int len)
{
	S_VOIP_Capture_Callback(stream, len);
}

static void Snd_OpenInputDevice(const char *name)
{
	SDL_AudioSpec wantspec;
	SDL_AudioSpec obtainspec;
	wantspec.callback = Buffer_Capture_Callback;
	wantspec.userdata = NULL;
	wantspec.freq = VOIP_FREQ;
	wantspec.format = (VOIP_WIDTH == 2 ? AUDIO_S16SYS : AUDIO_U8);
	wantspec.channels = VOIP_CHANNELS;
	wantspec.samples = 2048;  // needs to be a power of 2 on some platforms.
	S_VOIP_Stop();
	S_Echo_Stop();
	if ((audio_device_capture = SDL_OpenAudioDevice(name, 1, &wantspec, &obtainspec, 0)) == 0)
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
}

static void Snd_ListInputDevices_f(void)
{
	int i, n;
	n = SDL_GetNumAudioDevices(true);
	for (i = 0; i < n; i++)
	{
		Con_Printf("%i: %s\n", i, SDL_GetAudioDeviceName(i, true));
	}
}

static void Snd_SetInputDevice_f(void)
{
	int i;
	const char *name;
	if (Cmd_Argc() != 2)
	{
		Con_Printf("Usage: %s <device number>\n", Cmd_Argv(0));
		return;
	}
	i = atoi(Cmd_Argv(1));
	SDL_CloseAudioDevice(audio_device_capture);
	name = SDL_GetAudioDeviceName(i, true);
	if (!name)
	{
		Con_Printf("There is no device %i, use default input device. Check snd_list_input_devices for available devices\n", i);
	}
	Snd_OpenInputDevice(name);
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

	#ifdef CONFIG_VOIP
	Cmd_AddCommand("snd_list_input_devices", Snd_ListInputDevices_f, "list input audio devices");
	Cmd_AddCommand("snd_set_input_device", Snd_SetInputDevice_f, "set input audio device");
	#endif

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
	Snd_OpenInputDevice(NULL);
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
		S_VOIP_Stop();
		SDL_CloseAudioDevice(audio_device_capture);
		audio_device_capture = 0;
	}
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

#ifdef CONFIG_VOIP
/*
====================
SndSys_LockCapture

Get the exclusive lock on capture stream
====================
*/
qboolean SndSys_LockCapture (void)
{
	SDL_LockAudioDevice(audio_device_capture);
	return true;
}

/*
====================
SndSys_UnlockCapture

Release the exclusive lock on capture stream
====================
*/
void SndSys_UnlockCapture (void)
{
	SDL_UnlockAudioDevice(audio_device_capture);
}

/*
====================
SndSys_PauseCapture

Pause capture stream
====================
*/
void SndSys_PauseCapture (void)
{
	SDL_PauseAudioDevice(audio_device_capture, true);
}

/*
====================
SndSys_UnpauseCapture

Unpause capture stream
====================
*/
void SndSys_UnpauseCapture (void)
{
	SDL_PauseAudioDevice(audio_device_capture, false);
}

/*
====================
SndSys_CaptureAvailable

Indicate that capture is available
====================
*/
qboolean SndSys_CaptureAvailable (void)
{
	return audio_device_capture;
}
#endif

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
