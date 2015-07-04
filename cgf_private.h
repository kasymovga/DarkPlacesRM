#pragma once
#ifndef CGF_PRIVATE_H
#define CGF_PRIVATE_H
#include "cgf.h"


struct AssetArchive {
    sqlite3* db;
};


#define CGF_MAX_CHUNK_SIZE (UINT32_C(256) << 20)
#define CGF_HASH_SIZE (256/8)


//helper functions used by the library -- exposed for testing purposes
bool AssetArchive_check_hash(const uint8_t* data, const size_t dlen, const uint8_t chash[CGF_HASH_SIZE]);

#endif /* CGF_PRIVATE_H */
