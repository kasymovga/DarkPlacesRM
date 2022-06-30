#include "quakedef.h"
#include "net_file_client.h"
#include "net_file_server.h"
#include "fs.h"

#define NET_FILE_CLIENT_REQUESTS_COUNT 128

#include <stdio.h>
#ifdef WIN32
#include <io.h>
#if _MSC_VER >= 1400
# define unlink _unlink
#endif
#else
#include <unistd.h>
#endif

static char dl_filename[NET_FILE_NAME_SIZE];
static int dl_id;
static int dl_status;
static unsigned int dl_chunks;
static qfile_t *dl_file;
static unsigned int dl_received;
static qboolean dl_success;
struct chunk_request
{
	unsigned int pos;
	double time;
	qboolean done;
};
static struct chunk_request dl_requested[NET_FILE_CLIENT_REQUESTS_COUNT];
static unsigned int dl_requested_count;
static unsigned int dl_requested_max;
static double dl_request_last_time;
static double dl_start_time;
static double dl_received_last_time;

enum {
	NET_FILE_DOWNLOAD_FINISHED = 0,
	NET_FILE_DOWNLOAD_BEGIN,
	NET_FILE_DOWNLOAD_ACTIVE,
};

void Net_File_Client_Packet(unsigned char *packet, unsigned int size)
{
	unsigned int i;
	if (dl_status == NET_FILE_DOWNLOAD_BEGIN && packet[0] == 'R' && size > 6 && size <= 262) //request answer
	{
		char filename[NET_FILE_NAME_SIZE];
		char filename_dlcache[NET_FILE_NAME_SIZE + 8];
		unsigned char id;
		unsigned int chunks_count;
		id = packet[1];
		chunks_count = (packet[2] << 24) + (packet[3] << 16) + (packet[4] << 8) + packet[5];
		if (chunks_count == 0) return;
		memcpy(filename, &packet[6], size - 6);
		filename[size - 6] = '\0';
		if (strcmp(dl_filename, filename))
		{
			Con_Printf("Net_File_Client_Packet: wrong file name: expected: %s, got: %s\n", dl_filename, filename);
			return;
		}
		dl_status = NET_FILE_DOWNLOAD_ACTIVE;
		dl_id = id;
		dl_chunks = chunks_count;
		dpsnprintf(filename_dlcache, sizeof(filename_dlcache), "dlcache/%s", filename);
		dl_file = FS_OpenRealFile(filename_dlcache, "wb", false);
	}
	else if (packet[0] == 'C') //Chunk
	{
		unsigned int chunk_pos;
		long int file_pos;
		int chunk_size = size - 6;
		if (dl_status != NET_FILE_DOWNLOAD_ACTIVE) return;
		if (chunk_size > NET_FILE_CHUNK_SIZE)
		{
			Con_Printf("Net_File_Client_Packet: chunk size too big\n");
			return;
		}
		if (packet[1] != dl_id)
		{
			Con_Printf("Net_File_Client_Packet: wrong id\n");
			return;
		}
		chunk_pos = (packet[2] << 24) + (packet[3] << 16) + (packet[4] << 8) + packet[5];
		if (chunk_pos + 1 < dl_chunks && chunk_size != NET_FILE_CHUNK_SIZE)
		{
			Con_Printf("Net_File_Client_Packet: wrong chunk size\n");
			return;
		}
		if (chunk_pos >= dl_chunks)
		{
			Con_Printf("Net_File_Client_Packet: wrong chunk position\n");
			return;
		}
		file_pos = chunk_pos * NET_FILE_CHUNK_SIZE;
		for (i = 0; i < dl_requested_count; i++)
		{
			if (dl_requested[i].pos == chunk_pos)
				break;
		}
		if (i == dl_requested_count)
		{
			Con_Printf("Net_File_Client_Packet: unexpected chunk %u (expected count of chunks: %u)\n", chunk_pos, dl_requested_count);
			return;
		}
		if (FS_Tell(dl_file) != file_pos)
			FS_Seek(dl_file, file_pos, SEEK_SET);

		if (FS_Tell(dl_file) != file_pos)
		{
			Con_Printf("seek to %li failed\n", file_pos);
			return;
		}
		if (FS_Write(dl_file, &packet[6], chunk_size) != chunk_size)
		{
			Con_Printf("write failed\n");
			return;
		}
		if (chunk_pos == dl_received)
		{
			qboolean need_check_received = true;
			dl_received++;
			memmove(&dl_requested[i], &dl_requested[i + 1], (dl_requested_count - i - 1) * sizeof(struct chunk_request));
			dl_requested_count--;
			while (need_check_received)
			{
				for (i = 0; i < dl_requested_count; i++)
				{
					if (dl_requested[i].done && dl_requested[i].pos == dl_received)
					{
						dl_received++;
						memmove(&dl_requested[i], &dl_requested[i + 1], (dl_requested_count - i - 1) * sizeof(struct chunk_request));
						dl_requested_count--;
						break;
					}
				}
				if (i == dl_requested_count) need_check_received = false;
			}
		}
		else
			dl_requested[i].done = true;

		dl_received_last_time = realtime;
		if (dl_received == dl_chunks)
		{
			char answer[3];
			Con_Printf("Net_File_Client_Packet: %s downloaded\n", dl_filename);
			dl_success = TRUE;
			dl_status = NET_FILE_DOWNLOAD_FINISHED;
			FS_Close(dl_file);
			dl_file = NULL;
			answer[0] = 'F';
			answer[1] = 'E';
			answer[2] = dl_id;
			NetConn_Write(cls.connect_mysocket, answer, sizeof(answer), &cls.connect_address);
			dl_filename[0] = '\0';
		}
	}
}

static void Net_File_Client_Request(const char *filename)
{
	char packet[NET_FILE_NAME_SIZE + 1];
	int len = strlen(filename);
	if (len + 1 > NET_FILE_NAME_SIZE) return;
	packet[0] = 'F';
	packet[1] = 'R';
	memcpy(&packet[2], filename, len);
	NetConn_Write(cls.connect_mysocket, packet, len + 2, &cls.connect_address);
}

qboolean Net_File_Client_Download_Start(const char *filename)
{
	Con_Printf("Net_File_Client_Download_Start: %s\n", filename);
	if (!strncmp(filename, "dlcache/", 8)) filename += 8;
	if (!strcmp(filename, dl_filename) && dl_status != NET_FILE_DOWNLOAD_FINISHED)
		return true;

	if (strlen(filename) + 1 > NET_FILE_NAME_SIZE) return false;
	strlcpy(dl_filename, filename, sizeof(dl_filename));
	dl_status = NET_FILE_DOWNLOAD_BEGIN;
	dl_requested_count = 0;
	dl_requested_max = 0;
	dl_received = 0;
	dl_received_last_time = dl_start_time = dl_request_last_time = realtime;
	dl_success = false;
	Net_File_Client_Request(filename);
	return true;
}

static void Net_File_Client_Request_Chunk(struct chunk_request *req)
{
	unsigned char packet[7];
	unsigned int chunk = req->pos;
	req->time = realtime;
	packet[0] = 'F';
	packet[1] = 'C';
	packet[2] = dl_id;
	packet[3] = (chunk >> 24) & 0xFF;
	packet[4] = (chunk >> 16) & 0xFF;
	packet[5] = (chunk >> 8) & 0xFF;
	packet[6] = chunk & 0xFF;
	NetConn_Write(cls.connect_mysocket, packet, sizeof(packet), &cls.connect_address);
}

void Net_File_Client_Frame(void)
{
	int i;
	if (dl_status != NET_FILE_DOWNLOAD_FINISHED)
	{
		if (cls.state != ca_connected)
		{
			Con_Printf("Downloading of %s aborted because of lost connection\n", dl_filename);
			Net_File_Client_Stop();
		}
		if (dl_received_last_time + 2 < realtime)
		{
			Con_Printf("Downloading of %s aborted because of timeout\n", dl_filename);
			Net_File_Client_Stop();
		}
	}
	if (dl_status == NET_FILE_DOWNLOAD_FINISHED) return;
	if (dl_status == NET_FILE_DOWNLOAD_BEGIN)
	{
		if (dl_request_last_time + 0.5 < realtime)
		{
			Net_File_Client_Request(dl_filename);
			dl_request_last_time = realtime;
		}
	}
	else if (dl_status == NET_FILE_DOWNLOAD_ACTIVE)
	{
		while (dl_request_last_time + 0.002 < realtime)
		{
			dl_request_last_time += 0.002;
			if (dl_requested_max < dl_received + NET_FILE_CLIENT_REQUESTS_COUNT && dl_requested_max < dl_chunks)
			{
				dl_requested[dl_requested_count].pos = dl_requested_max;
				dl_requested[dl_requested_count].done = false;
				Net_File_Client_Request_Chunk(&dl_requested[dl_requested_count]);
				dl_requested_count++;
				dl_requested_max++;
				continue;
			}
			for (i = 0; i < NET_FILE_CLIENT_REQUESTS_COUNT; i++)
			{
				if (dl_requested[i].time + 0.5 < realtime)
					Net_File_Client_Request_Chunk(&dl_requested[i]);
			}
		}
	}
}

qboolean Net_File_Client_Active(void)
{
	return dl_status != NET_FILE_DOWNLOAD_FINISHED;
}

qboolean Net_File_Client_Success(void)
{
	return dl_success;
}

float Net_File_Client_Progress(void)
{
	return ((float)dl_received / (float)dl_chunks);
}

float Net_File_Client_Speed(void)
{
	if (realtime <= dl_start_time) return 0;
	return (dl_received * NET_FILE_CHUNK_SIZE) / (realtime - dl_start_time);
}

void Net_File_Client_Stop(void)
{
	if (dl_status == NET_FILE_DOWNLOAD_FINISHED) return;
	if (dl_file)
	{
		FS_Close(dl_file);
		dl_file = NULL;
		unlink(dl_filename);
	}
	dl_status = NET_FILE_DOWNLOAD_FINISHED;
	dl_success = false;
	dl_filename[0] = '\0';
}
