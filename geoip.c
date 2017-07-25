#ifdef __SIZEOF_INT128__
#define GEOIP_ENABLED
#endif
#include "quakedef.h"
#include "geoip.h"

#ifdef GEOIP_ENABLED
#include "maxminddb_defs.h"

#include <errno.h>

static cvar_t geoip_initialized = {CVAR_READONLY, "geoip_initialized", "0", "Indicates that the GeoIP module has been successfully initialized and database loaded"};
static cvar_t geoip_db_file = {CVAR_SAVE, "geoip_db_file", "/usr/share/GeoIP/GeoLite2-Country.mmdb", "Path to the MaxMind GeoIP2 Country database file"};

static mempool_t *geoip_mempool;
static dllhandle_t geoip_dll = NULL;
static MMDB_s *geoip_database;

static int (*MMDB_open)(const char *const filename, uint32_t flags, MMDB_s *const mmdb);
static void (*MMDB_close)(MMDB_s *const mmdb);
static const char* (*MMDB_strerror)(int error_code);
static MMDB_lookup_result_s (*MMDB_lookup_string)(MMDB_s *const mmdb, const char *const ipstr, int *const gai_error, int *const mmdb_error);
static int (*MMDB_get_value)(MMDB_entry_s *const start, MMDB_entry_data_s *const entry_data, ...);

static dllfunction_t geoip_funcs[] = {
    {"MMDB_open",                                  (void**) &MMDB_open},
    {"MMDB_close",                                 (void**) &MMDB_close},
    {"MMDB_lookup_string",                         (void**) &MMDB_lookup_string},
    {"MMDB_get_value",                             (void**) &MMDB_get_value},
    {"MMDB_strerror",                              (void**) &MMDB_strerror},
    {NULL, NULL}
};

static qboolean GeoIP_OpenLibrary(void) {
    const char *dllnames[] = {
#if WIN32
        "libmaxminddb.dll",
#elif defined(MACOSX)
        "libmaxminddb.0.dylib",
        "libmaxminddb.dylib",
#else
        "libmaxminddb.so.0.0.7",
        "libmaxminddb.so.0",
        "libmaxminddb.so",
#endif
        NULL
    };

    if(geoip_dll) {
        return true;
    }

    return Sys_LoadLibrary(dllnames, &geoip_dll, geoip_funcs);
}

static void GeoIP_Printf(const char *fmt, ...) {
    va_list args;
    char msg[MAX_INPUTLINE];

    va_start(args, fmt);
    dpvsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Con_Print("GeoIP: ");
    Con_Print(msg);
}

static qboolean GeoIP_OpenDB(void) {
    int status;

    if(!geoip_dll) {
        return false;
    }

    if(geoip_database) {
        return true;
    }

    geoip_database = Mem_Alloc(geoip_mempool, sizeof(MMDB_s));

    if((status = MMDB_open(geoip_db_file.string, 0, geoip_database)) == MMDB_SUCCESS) {
        Cvar_SetQuick(&geoip_initialized, "1");
        GeoIP_Printf("Loaded database from %s\n", geoip_db_file.string);
        return true;
    }

    if(status == MMDB_IO_ERROR) {
        GeoIP_Printf("Can't open database %s: %s (IO error: %s)\n", geoip_db_file.string, MMDB_strerror(status), strerror(errno));
    } else {
        GeoIP_Printf("Can't open database %s: %s\n", geoip_db_file.string, MMDB_strerror(status));
    }

    Mem_Free(geoip_database);
    geoip_database = NULL;
    Cvar_SetQuick(&geoip_initialized, "0");

    return false;
}

static void GeoIP_CloseDB(void) {
    if(!geoip_database) {
        return;
    }

    MMDB_close(geoip_database);
    Mem_Free(geoip_database);
    geoip_database = NULL;

    Cvar_SetQuick(&geoip_initialized, "0");
}

static void GeoIP_Cmd_Reload_f(void) {
    if(!geoip_dll) {
        GeoIP_Printf("libmaxminddb is not loaded\n");
        return;
    }

    GeoIP_CloseDB();
    GeoIP_OpenDB();
}

static void GeoIP_Cmd_LookUp_f(void) {
    char code[] = "lookup error";

    if(!geoip_dll) {
        GeoIP_Printf("libmaxminddb is not loaded\n");
        return;
    }

    if(Cmd_Argc() < 2) {
        Con_Printf("Usage: geoip_lookup <ip>\n");
        return;
    }

    GeoIP_LookUp(Cmd_Argv(1), code, sizeof(code));
    GeoIP_Printf("%s: %s\n", Cmd_Argv(1), code);
}
#endif //GEOIP_ENABLED

void GeoIP_Init(void) {
#ifdef GEOIP_ENABLED
    geoip_mempool = Mem_AllocPool("geoip", 0, NULL);
    Cvar_RegisterVariable(&geoip_initialized);
    Cvar_RegisterVariable(&geoip_db_file);
    Cmd_AddCommand("geoip_reload", GeoIP_Cmd_Reload_f, "Reloads the GeoIP database");
    Cmd_AddCommand("geoip_lookup", GeoIP_Cmd_LookUp_f, "Resolve an IP address to country code");
    GeoIP_OpenLibrary();
#endif
}

void GeoIP_Shutdown(void) {
#ifdef GEOIP_ENABLED
    GeoIP_CloseDB();
    Mem_FreePool(&geoip_mempool);
#endif
}

qboolean GeoIP_LookUp(const char *const ipstr, char *buf, size_t buf_size) {
#ifdef GEOIP_ENABLED
    int gai_error, mmdb_error;
    MMDB_lookup_result_s result;
    MMDB_entry_data_s data;

    if(!geoip_dll || !GeoIP_OpenDB()) {
        return false;
    }

    result = MMDB_lookup_string(geoip_database, ipstr, &gai_error, &mmdb_error);

    if(gai_error || mmdb_error || !result.found_entry) {
        return false;
    }

    if(MMDB_get_value(&result.entry, &data, "country", "iso_code", NULL) != MMDB_SUCCESS) {
        return false;
    }

    if(buf_size > 3) {
        buf_size = 3;
    }

    strlcpy(buf, data.utf8_string, buf_size);
    return true;
#else
	return false;
#endif
}
