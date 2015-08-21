#pragma once
#ifndef CGF_PRIVATE_H
#define CGF_PRIVATE_H
#include "cgf.h"

#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101

typedef void (*sqlite3_destructor_type)(void*);
#define SQLITE_STATIC      ((sqlite3_destructor_type)0)
#define SQLITE_TRANSIENT   ((sqlite3_destructor_type)-1)

#define SQLITE_OPEN_READONLY 0x00000001

typedef void sqlite3;
typedef void sqlite3_stmt;
typedef int64_t sqlite3_int64;

struct AssetArchive {
    sqlite3* db;
    uint8_t* decomp_buf;
};


#define CGF_MAX_CHUNK_SIZE (UINT32_C(48) << 20)
#define CGF_HASH_SIZE (256/8)


//helper functions used by the library -- exposed for testing purposes
bool AssetArchive_check_hash(const uint8_t* data, const size_t dlen, const uint8_t chash[CGF_HASH_SIZE]);

#endif /* CGF_PRIVATE_H */
