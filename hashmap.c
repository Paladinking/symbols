#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "hashmap.h"

#ifdef HASHMAP_ALLOC_ERROR
#define CHECKED_CALL(c) if (!(c)) {return 0;}
#define CHECKED_ALLOC(name, size) do {name = HASHMAP_ALLOC_FN(size); if (name == NULL) { return 0; } } while(0)
#else
#define CHECKED_CALL(c) c
#define CHECKED_ALLOC(name, size) name = HASHMAP_ALLOC_FN(size)
#endif

void HashMap_Free(HashMap* map) {
    for (uint32_t i = 0; i < map->bucket_count; ++i) {
        for (uint32_t j = 0; j < map->buckets[i].size; ++j) {
            HASHMAP_FREE_FN((char*)map->buckets[i].data[j].key);
        } 
        HASHMAP_FREE_FN(map->buckets[i].data);
    }
    map->element_count = 0;
    map->bucket_count = 0;
    HASHMAP_FREE_FN(map->buckets);
    map->buckets = NULL;
}

int HashMap_Allocate(HashMap* map, uint32_t bucket_count) {
    map->element_count = 0;
    map->bucket_count = bucket_count;
    map->buckets = HASHMAP_ALLOC_FN(bucket_count * sizeof(HashBucket));
#ifdef HASHMAP_ALLOC_ERROR
    if (map->buckets == NULL) {
        map->bucket_count = 0;
        return 0;
    }
#endif
    for (uint32_t i = 0; i < bucket_count; ++i) {
        map->buckets[i].size = 0;
        map->buckets[i].capacity = HASHMAP_INIT_BUCKET_CAP;
        map->buckets[i].data = HASHMAP_ALLOC_FN(HASHMAP_INIT_BUCKET_CAP * sizeof(HashElement));
#ifdef HASHMAP_ALLOC_ERROR
        if (map->buckets[i].data == NULL) {
            map->bucket_count = i;
            HashMap_Free(map);
            return 0;
        }
#endif
    }
    return 1;
}

void HashMap_Clear(HashMap* map) {
    for (uint32_t i = 0; i < map->bucket_count; ++i) {
        for (uint32_t j = 0; j < map->buckets[i].size; ++j) {
            HASHMAP_FREE_FN((char*)map->buckets[i].data[j].key);
        }
        map->buckets[i].size = 0;
    }
    map->element_count = 0;
}

int HashMap_Create(HashMap* map) {
    return HashMap_Allocate(map, HASHMAP_INIT_BUCKETS);
}

uint64_t hash(const char* str) {
    uint64_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

HashElement* HashMap_GetElement(HashMap* map, const char* key, HashBucket** bucket) {
    uint64_t h = hash(key);
    *bucket = &map->buckets[h % map->bucket_count];
    for (uint32_t i = 0; i < (*bucket)->size; ++i) {
        if (strcmp(key, (*bucket)->data[i].key) == 0) {
            return &((*bucket)->data[i]);
        }
    }
    return NULL;
}

int HashMap_Rehash(HashMap* map) {
    HashMap tmp;
    CHECKED_CALL(HashMap_Allocate(&tmp, map->bucket_count * 2));
    for (uint32_t b = 0; b < map->bucket_count; ++b) {
        for (uint32_t ix = 0; ix < map->buckets[b].size; ++ix) {
            int status = HashMap_Insert(&tmp, map->buckets[b].data[ix].key, map->buckets[b].data[ix].value);
#ifdef HASHMAP_ALLOC_ERROR
            if (!status) {
                HashMap_Free(&tmp);
                return 0;
            }
#endif
        }
    }
    HashMap_Free(map);
    *map = tmp;
    return 1;
}

int HashMap_AddElement(HashMap* map, HashBucket* bucket, HashElement element) {
    if (bucket->size == bucket->capacity) {
        HashElement* new_data;
        CHECKED_ALLOC(new_data, bucket->size * 2 * sizeof(HashElement));
        memcpy(new_data, bucket->data, bucket->size * sizeof(HashElement));
        HASHMAP_FREE_FN(bucket->data);
        bucket->data = new_data;
        bucket->capacity = bucket->size * 2;
    }
    memcpy(bucket->data + bucket->size, &element, sizeof(HashElement));
    ++(bucket->size);
    ++(map->element_count);
    return 1;
}

int HashMap_Insert(HashMap* map, const char* key, char* value) {
    if (map->bucket_count == map->element_count) {
        CHECKED_CALL(HashMap_Rehash(map));
    }
    HashBucket* bucket;
    HashElement* elem = HashMap_GetElement(map, key, &bucket);
    if (elem != NULL) {
        elem->value = value;
        return 1;
    }
    uint32_t len = strlen(key);
    char* buf;
    CHECKED_ALLOC(buf, len + 1);
    memcpy(buf, key, len + 1);
    HashElement he = {buf, value};
    CHECKED_CALL(HashMap_AddElement(map, bucket, he));
    return 1;
}

HashElement* HashMap_Get(HashMap* map, const char* key) {
    if (map->bucket_count == map->element_count) {
        CHECKED_CALL(HashMap_Rehash(map));
    }
    HashBucket* bucket;
    HashElement* elem = HashMap_GetElement(map, key, &bucket);
    if (elem != NULL) {
        return elem;
    }
    uint32_t len = strlen(key);
    char* buf;
    CHECKED_ALLOC(buf, len + 1);
    memcpy(buf, key, len + 1);
    HashElement he = {buf, NULL};
    CHECKED_CALL(HashMap_AddElement(map, bucket, he));
    return &(bucket->data[bucket->size - 1]);
}

HashElement* HashMap_Find(HashMap* map, const char* key) {
    HashBucket* bucket;
    return HashMap_GetElement(map, key, &bucket);
}

char* HashMap_Value(HashMap* map, const char* key) {
    HashBucket *bucket;
    HashElement* element = HashMap_GetElement(map, key, &bucket);
    if (element == NULL) {
        return NULL;
    }
    return element->value;
}

int HashMap_Remove(HashMap* map, const char* key) {
    HashBucket* bucket;
    HashElement* element = HashMap_GetElement(map, key, &bucket);
    if (element == NULL) {
        return 0;
    }
    HASHMAP_FREE_FN((char*)element->key);
    uint32_t ix = element - bucket->data;
    memmove(element, element + 1, (bucket->size - ix - 1) * sizeof(HashElement));
    --bucket->size;
    --map->element_count;
    return 1;
}

int HashMap_Freeze(const HashMap* map, HashMapFrozen *out) {
    uint64_t size = sizeof(HashBucket) * map->bucket_count + sizeof(HashElement) * map->element_count;
    for (uint32_t i = 0; i < map->bucket_count; ++i) {
        for (uint32_t j = 0; j < map->buckets[i].size; ++j) {
            HashElement* elem = &map->buckets[i].data[j];
            size += strlen(elem->key) + 1;
            if (elem->value != NULL) {
                size += strlen(elem->value) + 1;
            }
        }
    }

    char* ptr;
    CHECKED_ALLOC(ptr, size);
    char* bucket_ptr = ptr;
    char* elem_ptr = ptr + map->bucket_count * sizeof(HashBucket);
    char* str_ptr = elem_ptr + map->element_count * sizeof(HashElement);
    for (uint32_t i = 0; i < map->bucket_count; ++i) {
        HashBucket* old_bucket = &map->buckets[i];
        HashBucket bucket = {(HashElement*)elem_ptr, old_bucket->size, old_bucket->size};
        memcpy(bucket_ptr, &bucket, sizeof(HashBucket));
        bucket_ptr += sizeof(HashBucket);
        for (uint32_t j = 0; j < old_bucket->size; ++j) {
            uint32_t key_len = strlen(old_bucket->data[j].key) + 1;
            memcpy(str_ptr, old_bucket->data[j].key, key_len);
            uint32_t val_len = 0;
            HashElement e = {str_ptr, NULL};
            if (old_bucket->data[j].value != NULL) {
                val_len = strlen(old_bucket->data[j].value) + 1;
                memcpy(str_ptr + key_len, old_bucket->data[j].value, val_len);
                e.value = str_ptr + key_len;
            }
            memcpy(elem_ptr, &e, sizeof(e));
            elem_ptr += sizeof(e);
            str_ptr += key_len + val_len;
        }
    }

    out->map.buckets = (HashBucket*)ptr;
    out->map.bucket_count = map->bucket_count;
    out->map.element_count = map->element_count;
    out->data_size = size;

    return 1;
}

void HashMap_FreeFrozen(HashMapFrozen *map) {
    HASHMAP_FREE_FN(map->map.buckets);
    map->map.element_count = 0;
    map->map.bucket_count = 0;
    map->data_size = 0;
}
