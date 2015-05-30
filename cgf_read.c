#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sqlite3.h>
#include <lzma.h>

#include "cgf_private.h"

#ifdef DEBUG
#include <assert.h>
#include <stdio.h>
static inline void log_sqlite_error(int status) {
    const char* tmp = sqlite3_errstr(status);
    fprintf(stderr, "%s\n", tmp);
    fflush(stderr);
}
#else
static inline void log_sqlite_error(int _) { (void)(_); }
#endif

#include "sha256.h"

bool AssetArchive_check_hash(const uint8_t* data, const size_t dlen, const uint8_t* chash) {
    SHA256Context ctx;
    uint8_t digest[256/8];

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

    return (0 == memcmp(digest, chash, 256/8));
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

    *aa = ret;

    return true;
}

void AssetArchive_close(struct AssetArchive* aa) {
    if(aa) {
        if(aa->db) {
            sqlite3_close(aa->db);
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

static inline bool decompress(uint8_t** decompdata, size_t* dlen, const uint8_t* compdata, const size_t complen);

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
            bool success = decompress(&tmp, &tlen, dptr, dlen);
            if(success) {
                data[i] = tmp;
                dlens[i] = tlen;
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


static inline bool decompress(uint8_t** decompdata, size_t* dlen, const uint8_t* compdata, const size_t complen) {
    lzma_ret ret;
    uint8_t* buf = calloc(MAX_SIZE, 1);
    size_t inpos=0;
    size_t bufpos=0;
    uint64_t memuse = UINT32_MAX;
    uint8_t* res;

    if(!buf || !compdata) {
        if(buf) free(buf);
        return false;
    }

    ret = lzma_stream_buffer_decode(&memuse, 0, NULL, compdata, &inpos, complen, buf, &bufpos, MAX_SIZE);

    if(LZMA_OK != ret && LZMA_NO_CHECK != ret) {
        free(buf);
        return false;
    }

    if(bufpos>=MAX_SIZE) {
        *decompdata = buf;
        *dlen = MAX_SIZE;
        return true;
    }

    res = calloc(bufpos, 1);
    if(!res) {
        free(buf);
        return false;
    }

    memcpy(res, buf, bufpos);
    free(buf);
    *decompdata = res;
    *dlen = bufpos;

    return true;
}
