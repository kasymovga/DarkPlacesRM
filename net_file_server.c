#include "quakedef.h"
#include "thread.h"
#include "fs.h"
#include "net_file_server.h"

#define NET_FILE_SESSIONS_MAX 256
#define NET_FILE_SERVER_BUFFER_SIZE 1024*1024*16

static void *net_file_server_thread;
static void *net_file_server_mutex;
static volatile qboolean net_file_server_thread_terminate;
static unsigned char *net_file_server_buffer;
static volatile int net_file_server_buffer_length;
static double net_file_server_terminate_time;
struct net_file_server_buffer_header
{
	unsigned short length;
	lhnetsocket_t *mysocket;
	lhnetaddress_t peeraddress;
};

struct download_session
{
	qfile_t *file;
	char name[NET_FILE_NAME_SIZE];
	unsigned char id;
	lhnetsocket_t *mysocket;
	lhnetaddress_t peeraddress;
	double time;
};

static struct download_session *sessions;
static int sessions_count;

static void Net_File_Server_Session_Finish(struct download_session *session)
{
	if (session->file)
	{
		FS_Close(session->file);
		session->file = NULL;
	}
	session->name[0] = '\0';
	session->mysocket = NULL;
	memset(&session->peeraddress, 0, sizeof(session->peeraddress));
}

static qboolean Net_File_Server_Session_Start(struct download_session *session)
{
	const char *ext = FS_FileExtension(session->name);
	if (
			strcasecmp(ext, "pk3") &&
			strcasecmp(ext, "pak") &&
			strcasecmp(ext, "kpf") &&
			strcasecmp(ext, "obb")
	)
	{
		Con_Printf("Net_File_Server_Session_Start: requested not pak file (%s), aborted\n", session->name);
		return false;
	}
	if (session->file)
		FS_Close(session->file);
	session->file = FS_OpenVirtualFile(session->name, false);
	if (!session->file)
	{
		char dlcache_name[strlen(session->name) + 9];
		sprintf(dlcache_name, "dlcache/%s", session->name);
		session->file = FS_OpenVirtualFile(dlcache_name, false);
	}
	if (!session->file)
	{
		Con_Printf("Net_File_Server_Session_Start: cannot open file %s\n", session->name);
		return false;
	}
	return true;
}

static void Net_File_Server_Session_Remove(int num)
{
	Net_File_Server_Session_Finish(&sessions[num]);
	memmove(&sessions[num], &sessions[num + 1], sizeof(struct download_session) * (sessions_count - num));
	sessions_count--;
}

static void Net_File_Server_Packet_Process(lhnetsocket_t *mysocket, lhnetaddress_t *peeraddress, unsigned char *packet, int len)
{
	struct download_session *session = NULL;
	int sn;
	unsigned int chunk_count;
	char answer[NET_FILE_CHUNK_SIZE + 32];
	int i;
	static unsigned char id_static;
	if (packet[0] == 'R') //request file
	{
		if (len > NET_FILE_NAME_SIZE) return;
		for (i = 0; i < sessions_count; i++)
		{
			if (!LHNETADDRESS_Compare(&sessions[i].peeraddress, peeraddress))
			{
				session = &sessions[i];
				sn = i;
				break;
			}
		}
		if (!session)
		{
			if (sessions_count == NET_FILE_SESSIONS_MAX)
			{
				Con_Printf("Net_File_Server_Packet: not enough session slots\n");
				return;
			}
			sn = sessions_count;
			session = &sessions[sessions_count];
			sessions_count++;
		}
		if (strlen(session->name) != (unsigned int)(len - 1) || strncmp(session->name, (const char *)&packet[1], len - 1))
		{
			id_static++;
			session->id = id_static;
			memcpy(session->name, &packet[1], len - 1);
			session->name[len - 1] = '\0';
			session->mysocket = mysocket;
			session->peeraddress = *peeraddress;
			if (!Net_File_Server_Session_Start(session))
			{
				Net_File_Server_Session_Remove(sn);
				Con_Printf("Net_File_Server_Packet: cannot start session\n");
				return;
			}
		}
		answer[0] = 'F';
		answer[1] = 'R';
		answer[2] = session->id;
		chunk_count = FS_FileSize(session->file) / NET_FILE_CHUNK_SIZE;
		if (FS_FileSize(session->file) % NET_FILE_CHUNK_SIZE) chunk_count++;
		answer[3] = (chunk_count >> 24) & 0xFF;
		answer[4] = (chunk_count >> 16) & 0xFF;
		answer[5] = (chunk_count >> 8) & 0xFF;
		answer[6] = chunk_count & 0xFF;
		memcpy(&answer[7], &packet[1], len - 1);
		NetConn_Write(session->mysocket, answer, len + 6, &session->peeraddress); //len - 1 + 7 = len - 6
	}
	else if (packet[0] == 'C') // request chunk
	{
		unsigned char id = packet[1];
		long int file_pos;
		unsigned int chunk_pos;
		int chunk_size;
		if (len != 6)
		{
			Con_Printf("Net_File_Server_Packet: wrong packet size\n");
			return;
		}
		for (i = 0; i < sessions_count; i++)
		{
			if (!LHNETADDRESS_Compare(&sessions[i].peeraddress, peeraddress) && sessions[i].id == id)
			{
				session = &sessions[i];
				sn = i;
				break;
			}
		}
		if (!session)
		{
			Con_Printf("Net_File_Server_Packet: session not found\n");
			return;
		}
		chunk_pos = (packet[2] << 24) + (packet[3] << 16) + (packet[4] << 8) + packet[5];
		file_pos = chunk_pos * NET_FILE_CHUNK_SIZE;
		if (file_pos > FS_FileSize(session->file))
		{
			Con_Printf("Net_File_Server_Packet: wrong chunk position\n");
			return;
		}
		if (FS_Tell(session->file) != file_pos)
			FS_Seek(session->file, file_pos, SEEK_SET);
		if (FS_Tell(session->file) != file_pos) return;
		answer[0] = 'F';
		answer[1] = 'C';
		answer[2] = id;
		answer[3] = (chunk_pos >> 24) & 0xFF;
		answer[4] = (chunk_pos >> 16) & 0xFF;
		answer[5] = (chunk_pos >> 8) & 0xFF;
		answer[6] = chunk_pos & 0xFF;
		chunk_size = FS_Read(session->file, &answer[7], NET_FILE_CHUNK_SIZE);
		if (chunk_size > NET_FILE_CHUNK_SIZE || chunk_size <= 0) return;
		NetConn_Write(session->mysocket, answer, chunk_size + 7, &session->peeraddress);
	}
	else if (packet[0] == 'E') // session end
	{
		for (i = 0; i < sessions_count; i++)
		{
			if (!LHNETADDRESS_Compare(&sessions[i].peeraddress, peeraddress))
			{
				session = &sessions[i];
				sn = i;
				break;
			}
		}
		if (session && session->id == packet[1])
		{
			Net_File_Server_Session_Finish(session);
			Net_File_Server_Session_Remove(sn);
		}
	}
	if (session) session->time = Sys_DirtyTime() + 2;
	for (i = 0; i < sessions_count; i++)
	{
		if (sessions[i].time < realtime)
		{
			Net_File_Server_Session_Finish(&sessions[i]);
			Net_File_Server_Session_Remove(i);
		}
	}
}

static int Net_File_Server_Thread(void *data)
{
	unsigned char packet[512];
	struct net_file_server_buffer_header header;
	for (;;)
	{
		Thread_LockMutex(net_file_server_mutex);
		if (net_file_server_thread_terminate)
		{
			Thread_UnlockMutex(net_file_server_mutex);
			break;
		}
		while (net_file_server_buffer_length > 0)
		{
			memcpy(&header, net_file_server_buffer, sizeof(header));
			memcpy(packet, net_file_server_buffer + sizeof(header), header.length);
			memmove(net_file_server_buffer, net_file_server_buffer + sizeof(header) + header.length, net_file_server_buffer_length - sizeof(header) - header.length);
			net_file_server_buffer_length -= (sizeof(header) + header.length);
			Thread_UnlockMutex(net_file_server_mutex);
			Net_File_Server_Packet_Process(header.mysocket, &header.peeraddress, packet, header.length);
			Thread_LockMutex(net_file_server_mutex);
		}
		Thread_UnlockMutex(net_file_server_mutex);
		Sys_Sleep(1000);
	}
	return 0;
}

void Net_File_Server_Packet(client_t *client, unsigned char *packet, int len)
{
	net_file_server_terminate_time = realtime + 2;
	if (!sessions)
	{
		sessions = Z_Malloc(sizeof(struct download_session) * NET_FILE_SESSIONS_MAX);
	}
	if (!Thread_HasThreads())
	{
		Net_File_Server_Packet_Process(client->netconnection->mysocket, &client->netconnection->peeraddress, packet, len);
		return;
	}
	if (!net_file_server_thread)
	{
		net_file_server_thread_terminate = false;
		net_file_server_buffer = Z_Malloc(NET_FILE_SERVER_BUFFER_SIZE);
		net_file_server_mutex = Thread_CreateMutex();
		net_file_server_thread = Thread_CreateThread(Net_File_Server_Thread, NULL);
	}
	Thread_LockMutex(net_file_server_mutex);
	if (len + net_file_server_buffer_length + sizeof(struct net_file_server_buffer_header) <= NET_FILE_SERVER_BUFFER_SIZE)
	{
		struct net_file_server_buffer_header header;
		header.length = len;
		header.mysocket = client->netconnection->mysocket;
		header.peeraddress = client->netconnection->peeraddress;
		memcpy(&net_file_server_buffer[net_file_server_buffer_length], &header, sizeof(header));
		net_file_server_buffer_length += sizeof(header);
		memcpy(&net_file_server_buffer[net_file_server_buffer_length], packet, len);
		net_file_server_buffer_length += len;
	}
	Thread_UnlockMutex(net_file_server_mutex);
}

void Net_File_Server_Shutdown(void)
{
	int i;
	if (net_file_server_thread)
	{
		Thread_LockMutex(net_file_server_mutex);
		net_file_server_thread_terminate = true;
		Thread_UnlockMutex(net_file_server_mutex);
		Thread_WaitThread(net_file_server_thread, 0);
		net_file_server_thread = NULL;
		Thread_DestroyMutex(net_file_server_mutex);
		net_file_server_mutex = NULL;
		Z_Free(net_file_server_buffer);
		net_file_server_buffer = NULL;
		net_file_server_buffer_length = 0;
	}
	if (sessions)
	{
		for (i = 0; i < sessions_count; i++)
		{
			Net_File_Server_Session_Remove(i);
		}
		Z_Free(sessions);
		sessions = NULL;
		sessions_count = 0;
	}
}

void Net_File_Server_Frame(void)
{
	if (sessions && net_file_server_terminate_time < realtime)
		Net_File_Server_Shutdown();
}
