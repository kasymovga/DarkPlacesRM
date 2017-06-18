#include "quakedef.h"
#include "netconn.h"
#include "net_httpserver.h"
#include "fs.h"

#ifdef USE_LIBMICROHTTPD
#include <string.h>
#include <microhttpd.h>
cvar_t net_http_server = {0, "net_http_server","1", "Internal http server"};
cvar_t net_http_server_host = {0, "net_http_server_host","", "External server address"};

static struct MHD_Daemon *mhd_daemon;

static ssize_t Net_HttpServer_FileReadCallback(void *cls, uint64_t pos, char *buf, size_t max) {
	qfile_t *file = cls;
	FS_Seek(file, pos, SEEK_SET);
	return FS_Read(file, buf, max);
}

static void Net_HttpServer_FileFreeCallback(void *cls) {
	FS_Close((qfile_t *)cls);
}

int Net_HttpServer_Request(void *cls, struct MHD_Connection *connection,
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
	Con_Printf("Url request: %s\n", url);
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

	pk3_file = FS_OpenRealFile(pk3, "r", false);
	if (!pk3_file) {
		char dlcache_pk3[pk3_len + 9];
		sprintf(dlcache_pk3, "dlcache/%s", pk3);
		pk3_file = FS_OpenRealFile(dlcache_pk3, "r", false);
	}
	if (!pk3_file)
		return MHD_NO;

	response = MHD_create_response_from_callback(FS_FileSize(pk3_file), 32 * 1024, Net_HttpServer_FileReadCallback, pk3_file, Net_HttpServer_FileFreeCallback);
	if (!response) {
		FS_Close(pk3_file);
		return MHD_NO;
	}
	ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
	MHD_destroy_response(response);
	return ret;
}
#endif

static net_http_server_url_data[128];
void Net_HttpServerInit(void)
{
#ifdef USE_LIBMICROHTTPD
	int port, i;
#endif
	net_http_server_url = NULL;
#ifdef USE_LIBMICROHTTPD
	Cvar_RegisterVariable (&net_http_server);
	Cvar_RegisterVariable (&net_http_server_host);
	if (!net_http_server.integer)
		return;

	for (i = 0; i < 3; i++) {
		port = sv_netport.integer + i;
		mhd_daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
		                              Net_HttpServer_Request, NULL, MHD_OPTION_END);
		if (mhd_daemon)
			break;
	}
	if (mhd_daemon) {
		dpsnprintf(net_http_server_url_data, 64, "http://%s:%i/", net_http_server_host.string, port);
		net_http_server_url = net_http_server_url_data;
		Con_Printf("libmicrohttpd listen port %i\n", (int)port);
	} else
		Con_Printf("libmicrohttpd listen failed on port %i\n", (int)port);
#endif
}

void Net_HttpServerShutdown(void)
{
#ifdef USE_LIBMICROHTTPD
	if (mhd_daemon)
		MHD_stop_daemon(mhd_daemon);
#endif
}
