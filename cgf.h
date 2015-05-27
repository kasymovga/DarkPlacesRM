#pragma once
#ifndef CGF_H
#define CGF_H

//portability: requires C99 or newer
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sqlite3.h>

#define MAX_SIZE (UINT32_C(256) << 20)


struct AssetArchive;

//open/close functions
bool AssetArchive_openRead(struct AssetArchive** aa, const char* const filename);
void AssetArchive_close(struct AssetArchive* aa);

//stat functions
int64_t AssetArchive_countAssets(struct AssetArchive* a);
int64_t AssetArchive_countFiles(struct AssetArchive* a);
int64_t AssetArchive_countAliases(struct AssetArchive* a);

//read functions
bool AssetArchive_loadMany(struct AssetArchive* aa, uint8_t* data[], size_t* dlens, const char* filenames[], const size_t count);
bool AssetArchive_loadOne(struct AssetArchive* aa, uint8_t** data, size_t* dlen, const char* filename);

//helper functions used by the library -- exposed for testing purposes
bool AssetArchive_check_hash(const uint8_t* data, const size_t dlen, const uint8_t chash[256/8]);

#endif /* CGF_H */
