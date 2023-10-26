#include "net_httpserver.h"
#ifdef USE_LIBMICROHTTPD
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#define SOCKET_CLOSE closesocket
#else //WIN32
#define SOCKET_CLOSE close
#define SOCKET int
#endif //WIN32
#include "quakedef.h"
#include "netconn.h"
#include "fs.h"
#include "thread.h"
#include <string.h>
#include <microhttpd.h>
#include <errno.h>
static cvar_t net_http_server_host = {0, "net_http_server_host","", "External server address"};
static cvar_t net_http_server = {0, "net_http_server","1", "Internal http server"};

static struct MHD_Daemon *mhd_daemon;

#ifdef WIN32
static int win_socketpair(SOCKET socks[2])
{
	struct sockaddr_in addr;
	struct sockaddr_in accept_addr;
	SOCKET listener = -1;
	int ret = -1;
	socklen_t addrlen = sizeof(addr);
	socks[0] = socks[1] = INVALID_SOCKET;
	listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET)
		goto finish;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
		goto finish;

	memset(&addr, 0, sizeof(addr));
	if  (getsockname(listener, (struct sockaddr *)&addr, &addrlen) == SOCKET_ERROR)
		goto finish;

	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_family = AF_INET;
	if (listen(listener, 1) == SOCKET_ERROR)
		goto finish;

	socks[0] = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (socks[0] == INVALID_SOCKET)
		goto finish;

	if (connect(socks[0], (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
		goto finish;

	addrlen = sizeof(addr);
	if  (getsockname(socks[0], (struct sockaddr *)&addr, &addrlen) == SOCKET_ERROR)
		goto finish;

	for (;;) {
		socks[1] = accept(listener, NULL, 0);
		if (socks[1] == INVALID_SOCKET)
			goto finish;

		addrlen = sizeof(accept_addr);
		if (getpeername(socks[1], (struct sockaddr*)&accept_addr, &addrlen) == SOCKET_ERROR)
			goto finish;

		if (addr.sin_addr.s_addr == accept_addr.sin_addr.s_addr && addr.sin_port == accept_addr.sin_port)
			break;

		closesocket(socks[1]);
	}
	ret = 0;
finish:
	if (ret == -1) {
		closesocket(listener);
		if (socks[0] != INVALID_SOCKET)
			closesocket(socks[0]);

		if (socks[1] != INVALID_SOCKET)
			closesocket(socks[1]);
	}
	closesocket(listener);
    return ret;
}
#endif //WIN32
static ssize_t Net_HttpServer_FileReadCallback(void *cls, uint64_t pos, char *buf, size_t max) {
	qfile_t *file = cls;
	FS_Seek(file, pos, SEEK_SET);
	return FS_Read(file, buf, max);
}

static void Net_HttpServer_FileFreeCallback(void *cls) {
	Con_DPrintf("HTTP response %li finished\n", (long int)cls);
	FS_Close((qfile_t *)cls);
}

static enum MHD_Result Net_HttpServer_Request(void *cls, struct MHD_Connection *connection,
		const char *url,
		const char *method, const char *version,
		const char *upload_data,
		size_t *upload_data_size, void **con_cls)
{
	struct MHD_Response *response;
	int ret;
	int pk3_len;
	const char *pk3;
	qfile_t *pk3_file;
	Con_DPrintf("HTTP request: %s\n", url);
	if (!*url)
		return MHD_NO;

	if (strcmp(method, "GET"))
		return MHD_NO;

	pk3 = &url[1];

	if (strchr(pk3, '/') || strchr(pk3, '\\'))
		return MHD_NO;

	pk3_len = strlen(pk3);
	if (pk3_len < 4)
		return MHD_NO;

	if (strcasecmp(&pk3[pk3_len - 4], ".pk3"))
		return MHD_NO;

	pk3_file = FS_OpenVirtualFile(pk3, false);
	if (!pk3_file) {
		char dlcache_pk3[pk3_len + 9];
		sprintf(dlcache_pk3, "dlcache/%s", pk3);
		pk3_file = FS_OpenVirtualFile(dlcache_pk3, false);
	}
	if (!pk3_file)
		return MHD_NO;

	response = MHD_create_response_from_callback(FS_FileSize(pk3_file), 32 * 1024, Net_HttpServer_FileReadCallback, pk3_file, Net_HttpServer_FileFreeCallback);
	Con_DPrintf("HTTP response %li for %s started\n", (long int)pk3_file, url);
	if (!response) {
		FS_Close(pk3_file);
		Con_Printf("libmicrohttpd response failed\n");
		return MHD_NO;
	}
	ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
	MHD_destroy_response(response);
	return ret;
}

SOCKET control_sock[2];
static int Net_HttpServer_Thread(void *daemon) {
	fd_set rs;
	fd_set ws;
	fd_set es;
	MHD_socket max;
	MHD_UNSIGNED_LONG_LONG mhd_timeout;
	struct timeval tv;
	for(;;) {
		FD_ZERO (&rs);
		FD_ZERO (&ws);
		FD_ZERO (&es);
		FD_SET(control_sock[0], &rs);
		if (MHD_YES != MHD_get_fdset(daemon, &rs, &ws, &es, &max))
			break;

		if (MHD_get_timeout(daemon, &mhd_timeout) == MHD_YES) {
			tv.tv_sec = mhd_timeout / 1000LL;
			tv.tv_usec = (mhd_timeout - (tv.tv_sec * 1000LL)) * 1000LL;
			if (select(max + 1, &rs, &ws, &es, &tv) < 0) {
				if (errno == EINTR) continue;
				Con_Printf("libmicrohttpd thread: select() return -1\n");
				break;
			}
		} else if (select(max + 1, &rs, &ws, &es, NULL) < 0) {
			if (errno == EINTR) continue;
			Con_Printf("libmicrohttpd thread: select() return -1\n");
			break;
		}
		if (FD_ISSET(control_sock[0], &rs))
			break;

		if (MHD_run(daemon) == MHD_NO)
			break;
	}
	SOCKET_CLOSE(control_sock[0]);
	SOCKET_CLOSE(control_sock[1]);

	MHD_stop_daemon(mhd_daemon);
	Con_Printf("libmicrohttpd thread finished\n");
	return 0;
}

static void *mhd_thread;
static int net_http_server_port;
static char net_http_server_url_data[128];
#endif //USE_LIBMICROHTTPD

void Net_HttpServerInit(void)
{
#ifdef USE_LIBMICROHTTPD
	Cvar_RegisterVariable (&net_http_server);
	Cvar_RegisterVariable (&net_http_server_host);
#endif //USE_LIBMICROHTTPD
}

void Net_HttpServerStart(void)
{
#ifdef USE_LIBMICROHTTPD
	int i;
	if (mhd_daemon && mhd_thread) return;
	if (!Thread_HasThreads()) {
		Con_Printf("No thread support, http server disabled\n");
		return;
	}
	if (!net_http_server.integer)
		return;

#ifdef WIN32
	if (win_socketpair(control_sock))
#else //WIN32
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, control_sock) < 0)
#endif //WIN32
		return;

	for (i = 0; i < 3; i++) {
		net_http_server_port = sv_netport.integer + i;
		mhd_daemon = MHD_start_daemon(MHD_USE_AUTO, net_http_server_port, NULL, NULL,
		                              Net_HttpServer_Request, NULL, MHD_OPTION_END);
		if (mhd_daemon)
			break;

		Con_Printf("libmicrohttpd listen failed on port %i\n", (int)net_http_server_port);
	}
	if (mhd_daemon) {
		mhd_thread = Thread_CreateThread(Net_HttpServer_Thread, mhd_daemon);
		Con_Printf("libmicrohttpd listen port %i\n", (int)net_http_server_port);
	} else {
		SOCKET_CLOSE(control_sock[0]);
		SOCKET_CLOSE(control_sock[1]);
	}
#endif //USE_LIBMICROHTTPD
}

const char *Net_HttpServerUrl(void) {
#ifdef USE_LIBMICROHTTPD
	if (mhd_daemon) {
		Cvar_LockThreadMutex();
		dpsnprintf(net_http_server_url_data, 64, "http://%s:%i/", net_http_server_host.string, net_http_server_port);
		Cvar_UnlockThreadMutex();
		return net_http_server_url_data;
	}
#endif //USE_LIBMICROHTTPD
	return "";
}

void Net_HttpServerShutdown(void)
{
#ifdef USE_LIBMICROHTTPD
	if (mhd_daemon)
		send(control_sock[1], "", 1, 0);

	if (mhd_thread)
		Thread_WaitThread(mhd_thread, 0);

	mhd_daemon = NULL;
	mhd_thread = NULL;
#endif //USE_LIBMICROHTTPD
}
