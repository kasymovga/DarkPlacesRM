#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "quakedef.h"
#include "cgf_private.h"

#ifdef DEBUG
#include <assert.h>
#include <stdio.h>
static const char* (*sqlite3_errstr)(int);
static inline void log_sqlite_error(int status) {
    const char* tmp = sqlite3_errstr(status);
    Con_DPrintf("cgf: db error: %s\n", tmp);
}
#else
static inline void log_sqlite_error(int _) { (void)(_); }
#endif

#include "sha256.h"

/*
 * BEGIN Partial liblzma defs for dynamic loading
 */

dllhandle_t lzma_dll = NULL;

typedef enum {
    LZMA_OK                 = 0,
    LZMA_STREAM_END         = 1,
    LZMA_NO_CHECK           = 2,
    LZMA_UNSUPPORTED_CHECK  = 3,
    LZMA_GET_CHECK          = 4,
    LZMA_MEM_ERROR          = 5,
    LZMA_MEMLIMIT_ERROR     = 6,
    LZMA_FORMAT_ERROR       = 7,
    LZMA_OPTIONS_ERROR      = 8,
    LZMA_DATA_ERROR         = 9,
    LZMA_BUF_ERROR          = 10,
    LZMA_PROG_ERROR         = 11,
} lzma_ret;

static lzma_ret (*lzma_stream_buffer_decode)(
        uint64_t *memlimit, uint32_t flags,
        const void *allocator,
        const uint8_t *in, size_t *in_pos, size_t in_size,
        uint8_t *out, size_t *out_pos, size_t out_size);

static dllfunction_t lzma_funcs[] = {
    {"lzma_stream_buffer_decode", (void**) &lzma_stream_buffer_decode},
    {NULL, NULL}
};

static bool LZMA_OpenLibrary(void) {
    const char *dllnames[] = {
#if WIN32
        "liblzma.dll",
        "liblzma-5.dll",
#elif defined(MACOSX)
        "liblzma.dylib",
        "liblzma.5.dylib",
#else
        "liblzma.so",
        "liblzma.so.5",
        "liblzma.so.5.2.1",
#endif
        NULL
    };

    if(lzma_dll)
        return true;

    return Sys_LoadLibrary(dllnames, &lzma_dll, lzma_funcs);
}

/*
 * END Partial liblzma defs for dynamic loading
 */

/*
 * BEGIN Partial libsqlite3 defs for dynamic loading
 */

dllhandle_t sqlite_dll = NULL;

static int (*sqlite3_bind_text)(sqlite3_stmt*, int, const char*, int, void(*)(void*));
static int (*sqlite3_clear_bindings)(sqlite3_stmt*);
static int (*sqlite3_close)(sqlite3*);
static const void* (*sqlite3_column_blob)(sqlite3_stmt*, int iCol);
static int (*sqlite3_column_bytes)(sqlite3_stmt*, int iCol);
static sqlite3_int64 (*sqlite3_column_int64)(sqlite3_stmt*, int iCol);
static const unsigned char* (*sqlite3_column_text)(sqlite3_stmt*, int iCol);
static const char* (*sqlite3_errmsg)(sqlite3*);
static int (*sqlite3_finalize)(sqlite3_stmt *pStmt);
static int (*sqlite3_initialize)(void);
static int (*sqlite3_open_v2)(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
static int (*sqlite3_prepare_v2)(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
static int (*sqlite3_reset)(sqlite3_stmt *pStmt);
static int (*sqlite3_step)(sqlite3_stmt*);

static dllfunction_t sqlite_funcs[] = {
    {"sqlite3_bind_text", (void**) &sqlite3_bind_text},
    {"sqlite3_clear_bindings", (void**) &sqlite3_clear_bindings},
    {"sqlite3_close", (void**) &sqlite3_close},
    {"sqlite3_column_blob", (void**) &sqlite3_column_blob},
    {"sqlite3_column_bytes", (void**) &sqlite3_column_bytes},
    {"sqlite3_column_int64", (void**) &sqlite3_column_int64},
    {"sqlite3_column_text", (void**) &sqlite3_column_text},
    {"sqlite3_errmsg", (void**) &sqlite3_errmsg},
    {"sqlite3_finalize", (void**) &sqlite3_finalize},
    {"sqlite3_initialize", (void**) &sqlite3_initialize},
    {"sqlite3_open_v2", (void**) &sqlite3_open_v2},
    {"sqlite3_prepare_v2", (void**) &sqlite3_prepare_v2},
    {"sqlite3_reset", (void**) &sqlite3_reset},
    {"sqlite3_step", (void**) &sqlite3_step},
#ifdef DEBUG
    {"sqlite3_errstr", (void**) &sqlite3_errstr},
#endif
    {NULL, NULL}
};

static bool SQLite_OpenLibrary(void) {
    const char *dllnames[] = {
#if WIN32
        "libsqlite3.dll",
#elif defined(MACOSX)
        "libsqlite3.dylib",
        "libsqlite3.0.dylib",
        "libsqlite3.0.8.6.dylib",
#else
        "libsqlite3.so",
        "libsqlite3.so.0",
        "libsqlite3.so.0.8.6",
#endif
        NULL
    };

    if(sqlite_dll)
        return true;

    return Sys_LoadLibrary(dllnames, &sqlite_dll, sqlite_funcs);
}

/*
 * END Partial libsqlite3 defs for dynamic loading
 */

void AssetArchive_init(void) {
    LZMA_OpenLibrary();
    SQLite_OpenLibrary();
}

bool AssetArchive_check_hash(const uint8_t* data, const size_t dlen, const uint8_t* chash) {
    SHA256Context ctx;
    uint8_t digest[CGF_HASH_SIZE];

    if(shaSuccess != SHA256Reset(&ctx)) {
        return false;
    }

    if(shaSuccess != SHA256Input(&ctx, data, dlen)) {
        return false;
    }

    if(shaSuccess != SHA256FinalBits(&ctx, 0, 0)) {
        return false;
    }

    if(shaSuccess != SHA256Result(&ctx, digest)) {
        return false;
    }

    return (0 == memcmp(digest, chash, CGF_HASH_SIZE));
}


bool AssetArchive_openRead(struct AssetArchive** aa, const char* const filename) {
    int res;
    struct AssetArchive* ret;

    if(!sqlite_dll || !aa || !filename) {
        return false;
    }

    ret = calloc(1, sizeof(struct AssetArchive));
    if(NULL == ret) {
        return false;
    }

    res = sqlite3_initialize();
    if(SQLITE_OK != res) {
        log_sqlite_error(res);
        free(ret);
        return false;
    }

    res = sqlite3_open_v2(filename, &ret->db, SQLITE_OPEN_READONLY, NULL);
    if(SQLITE_OK != res) {
        log_sqlite_error(res);
        free(ret);
        return false;
    }

    ret->decomp_buf = calloc(CGF_MAX_CHUNK_SIZE, 1);
    if(!ret->decomp_buf) {
        sqlite3_close(ret->db);
        return false;
    }

    *aa = ret;

    return true;
}

void AssetArchive_close(struct AssetArchive* aa) {
    if(aa && sqlite_dll) {
        if(aa->db) {
            sqlite3_close(aa->db);
        }
        if(aa->decomp_buf) {
            free(aa->decomp_buf);
        }
        free(aa);
    }
}

static const char* COUNT_ASSETS_QUERY = \
    "SELECT count(*) FROM files;";
static const char* COUNT_FILES_QUERY = \
    "SELECT count(*) FROM files WHERE data<>\"\";";
static const char* COUNT_ALIAS_QUERY = \
    "SELECT count(*) FROM files WHERE alias<>\"\";";

static inline int64_t runHardcodedOneInt64ResultQuery(struct AssetArchive* a, const char* query) {
    sqlite3_stmt* pq;
    int64_t ret;
    int res;

    res = sqlite3_prepare_v2(a->db, query, -1, &pq, NULL);
    if(SQLITE_OK != res) {
        return -1;
    }

    res = sqlite3_step(pq);
    if(SQLITE_ROW != res) {
        return -1;
    }

    ret = sqlite3_column_int64(pq, 0);
    sqlite3_finalize(pq);

    return ret;
}

//stat functions
int64_t AssetArchive_countAssets(struct AssetArchive* a) {
    return runHardcodedOneInt64ResultQuery(a, COUNT_ASSETS_QUERY);
}

int64_t AssetArchive_countFiles(struct AssetArchive* a) {
    return runHardcodedOneInt64ResultQuery(a, COUNT_FILES_QUERY);
}

int64_t AssetArchive_countAliases(struct AssetArchive* a) {
    return runHardcodedOneInt64ResultQuery(a, COUNT_ALIAS_QUERY);
}

static inline bool decompress(struct AssetArchive* a, uint8_t** decompdata, size_t* dlen, const uint8_t* compdata, const size_t complen);

static const char* GET_QUERY = "SELECT alias, data, hash FROM files WHERE name = ?";

static inline bool real_LoadMany(struct AssetArchive* a, uint8_t* data[],
                                 size_t* dlens, const char* filenames[],
                                 const size_t count, bool resolve) {
    int res;
    sqlite3_stmt* pq;
    size_t i;
    const char* aptr;
    size_t alen;
    const uint8_t* dptr;
    size_t dlen;
    const uint8_t* hptr;

    if(!sqlite_dll) {
        Con_Printf("cgf: libsqlite3 not loaded\n");
        return false;
    }

    if(!a || !data || !filenames || !count) {
        Con_Printf("cgf: bad req: %p %p %p %p %lu\n", a, data, dlens, filenames, count);
        return false;
    }

    res = sqlite3_prepare_v2(a->db, GET_QUERY, -1, &pq, NULL);
    if(SQLITE_OK != res) {
        log_sqlite_error(res);
        return false;
    }

    for(i=0; i<count; ++i) {
        dlens[i] = 0;
    }

    for(i=0; i<count; ++i) {
        res = sqlite3_bind_text(pq, 1, filenames[i], -1, SQLITE_STATIC);
        if(SQLITE_OK != res) {
            log_sqlite_error(res);
            sqlite3_finalize(pq);
            return false;
        }

        res = sqlite3_step(pq);
        if(SQLITE_ROW != res) {
            log_sqlite_error(res);
            sqlite3_finalize(pq);
            return false;
        }

        aptr = (const char*)sqlite3_column_text(pq, 0);
        alen = sqlite3_column_bytes(pq, 0);
        if(NULL != aptr && alen>0 && resolve) {
            //Alias resolution
            char* name = calloc(alen + 1, 1);
            memcpy(name, aptr, alen);

            res = sqlite3_reset(pq);
            if(SQLITE_OK != res) {
                free(name);
                log_sqlite_error(res);
                sqlite3_finalize(pq);
                return false;
            }

            res = sqlite3_clear_bindings(pq);
            if(SQLITE_OK != res) {
                free(name);
                log_sqlite_error(res);
                sqlite3_finalize(pq);
                return false;
            }

            res = sqlite3_bind_text(pq, 1, name, -1, SQLITE_TRANSIENT);
            if(SQLITE_OK != res) {
                free(name);
                log_sqlite_error(res);
                sqlite3_finalize(pq);
                return false;
            }

            res = sqlite3_step(pq);
            if(SQLITE_ROW != res) {
                free(name);
                log_sqlite_error(res);
                sqlite3_finalize(pq);
                return false;
            }

            free(name);
        }

        //copy the data of the resolved name/alias
        dptr = sqlite3_column_blob(pq, 1);
        dlen = sqlite3_column_bytes(pq, 1);
        hptr = sqlite3_column_blob(pq, 2);

        if(NULL!=dptr && dlen > 0 && sqlite3_column_bytes(pq, 2) >= 32 &&
                AssetArchive_check_hash(dptr, dlen, hptr)) {
            //we've got to copy the data as the references will go poof
            //upon the next iteration -- a pity.
            uint8_t* tmp;
            size_t tlen;
            bool success = decompress(a, &tmp, &tlen, dptr, dlen);
            if(success) {
                data[i] = tmp;
                dlens[i] = tlen;
            } else {
                Con_Printf("cgf: failed to decompress '%s'\n", filenames[i]);
            }
        }

        res = sqlite3_reset(pq);
        if(SQLITE_OK != res) {
            log_sqlite_error(res);
            sqlite3_finalize(pq);
            return false;
        }
        res = sqlite3_clear_bindings(pq);
        if(SQLITE_OK != res) {
            log_sqlite_error(res);
            sqlite3_finalize(pq);
            return false;
        }
    }

    sqlite3_finalize(pq);
    return true;
}

//read functions
bool AssetArchive_loadMany(struct AssetArchive* a, uint8_t* data[], size_t* dlens, const char* filenames[], const size_t count) {
    return real_LoadMany(a, data, dlens, filenames, count, true);
}

bool AssetArchive_loadOne(struct AssetArchive* a, uint8_t** data, size_t* dlen, const char* filename) {
    size_t len[2] = {0, 0};
    const bool res = AssetArchive_loadMany(a, data, len, &filename, 1);
    *dlen = len[0];
    return res;
}

static inline size_t smin(const size_t x, const size_t y) {
    return (x<=y) ? x : y;
}

static inline bool decompress(struct AssetArchive* a, uint8_t** decompdata, size_t* dlen, const uint8_t* compdata, const size_t complen) {
    lzma_ret ret;
    uint8_t* buf = a->decomp_buf;
    size_t inpos=0;
    size_t bufpos=0;
    uint64_t memuse = UINT32_MAX;
    uint8_t* res;

    if(!lzma_dll || !buf || !compdata) {
        return false;
    }

    ret = lzma_stream_buffer_decode(&memuse, 0, NULL, compdata, &inpos, complen, buf, &bufpos, CGF_MAX_CHUNK_SIZE);

    if(LZMA_OK != ret && LZMA_NO_CHECK != ret) {
        return false;
    }

    bufpos = smin(CGF_MAX_CHUNK_SIZE, bufpos);

    res = calloc(bufpos, 1);
    if(!res) {
        return false;
    }

    memcpy(res, buf, bufpos);
    *decompdata = res;
    *dlen = bufpos;

    return true;
}

static const char* listquery = "SELECT name FROM files ORDER BY name ASC";

void CGF_BuildFileList(pack_t* pack, struct AssetArchive* ap)
{
    sqlite3_stmt* pq;
    int res;
    const unsigned char* nptr;
    char tmpname[MAX_QPATH+1];
    size_t len;
    const char* eptr;

    if(!ap || !pack) {
        if(ap) {
            eptr = sqlite3_errmsg(ap->db);
            Con_DPrintf("add cgf pack: 1: sqlite result = \"%s\"\n", eptr);
        }
        return;
    }

    res = sqlite3_prepare_v2(ap->db, listquery, -1, &pq, NULL);
    if(SQLITE_OK != res) {
        eptr = sqlite3_errmsg(ap->db);
        Con_DPrintf("add cgf pack: 2: sqlite result = \"%s\"\n", eptr);
        return;
    }

    for(;;) {
        memset(tmpname, '\0', MAX_QPATH);

        res = sqlite3_step(pq);
        if(SQLITE_DONE == res) {
            break;
        }
        else if(SQLITE_ROW != res) {
            eptr = sqlite3_errmsg(ap->db);
            Con_DPrintf("add cgf pack: 3: sqlite result = \"%s\"\n", eptr);
            break;
        }
        nptr = sqlite3_column_text(pq, 0);
        len = strlen((const char*)(nptr));
        memcpy(tmpname, nptr, MAX_QPATH);
        if(len<MAX_QPATH) {
            tmpname[len] = '\0';
        } else {
            tmpname[MAX_QPATH] = '\0';
        }

        Con_DPrintf("Adding \"%s\"\n", tmpname);

        FS_AddFileToPack(tmpname, pack, -1, -1, -1, PACKFILE_FLAG_CGF);
    }

    sqlite3_finalize(pq);
}

