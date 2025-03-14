/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#include "quakedef.h"
#include "random.h"

#ifdef CONFIG_VIDEO_CAPTURE
extern cvar_t cl_capturevideo;
extern cvar_t cl_capturevideo_demo_stop;
#endif
#define DEMO_BUFFER_SIZE 16777216
static char *demo_buffer;
static int demo_buffer_length;
static mempool_t *demo_mempool;


static void CL_FinishTimeDemo (void);

#include "timedemo.h"

/*
==============================================================================

DEMO CODE

When a demo is playing back, all outgoing network messages are skipped, and
incoming messages are read from the demo file.

Whenever cl.time gets past the last received message, another message is
read from the demo file.
==============================================================================
*/

static void Demo_FS_Close (void)
{
	if (demo_buffer_length)
	{
		Con_Printf("Flushing demo buffer into file...\n");
		FS_Write(cls.demofile, demo_buffer, demo_buffer_length);
		demo_buffer_length = 0;
	}
	FS_Close (cls.demofile);
}

static void Demo_Finalize (void)
{
	Demo_FS_Close();
	Mem_FreePool(&demo_mempool);
	demo_buffer = NULL;
	demo_buffer_length = 0;
	cls.demofile = NULL;
	cls.demorecording = false;
}

void CL_Demo_Start (const char *name)
{
	if (cls.demorecording)
		return;

	cls.demofile = FS_OpenRealFile(name, "wb", false);
	if (!cls.demofile)
		return;

	demo_mempool = Mem_AllocPool("Demo", 0, NULL);
	demo_buffer = Mem_Alloc(demo_mempool, DEMO_BUFFER_SIZE);
	demo_buffer_length = 0;
	cls.demorecording = true;
}

static void Demo_FS_Write (const void *data, size_t datasize)
{
	if (datasize + demo_buffer_length > DEMO_BUFFER_SIZE)
	{
		Con_Printf("Flushing demo buffer into file...\n");
		FS_Write(cls.demofile, demo_buffer, demo_buffer_length);
		demo_buffer_length = 0;
		if (datasize > DEMO_BUFFER_SIZE)
		{
			FS_Write(cls.demofile, data, datasize);
			return;
		}
	}
	memcpy(&demo_buffer[demo_buffer_length], data, datasize);
	demo_buffer_length += datasize;
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[MAX_INPUTLINE];

	if (cls.demonum == -1)
		return;		// don't play demos

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Print("No demos listed with startdemos\n");
			cls.demonum = -1;
			return;
		}
	}

	dpsnprintf (str, sizeof(str), "playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
// LordHavoc: now called only by CL_Disconnect
void CL_StopPlayback (void)
{
#ifdef CONFIG_VIDEO_CAPTURE
	if (cl_capturevideo_demo_stop.integer)
		Cvar_Set("cl_capturevideo", "0");
#endif

	if (!cls.demoplayback)
		return;

	Demo_Finalize();
	if (cls.timedemo)
		CL_FinishTimeDemo ();

	if (!cls.demostarting) // only quit if not starting another demo
		if (COM_CheckParm("-demo") || COM_CheckParm("-capturedemo"))
			Host_Quit_f();

}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length and view angles
#====================
*/
void CL_WriteDemoMessage (sizebuf_t *message)
{
	int		len;
	int		i;
	float	f;
	int angle_cmd = -1;
	extern cvar_t cl_movement;

	if (cls.demopaused) // LordHavoc: pausedemo
		return;

	len = LittleLong (message->cursize);
	Demo_FS_Write (&len, 4);
	if (cl_movement.integer && !cl.fixangle[1])
	{
		for (i = 0;i < CL_MAX_USERCMDS;i++)
			if (cl.movecmd[i].sequence <= cls.servermovesequence)
			{
				angle_cmd = i - 1;
				break;
			}
	}
	if (angle_cmd >= 0)
	{
		for (i=0 ; i<3 ; i++)
		{
			f = LittleFloat (cl.movecmd[angle_cmd].viewangles[i]);
			Demo_FS_Write (&f, 4);
		}
	}
	else
		for (i=0 ; i<3 ; i++)
		{
			f = LittleFloat (cl.viewangles[i]);
			Demo_FS_Write (&f, 4);
		}
	Demo_FS_Write (message->data, message->cursize);
}

/*
====================
CL_CutDemo

Dumps the current demo to a buffer, and resets the demo to its starting point.
Used to insert csprogs.dat files as a download to the beginning of a demo file.
====================
*/
void CL_CutDemo (unsigned char **buf, fs_offset_t *filesize)
{
	*buf = NULL;
	*filesize = 0;

	Demo_FS_Close();
	*buf = FS_LoadFile(cls.demoname, tempmempool, false, filesize);

	// restart the demo recording
	cls.demofile = FS_OpenRealFile(cls.demoname, "wb", false);
	if(!cls.demofile)
		Sys_Error("failed to reopen the demo file");
	FS_Printf(cls.demofile, "%i\n", cls.forcetrack);
}

/*
====================
CL_PasteDemo

Adds the cut stuff back to the demo. Also frees the buffer.
Used to insert csprogs.dat files as a download to the beginning of a demo file.
====================
*/
void CL_PasteDemo (unsigned char **buf, fs_offset_t *filesize)
{
	fs_offset_t startoffset = 0;

	if(!*buf)
		return;

	// skip cdtrack
	while(startoffset < *filesize && ((char *)(*buf))[startoffset] != '\n')
		++startoffset;
	if(startoffset < *filesize)
		++startoffset;

	Demo_FS_Write(*buf + startoffset, *filesize - startoffset);

	Mem_Free(*buf);
	*buf = NULL;
	*filesize = 0;
}

/*
====================
CL_ReadDemoMessage

Handles playback of demos
====================
*/
void CL_ReadDemoMessage(void)
{
	int i;
	float f, lasttime;
	vec3_t angles;

	if (!cls.demoplayback)
		return;

	// LordHavoc: pausedemo
	if (cls.demopaused)
		return;

	for (;;)
	{
		// decide if it is time to grab the next message
		// always grab until fully connected
		if (cls.signon == SIGNONS)
		{
			if (cls.timedemo)
			{
				cls.td_frames++;
				cls.td_onesecondframes++;
				// if this is the first official frame we can now grab the real
				// td_starttime so the bogus time on the first frame doesn't
				// count against the final report
				if (cls.td_frames == 0)
				{
					cls.td_starttime = realtime;
					cls.td_onesecondnexttime = cl.time + 1;
					cls.td_onesecondrealtime = realtime;
					cls.td_onesecondframes = 0;
					cls.td_onesecondminfps = 0;
					cls.td_onesecondmaxfps = 0;
					cls.td_onesecondavgfps = 0;
					cls.td_onesecondavgcount = 0;
				}
				if (cl.time >= cls.td_onesecondnexttime)
				{
					double fps = cls.td_onesecondframes / (realtime - cls.td_onesecondrealtime);
					if (cls.td_onesecondavgcount == 0)
					{
						cls.td_onesecondminfps = fps;
						cls.td_onesecondmaxfps = fps;
					}
					cls.td_onesecondrealtime = realtime;
					cls.td_onesecondminfps = min(cls.td_onesecondminfps, fps);
					cls.td_onesecondmaxfps = max(cls.td_onesecondmaxfps, fps);
					cls.td_onesecondavgfps += fps;
					cls.td_onesecondavgcount++;
					cls.td_onesecondframes = 0;
					cls.td_onesecondnexttime++;
				}
			}
			else if (cl.time <= cl.mtime[0] && cl.movevars_timescale > 0)
			{
				// don't need another message yet
				return;
			}
		}

		// get the next message
		if (FS_Read(cls.demofile, &cl_message.cursize, 4) < 4)
		{
			Con_Printf("CL_ReadDemoMessage: FS_Read failed\n");
			CL_Disconnect();
			return;
		}
		cl_message.cursize = LittleLong(cl_message.cursize);
		if(cl_message.cursize & DEMOMSG_CLIENT_TO_SERVER) // This is a client->server message! Ignore for now!
		{
			// skip over demo packet
			if (FS_Seek(cls.demofile, 12 + (cl_message.cursize & (~DEMOMSG_CLIENT_TO_SERVER)), SEEK_CUR))
			{
				Con_Printf("CL_ReadDemoMessage: FS_Seek failed\n");
				CL_Disconnect();
				return;
			}
			continue;
		}
		if (cl_message.cursize > cl_message.maxsize)
		{
			Con_Printf("Demo message (%i) > cl_message.maxsize (%i)", cl_message.cursize, cl_message.maxsize);
			cl_message.cursize = 0;
			CL_Disconnect();
			return;
		}
		lasttime = cl.mtime[0];
		for (i = 0;i < 3;i++)
		{
			if (FS_Read(cls.demofile, &f, 4) < 4)
			{
				Con_Printf("CL_ReadDemoMessage: FS_Read failed\n");
				CL_Disconnect();
			}
			angles[i] = LittleFloat(f);
		}

		if (FS_Read(cls.demofile, cl_message.data, cl_message.cursize) == cl_message.cursize)
		{
			MSG_BeginReading(&cl_message);
			CL_ParseServerMessage();
			if (lasttime != cl.mtime[0])
			{
				VectorCopy(cl.mviewangles[0], cl.mviewangles[1]);
				VectorCopy(angles, cl.mviewangles[0]);
			}
			if (cls.signon != SIGNONS)
				Cbuf_Execute(); // immediately execute svc_stufftext if in the demo before connect!

			// In case the demo contains a "svc_disconnect" message
			if (!cls.demoplayback)
				return;

			if (cls.timedemo)
				return;
		}
		else
		{
			Con_Printf("CL_ReadDemoMessage: FS_Read failed\n");
			CL_Disconnect();
			return;
		}
	}
}


/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f (void)
{
	sizebuf_t buf;
	unsigned char bufdata[64];

	if (!cls.demorecording)
	{
		Con_Print("Not recording a demo.\n");
		return;
	}

// write a disconnect message to the demo file
	// LordHavoc: don't replace the cl_message when doing this
	buf.data = bufdata;
	buf.maxsize = sizeof(bufdata);
	SZ_Clear(&buf);
	MSG_WriteByte(&buf, svc_disconnect);
	CL_WriteDemoMessage(&buf);

// finish up
	if(cl_autodemo.integer && (cl_autodemo_delete.integer & 1))
	{
		FS_RemoveOnClose(cls.demofile);
		Con_Print("Completed and deleted demo\n");
	}
	else
		Con_Print("Completed demo\n");
	Demo_Finalize();
}

/*
====================
CL_Record_f

record <demoname> <map> [cd track]
====================
*/
void CL_Record_f (void)
{
	int c, track;
	char name[MAX_OSPATH];
	char vabuf[1024];

	c = Cmd_Argc();
	if (c != 2 && c != 3 && c != 4)
	{
		Con_Print("record <demoname> [<map> [cd track]]\n");
		return;
	}

	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Print("Relative pathnames are not allowed.\n");
		return;
	}

	if (c == 2 && cls.state == ca_connected)
	{
		Con_Print("Can not record - already connected to server\nClient demo recording must be started before connecting\n");
		return;
	}

	if (cls.state == ca_connected)
		CL_Disconnect();

	// write the forced cd track number, or -1
	if (c == 4)
	{
		track = atoi(Cmd_Argv(3));
		Con_Printf("Forcing CD track to %i\n", cls.forcetrack);
	}
	else
		track = -1;

	// get the demo name
	strlcpy (name, Cmd_Argv(1), sizeof (name));
	FS_DefaultExtension (name, ".dem", sizeof (name));

	// start the map up
	if (c > 2)
		Cmd_ExecuteString ( va(vabuf, sizeof(vabuf), "map %s", Cmd_Argv(2)), src_command, false);

	// open the demo file
	Con_Printf("recording to %s.\n", name);
	CL_Demo_Start(name);
	if (!cls.demofile)
	{
		Con_Print("ERROR: couldn't open.\n");
		return;
	}
	strlcpy(cls.demoname, name, sizeof(cls.demoname));

	cls.forcetrack = track;
	FS_Printf(cls.demofile, "%i\n", cls.forcetrack);

	cls.demo_lastcsprogssize = -1;
	cls.demo_lastcsprogscrc = -1;
}


/*
====================
CL_PlayDemo_f

play [demoname]
====================
*/
void CL_PlayDemo_f (void)
{
	char	name[MAX_QPATH];
	int c;
	qboolean neg = false;
	qfile_t *f;

	if (Cmd_Argc() != 2)
	{
		Con_Print("play <demoname> : plays a demo\n");
		return;
	}

	// open the demo file
	strlcpy (name, Cmd_Argv(1), sizeof (name));
	FS_DefaultExtension (name, ".dem", sizeof (name));
	f = FS_OpenVirtualFile(name, false);
	if (!f)
	{
		Con_Printf("ERROR: couldn't open %s.\n", name);
		cls.demonum = -1;		// stop demo loop
		return;
	}

	cls.demostarting = true;

	// disconnect from server
	CL_Disconnect ();
	Host_ShutdownServer ();

	// update networking ports (this is mainly just needed at startup)
	NetConn_UpdateSockets();

	cls.protocol = PROTOCOL_QUAKE;

	Con_Printf("Playing demo %s.\n", name);
	cls.demofile = f;
	strlcpy(cls.demoname, name, sizeof(cls.demoname));

	cls.demoplayback = true;
	cls.state = ca_connected;
	cls.forcetrack = 0;

	while ((c = FS_Getc (cls.demofile)) != '\n')
		if (c == '-')
			neg = true;
		else
			cls.forcetrack = cls.forcetrack * 10 + (c - '0');

	if (neg)
		cls.forcetrack = -cls.forcetrack;

	cls.demostarting = false;
}

typedef struct
{
	int frames;
	double time, totalfpsavg;
	double fpsmin, fpsavg, fpsmax;
}
benchmarkhistory_t;
static size_t doublecmp_offset;
static int doublecmp_withoffset(const void *a_, const void *b_)
{
	const double *a = (const double *) ((const char *) a_ + doublecmp_offset);
	const double *b = (const double *) ((const char *) b_ + doublecmp_offset);
	if(*a > *b)
		return +1;
	if(*a < *b)
		return -1;
	return 0;
}

/*
====================
CL_FinishTimeDemo

====================
*/
static void CL_FinishTimeDemo (void)
{
	int frames;
	int i;
	double time, totalfpsavg;
	double fpsmin, fpsavg, fpsmax; // report min/avg/max fps
	double min_time, mean_time, max_time, stddev_time; // report frame times
	uint64_t min_frame, max_frame;
	static int benchmark_runs = 0;
	char vabuf[1024];

	cls.timedemo = false;

	frames = cls.td_frames;
	time = realtime - cls.td_starttime;
	totalfpsavg = time > 0 ? frames / time : 0;
	fpsmin = cls.td_onesecondminfps;
	fpsavg = cls.td_onesecondavgcount ? cls.td_onesecondavgfps / cls.td_onesecondavgcount : 0;
	fpsmax = cls.td_onesecondmaxfps;
	// LordHavoc: timedemo now prints out 7 digits of fraction, and min/avg/max
	Con_Printf("%i frames %5.7f seconds %5.7f fps, one-second fps min/avg/max: %.0f %.0f %.0f (%i seconds)\n", frames, time, totalfpsavg, fpsmin, fpsavg, fpsmax, cls.td_onesecondavgcount);
	Cvar_LockThreadMutex();
	Log_Printf("benchmark.log", "date %s | enginedate %s | demo %s | commandline %s | run %d | result %i frames %5.7f seconds %5.7f fps, one-second fps min/avg/max: %.0f %.0f %.0f (%i seconds)\n", Sys_TimeString("%Y-%m-%d %H:%M:%S"), buildstring, cls.demoname, cmdline.string, benchmark_runs + 1, frames, time, totalfpsavg, fpsmin, fpsavg, fpsmax, cls.td_onesecondavgcount);
	Cvar_UnlockThreadMutex();
	min_time = TimeDemo_Min(&tdstats);
	max_time = TimeDemo_Max(&tdstats);
	mean_time = TimeDemo_Mean(&tdstats);
	stddev_time = sqrt(TimeDemo_Variance(&tdstats));
	min_frame = TimeDemo_MinIndex(&tdstats);
	max_frame = TimeDemo_MaxIndex(&tdstats);
	Con_Printf("%5.7fms @ %" PRIu64 ", %5.7fms, %5.7fms @ %" PRIu64 " min/mean/max, %5.7fms standard deviation\n",
		min_time, min_frame, mean_time, max_time, max_frame, stddev_time);
	Log_Printf("benchmark.log", "        | %5.7fms @ %" PRIu64 " %5.7fms %5.7fms @ %" PRIu64 " %5.7fms\n",
		min_time, min_frame, mean_time, max_time, max_frame, stddev_time);
	TimeDemo_Reset(&tdstats);
	if (COM_CheckParm("-benchmark"))
	{
		++benchmark_runs;
		i = COM_CheckParm("-benchmarkruns");
		if(i && i + 1 < com_argc)
		{
			static benchmarkhistory_t *history = NULL;
			if(!history)
				history = (benchmarkhistory_t *)Z_Malloc(sizeof(*history) * atoi(com_argv[i + 1]));

			history[benchmark_runs - 1].frames = frames;
			history[benchmark_runs - 1].time = time;
			history[benchmark_runs - 1].totalfpsavg = totalfpsavg;
			history[benchmark_runs - 1].fpsmin = fpsmin;
			history[benchmark_runs - 1].fpsavg = fpsavg;
			history[benchmark_runs - 1].fpsmax = fpsmax;

			if(atoi(com_argv[i + 1]) > benchmark_runs)
			{
				// restart the benchmark
				Cbuf_AddText(va(vabuf, sizeof(vabuf), "timedemo %s\n", cls.demoname));
				// cannot execute here
			}
			else
			{
				// print statistics
				int first = COM_CheckParm("-benchmarkruns_skipfirst") ? 1 : 0;
				if(benchmark_runs > first)
				{
#define DO_MIN(f) \
					for(i = first; i < benchmark_runs; ++i) if((i == first) || (history[i].f < f)) f = history[i].f

#define DO_MAX(f) \
					for(i = first; i < benchmark_runs; ++i) if((i == first) || (history[i].f > f)) f = history[i].f

#define DO_MED(f) \
					doublecmp_offset = (char *)&history->f - (char *)history; \
					qsort(history + first, benchmark_runs - first, sizeof(*history), doublecmp_withoffset); \
					if((first + benchmark_runs) & 1) \
						f = history[(first + benchmark_runs - 1) / 2].f; \
					else \
						f = (history[(first + benchmark_runs - 2) / 2].f + history[(first + benchmark_runs) / 2].f) / 2

					DO_MIN(frames);
					DO_MAX(time);
					DO_MIN(totalfpsavg);
					DO_MIN(fpsmin);
					DO_MIN(fpsavg);
					DO_MIN(fpsmax);
					Con_Printf("MIN: %i frames %5.7f seconds %5.7f fps, one-second fps min/avg/max: %.0f %.0f %.0f (%i seconds)\n", frames, time, totalfpsavg, fpsmin, fpsavg, fpsmax, cls.td_onesecondavgcount);

					DO_MED(frames);
					DO_MED(time);
					DO_MED(totalfpsavg);
					DO_MED(fpsmin);
					DO_MED(fpsavg);
					DO_MED(fpsmax);
					Con_Printf("MED: %i frames %5.7f seconds %5.7f fps, one-second fps min/avg/max: %.0f %.0f %.0f (%i seconds)\n", frames, time, totalfpsavg, fpsmin, fpsavg, fpsmax, cls.td_onesecondavgcount);

					DO_MAX(frames);
					DO_MIN(time);
					DO_MAX(totalfpsavg);
					DO_MAX(fpsmin);
					DO_MAX(fpsavg);
					DO_MAX(fpsmax);
					Con_Printf("MAX: %i frames %5.7f seconds %5.7f fps, one-second fps min/avg/max: %.0f %.0f %.0f (%i seconds)\n", frames, time, totalfpsavg, fpsmin, fpsavg, fpsmax, cls.td_onesecondavgcount);
				}
				Z_Free(history);
				history = NULL;
				Host_Quit_f();
			}
		}
		else
			Host_Quit_f();
	}
}

/*
====================
CL_TimeDemo_f

timedemo [demoname]
====================
*/
void CL_TimeDemo_f (void)
{
	if (Cmd_Argc() != 2)
	{
		Con_Print("timedemo <demoname> : gets demo speeds\n");
		return;
	}

	srand(0); // predictable random sequence for benchmarking
    Xrand_Init(1);

	CL_PlayDemo_f ();

// cls.td_starttime will be grabbed at the second frame of the demo, so
// all the loading time doesn't get counted

	// instantly hide console and deactivate it
	key_dest = key_game;
	key_consoleactive = 0;
	scr_con_current = 0;

	cls.timedemo = true;
	cls.td_frames = -2;		// skip the first frame
	cls.demonum = -1;		// stop demo loop
}

