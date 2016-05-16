#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "siphash.h"

typedef struct hashtable_entry_s {
	void *object;
	char *name;
	struct hashtable_entry_s *next;
	struct hashtable_entry_s *previous;
} hashtable_entry_t;

typedef struct hashtable_s {
	uint8_t key[16]; // the map's unique key
	char *name; // used for debugging
	int size; // size of the map, used for error checking
	hashtable_entry_t **table; // the actuall map
} hashtable_t;

hashtable_t *HashTable_New(const char *name, const int size);

void *HashTable_Locate(const char* str, const hashtable_t *map);
hashtable_entry_t *HashTable_Add(const char* str, hashtable_t *map, void *object);
void HashTable_Remove(const char* str, const hashtable_t *map);
void HashTable_List(const hashtable_t *map);

#endif
