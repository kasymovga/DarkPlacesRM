/*
Copyright (C) 2015 Clemens Oliver Hoppe.
Copyright (C) 2016 Micah Talkiewicz.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// hashtable.c -- Generic siphash-based hashmap implmentation.

#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quakedef.h"
#include "hashtable.h"

// The hashing algo
static inline uint64_t siphash_Block(const char* name, const size_t len, const uint8_t key[16]) {
	uint64_t res;
	siphash(&res, (const uint8_t*)name, len, key);

	return res;
}

/* HashTable_Entry_New (void)
 * Create and return a new entry for the hashtable. */
inline hashtable_entry_t *HashTable_Entry_New(void) {
	hashtable_entry_t *entry;
	entry = (hashtable_entry_t*) malloc(sizeof(hashtable_entry_t));
	entry->object = NULL;
	entry->name = NULL;
	entry->next = NULL;
	entry->previous = NULL;
	return entry; 
}

/* HashTable_New
 * Creates a new hashmap. */
hashtable_t *HashTable_New(const char *name, const int size) {
	hashtable_t *map;
	int i, name_len;

	/* Make a map, or about. */
	if ( (map = (hashtable_t *) malloc(sizeof(hashtable_t))) == NULL) {
		return NULL;
	}

	name_len = strlen(name);
	/* Copy the name for our usage. */
	if ( (map->name = (void *) malloc(name_len * sizeof(char))) == NULL) {
		free(map);
		return NULL;
	} else {
		memcpy(map->name, name, name_len);
	}

	/* The table stores empty values as NULL, so we use calloc() */
	if ( (map->table = (void *) calloc(size, sizeof(void*))) == NULL) {
		free(map->name);
		free(map);
		return NULL;
	}
	map->size = size; /* Record the size of the table. */

	/* Seed and initialize the key. */
	for(i=0; i<16; ++i) {
		uint64_t tmp = 0;
		if(0 == i) {
		tmp = (uint64_t)clock();
		} else if(8 == i) {
		tmp = (uint64_t)time(NULL);
		}
		map->key[i] ^= ((tmp >> (8*(i%8))) & UINT8_C(0xff));
	}
	return map;
}

static inline int HashTable_Index(const char* str, const int len, const hashtable_t *map) {
	return siphash_Block(str, len, map->key) % map->size;
}

/* HashTable_FindHead
 * Find the Head entry of a hashchain, then return that entry. */
static inline hashtable_entry_t * HashTable_FindHead(const char* str, const int len, const hashtable_t *map) {
	int Index;

	// I feel a lot more could go wrong here. -- Player_2
	if (str == NULL || map == NULL || map->size == 0 || map->table == NULL) {
		return NULL; // Invalid... something... dono... just return null
	}

	Index = HashTable_Index(str, len, map);
	if (Index < 0 || Index > map->size) {
		return NULL;
	}

	return map->table[Index];
}

/* HashTable_FindEntry
 * Find an entry in the map, then return that entry. */
static inline hashtable_entry_t * HashTable_FindEntry(const char* str, const int len, const hashtable_t *map) {
	hashtable_entry_t *entry = NULL;
	entry = HashTable_FindHead(str, len, map);
	while(entry && strcmp(entry->name, str)) {
		entry = entry->next;
	}
	return entry;
}

/* HashTable_Locate
 * Find an entry in the map, then return it's data. */
void* HashTable_Locate(const char* str, const hashtable_t *map) {
	hashtable_entry_t *entry = NULL;
	int len;

	len = strlen(str);
	entry = HashTable_FindEntry(str, len, map);
	if (entry == NULL) {
		return NULL;
	}

	return entry->object; /* By this point we have what we want, or not. */
}

void HashTable_Remove(const char* str, const hashtable_t *map) {
	hashtable_entry_t *entry, *entry_previous;
	int len;

	len = strlen(str);

	/* If the entry doesn't exist, we're done. */
	if ( (entry = HashTable_FindEntry(str, len, map)) == NULL) {
		return;
	}

	/* Remove the entry, then relink the hashchain. */
	if ( (entry_previous = entry->previous) == NULL) {
		int Index = HashTable_Index(str, len, map);
		map->table[Index] = entry->next;
	} else {
		entry_previous->next = entry->next;
	}

	free(entry->name);
	free(entry);
}

/* HashTable_Add 
 * Add or Replace an item in the map. */
hashtable_entry_t *HashTable_Add(const char* str, hashtable_t *map, void *object) {
	hashtable_entry_t *entry = NULL;
	int len;

	/* Can't add data to an empty map. */
	if (map == NULL) {
		return NULL;
	}

	len = strlen(str);
	/* Add the entry, if it doesn't already exist. */
	if ( (entry = HashTable_FindEntry(str, len, map)) == NULL) {
		hashtable_entry_t *head;
		int Index;
		
		Index = HashTable_Index(str, len, map);
		entry = HashTable_Entry_New();

		if ( (head = HashTable_FindHead(str, len, map)) != NULL){
			head->previous = entry;
			entry->next = head;
		}
		map->table[Index] = entry;
	}

	/* Something went wrong... */
	if (entry == NULL) {
		return NULL;
	}

	len++; /* We need to copy the Null-terminator.  */
	if ( (entry->name = (char*) malloc(sizeof(char)*len)) == NULL) {
			free(entry);
			return NULL;
	}

	memcpy(entry->name, str, len);
	entry->object = object;

	return entry;
}

/* HashTable_List
 * List all the hashvalues and their names. */
void HashTable_List(const hashtable_t *map) {
	hashtable_entry_t *entry;
	int count, unique = 0;
	int i;

	if (!map || !map->name || !map->table)
		return;

	printf("Emitting hashtable %s\n", map->name);
	count = 0;
	for (i=0; i < map->size; i++)
		if ( (entry = map->table[i])) {
			while (entry) {
				count++;
				if (entry->name)
					printf("Index %d : %s\n", i, entry->name);
				else 
					printf("Index %d is empty.\n", i);
				entry = entry->next;
			}
			unique++;
		}
	printf("Total index count: %d\n", count);
	printf("Total unique indices: %d\n", unique);
}

/* HashTable_Count_Total
 * Return the count of hashes in a tabe. */
int HashTable_Count_Total(const hashtable_t *map) {
	hashtable_entry_t *entry;
	int ret = 0;
	int i;

	if (!map || !map->name || !map->table)
		return -1;

	for (i=0; i < map->size; i++) {
		if ( (entry = map->table[i])) {
			while (entry) {
				entry = entry->next;
				ret++;
			}
		}
	}
	return ret;
}

/* HashTable_List_Unique
 * Return the count of unique hashes in a tabe. */
int HashTable_Count_Unique(const hashtable_t *map) {
	hashtable_entry_t *entry;
	int ret = 0;
	int i;

	if (!map || !map->name || !map->table)
		return -1;

	for (i=0; i < map->size; i++) {
		if ( (entry = map->table[i])) {
			ret++;
		}
	}
	return ret;
}
