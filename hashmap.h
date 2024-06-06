#ifndef HASHMAP_H_00
#define HASHMAP_H_00

#include <stdint.h>
#include <stdlib.h>
#define HASHMAP_INIT_BUCKETS 4
#define HASHMAP_INIT_BUCKET_CAP 4
#ifndef HASHMAP_ALLOC_FN
#ifdef HASHMAP_PROCESS_HEAP
#define HASHMAP_ALLOC_FN(size) HeapAlloc(GetProcessHeap(), 0, (size))
#else
#define HASHMAP_ALLOC_FN(size) malloc((size))
#endif
#endif
#ifndef HASHMAP_FREE_FN
#ifdef HASHMAP_PROCESS_HEAP
#define HASHMAP_FREE_FN(ptr) HeapFree(GetProcessHeap(), 0, (ptr))
#else
#define HASHMAP_FREE_FN(ptr) free((ptr))
#endif
#endif
#ifndef HASHMAP_REALLOC_FN
#ifdef HASHMAP_PROCESS_HEAP
#define HASHMAP_REALLOC_FN(ptr, size) HeapReAlloc(GetProcessHeap(), 0, (ptr), (size))
#else
#define HASHMAP_REALLOC_FN(ptr, size) realloc((ptr), (size))
#endif
#endif

typedef struct HashElement {
    const char* const key;
    char* value;
} HashElement;

typedef struct HashBucket {
    HashElement* data;
    uint32_t size;
    uint32_t capacity;
} HashBucket;

typedef struct HashMap {
    HashBucket* buckets;
    uint32_t bucket_count;
    uint32_t element_count;
} HashMap;

typedef struct HashMapFrozen {
    HashMap map;

    uint64_t data_size;
} HashMapFrozen;


void HashMap_Free(HashMap* map);

int HashMap_Allocate(HashMap* map, uint32_t bucket_count);

void HashMap_Clear(HashMap* map);

int HashMap_Create(HashMap* map);

int HashMap_Insert(HashMap* map, const char* key, char* value);

HashElement* HashMap_Find(HashMap* map, const char* key);

HashElement* HashMap_Get(HashMap* map, const char* key);

char* HashMap_Value(HashMap* map, const char* key);

int HashMap_Remove(HashMap* map, const char* key);

int HashMap_Freeze(const HashMap* map, HashMapFrozen* out);

void HashMap_FreeFrozen(HashMapFrozen* map);

#endif
