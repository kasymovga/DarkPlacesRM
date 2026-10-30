/*
Copyright (C) 1996-1997 Id Software, Inc.

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
// Z_zone.c

#include "quakedef.h"
#include "thread.h"
#include "random.h"

#ifdef WIN32
#include <windows.h>
#include <winbase.h>
#else
#include <unistd.h>
#endif

#ifdef _MSC_VER
#include <vadefs.h>
#else
#include <stdint.h>
#endif
#define MEMHEADER_SENTINEL_FOR_ADDRESS(p) ((sentinel_seed ^ (unsigned int) (uintptr_t) (p)) + sentinel_seed)
unsigned int sentinel_seed;

qboolean mem_bigendian = false;
void *mem_mutex = NULL;

cvar_t developer_memory = {0, "developer_memory", "0", "prints debugging information about memory allocations"};
cvar_t developer_memorydebug = {0, "developer_memorydebug", "0", "enables memory corruption checks (very slow)"};
cvar_t developer_memoryreportlargerthanmb = {0, "developer_memorylargerthanmb", "16", "prints debugging information about memory allocations over this size"};
cvar_t sys_memsize_physical = {CVAR_READONLY, "sys_memsize_physical", "", "physical memory size in MB (or empty if unknown)"};
cvar_t sys_memsize_virtual = {CVAR_READONLY, "sys_memsize_virtual", "", "virtual memory size in MB (or empty if unknown)"};

static mempool_t *poolchain = NULL;

void Mem_PrintStats(void);
void Mem_PrintList(size_t minallocationsize);

static void *attempt_malloc(size_t size)
{
	void *base;
	// try for half a second or so
	unsigned int attempts = 500;
	while (attempts--)
	{
		base = (void *)malloc(size);
		if (base)
			return base;
		Sys_Sleep(1000);
	}
	return NULL;
}

// low level clumping functions, all other memory functions use these
static void *Clump_AllocBlock(size_t size)
{
	unsigned char *base;
	base = (unsigned char *)attempt_malloc(size);
	return base;
}

static void Clump_FreeBlock(void *base, size_t size)
{
	free(base);
}

void *_Mem_Alloc(mempool_t *pool, void *olddata, size_t size, size_t alignment, const char *filename, int fileline)
{
	unsigned int sentinel1;
	unsigned int sentinel2;
	size_t realsize;
	size_t sharedsize;
	size_t remainsize;
	memheader_t *mem;
	memheader_t *oldmem;
	unsigned char *base;

	if (size <= 0)
	{
		if (olddata)
			_Mem_Free(olddata, filename, fileline);
		return NULL;
	}
	if (pool == NULL)
	{
		if(olddata)
			pool = ((memheader_t *)((unsigned char *) olddata - sizeof(memheader_t)))->pool;
		else
			Sys_Error("Mem_Alloc: pool == NULL (alloc at %s:%i)", filename, fileline);
	}
	if (mem_mutex)
		Thread_LockMutex(mem_mutex);
	if (developer_memory.integer || size >= developer_memoryreportlargerthanmb.value * 1048576)
		Con_DPrintf("Mem_Alloc: pool %s, file %s:%i, size %f bytes (%f MB)\n", pool->name, filename, fileline, (double)size, (double)size / 1048576.0f);
	//if (developer.integer > 0 && developer_memorydebug.integer)
	//	_Mem_CheckSentinelsGlobal(filename, fileline);
	pool->totalsize += size;
	realsize = alignment + sizeof(memheader_t) + size + sizeof(sentinel2);
	pool->realsize += realsize;
	base = (unsigned char *)Clump_AllocBlock(realsize);
	if (base == NULL)
	{
		Mem_PrintList(0);
		Mem_PrintStats();
		Mem_PrintList(1<<30);
		Mem_PrintStats();
		Sys_Error("Mem_Alloc: out of memory (alloc of size %f (%.3fMB) at %s:%i)", (double)realsize, (double)realsize / (1 << 20), filename, fileline);
	}
	// calculate address that aligns the end of the memheader_t to the specified alignment
	mem = (memheader_t*)((((size_t)base + sizeof(memheader_t) + (alignment-1)) & ~(alignment-1)) - sizeof(memheader_t));
	mem->baseaddress = (void*)base;
	mem->filename = filename;
	mem->fileline = fileline;
	mem->size = size;
	mem->pool = pool;

	// calculate sentinels (detects buffer overruns, in a way that is hard to exploit)
	sentinel1 = MEMHEADER_SENTINEL_FOR_ADDRESS(&mem->sentinel);
	sentinel2 = MEMHEADER_SENTINEL_FOR_ADDRESS((unsigned char *) mem + sizeof(memheader_t) + mem->size);
	mem->sentinel = sentinel1;
	memcpy((unsigned char *) mem + sizeof(memheader_t) + mem->size, &sentinel2, sizeof(sentinel2));

	// append to head of list
	mem->next = pool->chain;
	mem->prev = NULL;
	pool->chain = mem;
	if (mem->next)
		mem->next->prev = mem;

	if (mem_mutex)
		Thread_UnlockMutex(mem_mutex);

	// copy the shared portion in the case of a realloc, then memset the rest
	sharedsize = 0;
	remainsize = size;
	if (olddata)
	{
		oldmem = (memheader_t*)olddata - 1;
		sharedsize = min(oldmem->size, size);
		memcpy((void *)((unsigned char *) mem + sizeof(memheader_t)), olddata, sharedsize);
		remainsize -= sharedsize;
		_Mem_Free(olddata, filename, fileline);
	}
	memset((void *)((unsigned char *) mem + sizeof(memheader_t) + sharedsize), 0, remainsize);
	return (void *)((unsigned char *) mem + sizeof(memheader_t));
}

// only used by _Mem_Free and _Mem_FreePool
static void _Mem_FreeBlock(memheader_t *mem, const char *filename, int fileline)
{
	mempool_t *pool;
	size_t size;
	size_t realsize;
	unsigned int sentinel1;
	unsigned int sentinel2;

	// check sentinels (detects buffer overruns, in a way that is hard to exploit)
	sentinel1 = MEMHEADER_SENTINEL_FOR_ADDRESS(&mem->sentinel);
	sentinel2 = MEMHEADER_SENTINEL_FOR_ADDRESS((unsigned char *) mem + sizeof(memheader_t) + mem->size);
	if (mem->sentinel != sentinel1)
		Sys_Error("Mem_Free: trashed head sentinel (alloc at %s:%i, free at %s:%i)", mem->filename, mem->fileline, filename, fileline);
	if (memcmp((unsigned char *) mem + sizeof(memheader_t) + mem->size, &sentinel2, sizeof(sentinel2)))
		Sys_Error("Mem_Free: trashed tail sentinel (alloc at %s:%i, free at %s:%i)", mem->filename, mem->fileline, filename, fileline);

	pool = mem->pool;
	if (developer_memory.integer)
		Con_DPrintf("Mem_Free: pool %s, alloc %s:%i, free %s:%i, size %i bytes\n", pool->name, mem->filename, mem->fileline, filename, fileline, (int)(mem->size));
	// unlink memheader from doubly linked list
	if (mem_mutex)
		Thread_LockMutex(mem_mutex);
	if ((mem->prev ? mem->prev->next != mem : pool->chain != mem) || (mem->next && mem->next->prev != mem))
		Sys_Error("Mem_Free: not allocated or double freed (free at %s:%i)", filename, fileline);
	if (mem->prev)
		mem->prev->next = mem->next;
	else
		pool->chain = mem->next;
	if (mem->next)
		mem->next->prev = mem->prev;
	// memheader has been unlinked, do the actual free now
	size = mem->size;
	realsize = sizeof(memheader_t) + size + sizeof(sentinel2);
	pool->totalsize -= size;
	pool->realsize -= realsize;
	Clump_FreeBlock(mem->baseaddress, realsize);
	if (mem_mutex)
		Thread_UnlockMutex(mem_mutex);
}

void _Mem_Free(void *data, const char *filename, int fileline)
{
	if (data == NULL)
	{
		Con_DPrintf("Mem_Free: data == NULL (called at %s:%i)\n", filename, fileline);
		return;
	}

	if (developer_memorydebug.integer)
	{
		//_Mem_CheckSentinelsGlobal(filename, fileline);
		if (!Mem_IsAllocated(NULL, data))
			Sys_Error("Mem_Free: data is not allocated (called at %s:%i)", filename, fileline);
	}

	_Mem_FreeBlock((memheader_t *)((unsigned char *) data - sizeof(memheader_t)), filename, fileline);
}

mempool_t *_Mem_AllocPool(const char *name, int flags, mempool_t *parent, const char *filename, int fileline)
{
	mempool_t *pool;
	if (developer_memorydebug.integer)
		_Mem_CheckSentinelsGlobal(filename, fileline);
	pool = (mempool_t *)Clump_AllocBlock(sizeof(mempool_t));
	if (pool == NULL)
	{
		Mem_PrintList(0);
		Mem_PrintStats();
		Mem_PrintList(1<<30);
		Mem_PrintStats();
		Sys_Error("Mem_AllocPool: out of memory (allocpool at %s:%i)", filename, fileline);
	}
	memset(pool, 0, sizeof(mempool_t));
	pool->sentinel1 = MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel1);
	pool->sentinel2 = MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel2);
	pool->filename = filename;
	pool->fileline = fileline;
	pool->flags = flags;
	pool->chain = NULL;
	pool->totalsize = 0;
	pool->realsize = sizeof(mempool_t);
	strlcpy (pool->name, name, sizeof (pool->name));
	pool->parent = parent;
	pool->next = poolchain;
	poolchain = pool;
	return pool;
}

void _Mem_FreePool(mempool_t **poolpointer, const char *filename, int fileline)
{
	mempool_t *pool = *poolpointer;
	mempool_t **chainaddress, *iter, *temp;

	if (developer_memorydebug.integer)
		_Mem_CheckSentinelsGlobal(filename, fileline);
	if (pool)
	{
		// unlink pool from chain
		for (chainaddress = &poolchain;*chainaddress && *chainaddress != pool;chainaddress = &((*chainaddress)->next));
		if (*chainaddress != pool)
			Sys_Error("Mem_FreePool: pool already free (freepool at %s:%i)", filename, fileline);
		if (pool->sentinel1 != MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel1))
			Sys_Error("Mem_FreePool: trashed pool sentinel 1 (allocpool at %s:%i, freepool at %s:%i)", pool->filename, pool->fileline, filename, fileline);
		if (pool->sentinel2 != MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel2))
			Sys_Error("Mem_FreePool: trashed pool sentinel 2 (allocpool at %s:%i, freepool at %s:%i)", pool->filename, pool->fileline, filename, fileline);
		*chainaddress = pool->next;

		// free memory owned by the pool
		while (pool->chain)
			_Mem_FreeBlock(pool->chain, filename, fileline);

		// free child pools, too
		for(iter = poolchain; iter; iter = temp) {
			temp = iter->next;
			if(iter->parent == pool)
				_Mem_FreePool(&temp, filename, fileline);
		}

		// free the pool itself
		Clump_FreeBlock(pool, sizeof(*pool));

		*poolpointer = NULL;
	}
}

void _Mem_EmptyPool(mempool_t *pool, const char *filename, int fileline)
{
	mempool_t *chainaddress;

	if (developer_memorydebug.integer)
	{
		//_Mem_CheckSentinelsGlobal(filename, fileline);
		// check if this pool is in the poolchain
		for (chainaddress = poolchain;chainaddress;chainaddress = chainaddress->next)
			if (chainaddress == pool)
				break;
		if (!chainaddress)
			Sys_Error("Mem_EmptyPool: pool is already free (emptypool at %s:%i)", filename, fileline);
	}
	if (pool == NULL)
		Sys_Error("Mem_EmptyPool: pool == NULL (emptypool at %s:%i)", filename, fileline);
	if (pool->sentinel1 != MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel1))
		Sys_Error("Mem_EmptyPool: trashed pool sentinel 1 (allocpool at %s:%i, emptypool at %s:%i)", pool->filename, pool->fileline, filename, fileline);
	if (pool->sentinel2 != MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel2))
		Sys_Error("Mem_EmptyPool: trashed pool sentinel 2 (allocpool at %s:%i, emptypool at %s:%i)", pool->filename, pool->fileline, filename, fileline);

	// free memory owned by the pool
	while (pool->chain)
		_Mem_FreeBlock(pool->chain, filename, fileline);

	// empty child pools, too
	for(chainaddress = poolchain; chainaddress; chainaddress = chainaddress->next)
		if(chainaddress->parent == pool)
			_Mem_EmptyPool(chainaddress, filename, fileline);

}

void _Mem_CheckSentinels(void *data, const char *filename, int fileline)
{
	memheader_t *mem;
	unsigned int sentinel1;
	unsigned int sentinel2;

	if (data == NULL)
		Sys_Error("Mem_CheckSentinels: data == NULL (sentinel check at %s:%i)", filename, fileline);

	mem = (memheader_t *)((unsigned char *) data - sizeof(memheader_t));
	sentinel1 = MEMHEADER_SENTINEL_FOR_ADDRESS(&mem->sentinel);
	sentinel2 = MEMHEADER_SENTINEL_FOR_ADDRESS((unsigned char *) mem + sizeof(memheader_t) + mem->size);
	if (mem->sentinel != sentinel1)
		Sys_Error("Mem_Free: trashed head sentinel (alloc at %s:%i, sentinel check at %s:%i)", mem->filename, mem->fileline, filename, fileline);
	if (memcmp((unsigned char *) mem + sizeof(memheader_t) + mem->size, &sentinel2, sizeof(sentinel2)))
		Sys_Error("Mem_Free: trashed tail sentinel (alloc at %s:%i, sentinel check at %s:%i)", mem->filename, mem->fileline, filename, fileline);
}

void _Mem_CheckSentinelsGlobal(const char *filename, int fileline)
{
	memheader_t *mem;
	mempool_t *pool;
	for (pool = poolchain;pool;pool = pool->next)
	{
		if (pool->sentinel1 != MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel1))
			Sys_Error("Mem_CheckSentinelsGlobal: trashed pool sentinel 1 (allocpool at %s:%i, sentinel check at %s:%i)", pool->filename, pool->fileline, filename, fileline);
		if (pool->sentinel2 != MEMHEADER_SENTINEL_FOR_ADDRESS(&pool->sentinel2))
			Sys_Error("Mem_CheckSentinelsGlobal: trashed pool sentinel 2 (allocpool at %s:%i, sentinel check at %s:%i)", pool->filename, pool->fileline, filename, fileline);
	}
	for (pool = poolchain;pool;pool = pool->next)
		for (mem = pool->chain;mem;mem = mem->next)
			_Mem_CheckSentinels((void *)((unsigned char *) mem + sizeof(memheader_t)), filename, fileline);
}

qboolean Mem_IsAllocated(mempool_t *pool, void *data)
{
	memheader_t *header;
	memheader_t *target;

	if (pool)
	{
		// search only one pool
		target = (memheader_t *)((unsigned char *) data - sizeof(memheader_t));
		for( header = pool->chain ; header ; header = header->next )
			if( header == target )
				return true;
	}
	else
	{
		// search all pools
		for (pool = poolchain;pool;pool = pool->next)
			if (Mem_IsAllocated(pool, data))
				return true;
	}
	return false;
}

void Mem_ExpandableArray_NewArray(memexpandablearray_t *l, mempool_t *mempool, size_t recordsize, int numrecordsperarray)
{
	memset(l, 0, sizeof(*l));
	l->mempool = mempool;
	l->recordsize = recordsize;
	l->numrecordsperarray = numrecordsperarray;
}

void Mem_ExpandableArray_FreeArray(memexpandablearray_t *l)
{
	size_t i;
	if (l->maxarrays)
	{
		for (i = 0;i != l->numarrays;i++)
			Mem_Free(l->arrays[i].data);
		Mem_Free(l->arrays);
	}
	memset(l, 0, sizeof(*l));
}

void *Mem_ExpandableArray_AllocRecord(memexpandablearray_t *l)
{
	size_t i, j;
	for (i = 0;;i++)
	{
		if (i == l->numarrays)
		{
			if (l->numarrays == l->maxarrays)
			{
				memexpandablearray_array_t *oldarrays = l->arrays;
				l->maxarrays = max(l->maxarrays * 2, 128);
				l->arrays = (memexpandablearray_array_t*) Mem_Alloc(l->mempool, l->maxarrays * sizeof(*l->arrays));
				if (oldarrays)
				{
					memcpy(l->arrays, oldarrays, l->numarrays * sizeof(*l->arrays));
					Mem_Free(oldarrays);
				}
			}
			l->arrays[i].numflaggedrecords = 0;
			l->arrays[i].data = (unsigned char *) Mem_Alloc(l->mempool, (l->recordsize + 1) * l->numrecordsperarray);
			l->arrays[i].allocflags = l->arrays[i].data + l->recordsize * l->numrecordsperarray;
			l->numarrays++;
		}
		if (l->arrays[i].numflaggedrecords < l->numrecordsperarray)
		{
			for (j = 0;j < l->numrecordsperarray;j++)
			{
				if (!l->arrays[i].allocflags[j])
				{
					l->arrays[i].allocflags[j] = true;
					l->arrays[i].numflaggedrecords++;
					if (l->arrays[i].data == NULL)
						Sys_Error("Mem_ExpandableArray_AllocRecord: Setting dead memory?");
					memset(l->arrays[i].data + l->recordsize * j, 0, l->recordsize);
					return (void *)(l->arrays[i].data + l->recordsize * j);
				}
			}
		}
	}
}

/*****************************************************************************
 * IF YOU EDIT THIS:
 * If this function was to change the size of the "expandable" array, you have
 * to update r_shadow.c
 * Just do a search for "range =", R_ShadowClearWorldLights would be the first
 * function to look at. (And also seems like the only one?) You  might have to
 * move the  call to Mem_ExpandableArray_IndexRange  back into for(...) loop's
 * condition
 */
void Mem_ExpandableArray_FreeRecord(memexpandablearray_t *l, void *record) // const!
{
	size_t i, j;
	unsigned char *p = (unsigned char *)record;
	for (i = 0;i != l->numarrays;i++)
	{
		if (p >= l->arrays[i].data && p < (l->arrays[i].data + l->recordsize * l->numrecordsperarray))
		{
			j = (p - l->arrays[i].data) / l->recordsize;
			if (p != l->arrays[i].data + j * l->recordsize)
				Sys_Error("Mem_ExpandableArray_FreeRecord: no such record %p\n", p);
			if (!l->arrays[i].allocflags[j])
				Sys_Error("Mem_ExpandableArray_FreeRecord: record %p is already free!\n", p);
			l->arrays[i].allocflags[j] = false;
			l->arrays[i].numflaggedrecords--;
			return;
		}
	}
}

size_t Mem_ExpandableArray_IndexRange(const memexpandablearray_t *l)
{
	size_t i, j, k, end = 0;
	for (i = 0;i < l->numarrays;i++)
	{
		for (j = 0, k = 0;k < l->arrays[i].numflaggedrecords;j++)
		{
			if (l->arrays[i].allocflags[j])
			{
				end = l->numrecordsperarray * i + j + 1;
				k++;
			}
		}
	}
	return end;
}

void *Mem_ExpandableArray_RecordAtIndex(const memexpandablearray_t *l, size_t index)
{
	size_t i, j;
	i = index / l->numrecordsperarray;
	j = index % l->numrecordsperarray;
	if (i >= l->numarrays || !l->arrays[i].allocflags[j])
		return NULL;
	return (void *)(l->arrays[i].data + j * l->recordsize);
}


// used for temporary memory allocations around the engine, not for longterm
// storage, if anything in this pool stays allocated during gameplay, it is
// considered a leak
mempool_t *tempmempool;
// only for zone
mempool_t *zonemempool;

void Mem_PrintStats(void)
{
	size_t count = 0, size = 0, realsize = 0;
	mempool_t *pool;
	memheader_t *mem;
	Mem_CheckSentinelsGlobal();
	for (pool = poolchain;pool;pool = pool->next)
	{
		count++;
		size += pool->totalsize;
		realsize += pool->realsize;
	}
	Con_Printf("%lu memory pools, totalling %lu bytes (%.3fMB)\n", (unsigned long)count, (unsigned long)size, size / 1048576.0);
	Con_Printf("total allocated size: %lu bytes (%.3fMB)\n", (unsigned long)realsize, realsize / 1048576.0);
	for (pool = poolchain;pool;pool = pool->next)
	{
		if ((pool->flags & POOLFLAG_TEMP) && pool->chain)
		{
			Con_Printf("Memory pool %p has sprung a leak totalling %lu bytes (%.3fMB)!  Listing contents...\n", (void *)pool, (unsigned long)pool->totalsize, pool->totalsize / 1048576.0);
			for (mem = pool->chain;mem;mem = mem->next)
				Con_Printf("%10lu bytes allocated at %s:%i\n", (unsigned long)mem->size, mem->filename, mem->fileline);
		}
	}
}

void Mem_PrintList(size_t minallocationsize)
{
	mempool_t *pool;
	memheader_t *mem;
	Mem_CheckSentinelsGlobal();
	Con_Print("memory pool list:\n"
	           "size    name\n");
	for (pool = poolchain;pool;pool = pool->next)
	{
		Con_Printf("%10luk (%10luk actual) %s (%+li byte change) %s\n", (unsigned long) ((pool->totalsize + 1023) / 1024), (unsigned long)((pool->realsize + 1023) / 1024), pool->name, (long)(pool->totalsize - pool->lastchecksize), (pool->flags & POOLFLAG_TEMP) ? "TEMP" : "");
		pool->lastchecksize = pool->totalsize;
		for (mem = pool->chain;mem;mem = mem->next)
			if (mem->size >= minallocationsize)
				Con_Printf("%10lu bytes allocated at %s:%i\n", (unsigned long)mem->size, mem->filename, mem->fileline);
	}
}

static void MemList_f(void)
{
	switch(Cmd_Argc())
	{
	case 1:
		Mem_PrintList(1<<30);
		Mem_PrintStats();
		break;
	case 2:
		Mem_PrintList(atoi(Cmd_Argv(1)) * 1024);
		Mem_PrintStats();
		break;
	default:
		Con_Print("MemList_f: unrecognized options\nusage: memlist [all]\n");
		break;
	}
}

static void MemStats_f(void)
{
	Mem_CheckSentinelsGlobal();
	#ifndef CONFIG_SV
	R_TextureStats_Print(false, false, true);
	GL_Mesh_ListVBOs(false);
	#endif
	Mem_PrintStats();
}


char* Mem_strdup (mempool_t *pool, const char* s)
{
	char* p;
	size_t sz;
	if (s == NULL)
		return NULL;
	sz = strlen (s) + 1;
	p = (char*)Mem_Alloc (pool, sz);
	strlcpy (p, s, sz);
	return p;
}

/*
========================
Memory_Init
========================
*/
void Memory_Init (void)
{
	static union {unsigned short s;unsigned char b[2];} u;
	u.s = 0x100;
	mem_bigendian = u.b[0] != 0;

	sentinel_seed = xrand();
	poolchain = NULL;
	tempmempool = Mem_AllocPool("Temporary Memory", POOLFLAG_TEMP, NULL);
	zonemempool = Mem_AllocPool("Zone", 0, NULL);

	if (Thread_HasThreads())
		mem_mutex = Thread_CreateMutex();
}

void Memory_Shutdown (void)
{
//	Mem_FreePool (&zonemempool);
//	Mem_FreePool (&tempmempool);

	if (mem_mutex)
		Thread_DestroyMutex(mem_mutex);
	mem_mutex = NULL;
}

void Memory_Init_Commands (void)
{
#ifndef WIN32
	char com_token[MAX_INPUTLINE];
#endif
	Cmd_AddCommand ("memstats", MemStats_f, "prints memory system statistics");
	Cmd_AddCommand ("memlist", MemList_f, "prints memory pool information (or if used as memlist 5 lists individual allocations of 5K or larger, 0 lists all allocations)");
	Cvar_RegisterVariable (&developer_memory);
	Cvar_RegisterVariable (&developer_memorydebug);
	Cvar_RegisterVariable (&developer_memoryreportlargerthanmb);
	Cvar_RegisterVariable (&sys_memsize_physical);
	Cvar_RegisterVariable (&sys_memsize_virtual);

#if defined(WIN32)
#ifdef _WIN64
	{
		MEMORYSTATUSEX status;
		// first guess
		Cvar_SetValueQuick(&sys_memsize_virtual, 8388608);
		// then improve
		status.dwLength = sizeof(status);
		if(GlobalMemoryStatusEx(&status))
		{
			Cvar_SetValueQuick(&sys_memsize_physical, status.ullTotalPhys / 1048576.0);
			Cvar_SetValueQuick(&sys_memsize_virtual, min(sys_memsize_virtual.value, status.ullTotalVirtual / 1048576.0));
		}
	}
#else
	{
		MEMORYSTATUS status;
		// first guess
		Cvar_SetValueQuick(&sys_memsize_virtual, 2048);
		// then improve
		status.dwLength = sizeof(status);
		GlobalMemoryStatus(&status);
		Cvar_SetValueQuick(&sys_memsize_physical, status.dwTotalPhys / 1048576.0);
		Cvar_SetValueQuick(&sys_memsize_virtual, min(sys_memsize_virtual.value, status.dwTotalVirtual / 1048576.0));
	}
#endif
#else
	{
		// first guess
		Cvar_SetValueQuick(&sys_memsize_virtual, (sizeof(void*) == 4) ? 2048 : 268435456);
		// then improve
		{
			// Linux, and BSD with linprocfs mounted
			FILE *f = fopen("/proc/meminfo", "r");
			if(f)
			{
				static char buf[1024];
				while(fgets(buf, sizeof(buf), f))
				{
					const char *p = buf;
					if(!COM_ParseToken_Console(&p, com_token, sizeof(com_token)))
						continue;
					if(!strcmp(com_token, "MemTotal:"))
					{
						if(!COM_ParseToken_Console(&p, com_token, sizeof(com_token)))
							continue;
						Cvar_SetValueQuick(&sys_memsize_physical, atof(com_token) / 1024.0);
					}
					if(!strcmp(com_token, "SwapTotal:"))
					{
						if(!COM_ParseToken_Console(&p, com_token, sizeof(com_token)))
							continue;
						Cvar_SetValueQuick(&sys_memsize_virtual, min(sys_memsize_virtual.value , atof(com_token) / 1024.0 + sys_memsize_physical.value));
					}
				}
				fclose(f);
			}
		}
	}
#endif
}

