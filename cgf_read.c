#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sqlite3.h>

#include "cgf_private.h"

#include "quakedef.h"

#ifdef DEBUG
#include <assert.h>
#include <stdio.h>
static inline void log_sqlite_error(int status) {
    const char* tmp = sqlite3_errstr(status);
    Con_DPrintf("cgf: db error: %s\n", tmp);
}
#else
static inline void log_sqlite_error(int _) { (void)(_); }
#endif

#include "sha256.h"

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

void AssetArchive_init(void) {
    LZMA_OpenLibrary();
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

    if(!aa || !filename) {
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
    if(aa) {
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
