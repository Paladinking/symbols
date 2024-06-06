#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include "printf.h"
#include "hashmap.h"
#include "args.h"


typedef struct Mapping {
    const char* data;
    uint64_t size;
    HANDLE mapping;
} Mapping;

Mapping create_mapping(HANDLE file) {
    Mapping m;
    m.size = 0;
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);
    m.size = size.QuadPart;
    m.mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, size.HighPart, size.LowPart, NULL);
    if (m.mapping == NULL) {
        m.data = NULL;
        _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Failed mapping file\n");
        return m;
    }
    m.data = MapViewOfFile(m.mapping, FILE_MAP_READ, 0, 0, 0);
    if (m.data == NULL) {
        CloseHandle(m.mapping);
        _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Failed mapping view\n");
    }

    return m;
}

void close_mapping(Mapping m) {
    UnmapViewOfFile(m.data);
    CloseHandle(m.mapping);
}

#define CHECKED_WRITE(out, addr, size, w) if (!WriteFile(out, addr, size, &w, NULL) || w != size) { goto error; }

DWORD write_frozen_map(const HashMapFrozen* map, HANDLE out) {
    DWORD w;
    CHECKED_WRITE(out, &(map->map.buckets), sizeof(void*), w);
    CHECKED_WRITE(out, &(map->map.bucket_count), sizeof(map->map.bucket_count), w);
    CHECKED_WRITE(out, &(map->map.element_count), sizeof(map->map.element_count), w);
    uint64_t ptr_count = map->map.bucket_count + map->map.element_count * 2;
    CHECKED_WRITE(out, &ptr_count, sizeof(ptr_count), w);
    for (uint32_t i = 0; i < map->map.bucket_count; ++i) {
        uint64_t data_offset = ((unsigned char*)&map->map.buckets[i].data) - ((unsigned char*)map->map.buckets);
        CHECKED_WRITE(out, &data_offset, sizeof(data_offset), w);
        for (uint32_t j = 0; j < map->map.buckets[i].size; ++j) {
            uint64_t key_offset = ((unsigned char*)&(map->map.buckets[i].data[j].key)) - ((unsigned char*)map->map.buckets);
            CHECKED_WRITE(out, &key_offset, sizeof(key_offset), w);
            uint64_t value_offset = ((unsigned char*)&(map->map.buckets[i].data[j].value)) - ((unsigned char*)map->map.buckets);
            CHECKED_WRITE(out, &value_offset, sizeof(value_offset), w);
        }
    }
    uint64_t written = 0;
    unsigned char* data = (unsigned char*)map->map.buckets;
    while (written < map->data_size) {
        if (!WriteFile(out, data + written, map->data_size - written, &w, NULL)) {
            return 1;
        }
        written += w;
    }
    return 0;
error:
    DWORD err = GetLastError();
    if (err == ERROR_SUCCESS) {
        SetLastError(1);
        return 1;
    }
    SetLastError(err);
    return err;
}
#undef CHECKED_WRITE

bool read_frozen_map(HANDLE in, HashMapFrozen *map) {
    Mapping m = create_mapping(in);
    if (m.data == NULL) {
        return false;
    }

    uint64_t alloc_size = 0;
    uint32_t element_count = 0;
    uint32_t bucket_count = 0;
    unsigned char* dest = NULL;
    uint64_t base;
    memcpy(&base, m.data, sizeof(uint64_t));
    memcpy(&bucket_count, m.data + sizeof(uint64_t), sizeof(uint32_t));
    memcpy(&element_count, m.data + sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint32_t));
    uint64_t ptr_count;
    memcpy(&ptr_count, m.data + sizeof(uint64_t) + 2 * sizeof(uint32_t), sizeof(uint64_t));
    alloc_size = m.size - 2 * sizeof(uint32_t) - (2 + ptr_count) * sizeof(uint64_t);
    dest = HeapAlloc(GetProcessHeap(), 0, alloc_size);
    memcpy(dest, m.data + (m.size - alloc_size), alloc_size);
    for (uint64_t i = 0; i < ptr_count; ++i) {
        uint64_t prev;
        uint64_t offset;
        memcpy(&offset, m.data + 2 * sizeof(uint32_t) + (2 + i) * sizeof(uint64_t), sizeof(uint64_t));
        memcpy(&prev, m.data + (m.size - alloc_size) + offset, sizeof(uint64_t));
        unsigned char* addr = dest + prev - base;
        memcpy(dest + offset, &addr, sizeof(char*));
    }
    close_mapping(m);
    map->data_size = alloc_size;
    map->map.buckets = (HashBucket*)dest;
    map->map.element_count = element_count;
    map->map.bucket_count = bucket_count;
    return true;
}

enum MapStatus {
    MAP_EXISTS, MAP_NEEDED, MAP_MISSING_FILE, MAP_PARSE_ERROR
};

enum MapStatus check_map_file(const wchar_t* target, wchar_t* outname, HANDLE* target_handle, HANDLE* out_handle) {
    wchar_t name[244];
    if (_wsplitpath_s(target, NULL, 0, NULL, 0, name, 240, NULL, 0) != 0) {
        return MAP_PARSE_ERROR;
    }
    if (_wmakepath_s(outname, 256, NULL, L"index", name, L".bin") != 0) {
        return MAP_PARSE_ERROR;
    }
    *target_handle = CreateFileW(target, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (*target_handle == INVALID_HANDLE_VALUE) {
        return MAP_MISSING_FILE;
    }

    *out_handle = CreateFileW(outname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (*out_handle == INVALID_HANDLE_VALUE) {
        *out_handle = CreateFileW(outname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (*out_handle == INVALID_HANDLE_VALUE) {
            CloseHandle(*target_handle);
            return MAP_PARSE_ERROR;
        }
        return MAP_NEEDED;
    }

    FILETIME in_time;
    if (!GetFileTime(*target_handle, NULL, NULL, &in_time)) {
        return MAP_NEEDED;
    }

    FILETIME out_time;
    if (!GetFileTime(*out_handle, NULL, NULL, &out_time)) {
        return MAP_NEEDED;
    }

    if (in_time.dwHighDateTime > out_time.dwHighDateTime) {
        return MAP_NEEDED;
    }
    if (in_time.dwHighDateTime == out_time.dwHighDateTime && in_time.dwLowDateTime > out_time.dwLowDateTime) {
        return MAP_NEEDED;
    }
    return MAP_EXISTS;
}

bool create_map_file(HANDLE in, HANDLE out) {
    Mapping m = create_mapping(in);
    if (m.data == NULL) {
        return false;
    }

    char* data = HeapAlloc(GetProcessHeap(), 0, m.size + 1);
    data[m.size] = '\0';
    memcpy(data, m.data, m.size);
    close_mapping(m);
    char* end = data + m.size;
    char* line = data;
    HashMap map;
    HashMap_Create(&map);
    const char* dll = NULL;
    uint32_t dll_len = 0;
    bool success = true;

    while (line < end) {
        if (*line == '\r' || *line == '\n') {
            ++line;
            continue;
        }
        char* b = strchr(line, '\n');
        char* line_end = strchr(line, '\r');
        if (line_end == NULL || (b != NULL && b < line_end)) {
            line_end = b;
        }
        if (!line_end) {
            line_end = end;
        }
        *line_end = '\0';
        unsigned len = line_end - line;
        if (*line != ' ') {
            dll = NULL;
            dll_len = 0;
        } else {
            while (*line == ' ') {
                ++line;
            }
            if (strncmp(line, "fullpath:", 9) == 0) {
                dll = line + 9;
                while (*dll == ' ' || *dll == '\t') {
                    ++dll;
                }
                dll_len = strlen(dll);
            } else if (*line == '-') {
                if (dll == NULL) {
                    success = false;
                    break;
                }
                ++line;
                while (*line == ' ' || *line == '\t') {
                    ++line;
                }
                HashElement* elem = HashMap_Get(&map, line);
                if (elem->value == NULL) {
                    elem->value = HeapAlloc(GetProcessHeap(), 0, dll_len + 1);
                    memcpy(elem->value, dll, dll_len + 1);
                } else {
                    uint32_t old_len = strlen(elem->value);
                    char* s = HeapAlloc(GetProcessHeap(), 0, dll_len + 2 + old_len);
                    memcpy(s, elem->value, old_len);
                    s[old_len] = '\n';
                    memcpy(s + old_len + 1, dll, dll_len + 1);
                    HeapFree(GetProcessHeap(), 0, elem->value);
                    elem->value = s;
                }
            }
        }
        line = line_end + 1;
    }
    HeapFree(GetProcessHeap(), 0, data);
    HashMapFrozen frozen;
    if (success) {
        HashMap_Freeze(&map, &frozen);
    }

    for (uint32_t i = 0; i < map.bucket_count; ++i) {
        for (uint32_t j = 0; j < map.buckets[i].size; ++j) {
            HeapFree(GetProcessHeap(), 0, map.buckets[i].data[j].value);
        }
    }
    HashMap_Free(&map);
    if (!success) {
        return false;
    }
    if (write_frozen_map(&frozen, out) != 0) {
        return false;
    }

    return true;
}

bool find_symbols(const wchar_t* filename, const char* type, const char* arg, bool full_names) {
    wchar_t name[256];
    HANDLE in, out;
    enum MapStatus ms = check_map_file(filename, name, &in, &out);
    if (ms == MAP_PARSE_ERROR) {
        _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Failed creating symbol hash file\n");
        return false;
    } else if (ms == MAP_MISSING_FILE) {
        _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Missing symbol file '%S'", filename);
        return false;
    } else if (ms == MAP_NEEDED) {
        if (!create_map_file(in, out)) {
            _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Failed creating symbol hash file\n");
            CloseHandle(in);
            CloseHandle(out);
            return false;
        }
    }

    HashMapFrozen map;
    if (!read_frozen_map(out, &map)) {
        _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Failed reading symbol hash file\n");
        CloseHandle(in);
        CloseHandle(out);
        return false;
    }

    char* res = HashMap_Value(&map.map, arg);
    if (res == NULL) {
        _printf("No %s matches found for '%s'\n", type, arg);
    } else {
        _printf("%s matches for '%s':\n", type, arg);
        if (full_names) {
            _printf("%s\n", res);
        } else {
            int len = strlen(res);
            char* pos = HeapAlloc(GetProcessHeap(), 0, len + 1);
            char* tmp = pos;
            memcpy(tmp, res, len + 1);
            HashMap seen;
            HashMap_Create(&seen);
            while (1) {
                char* c = strchr(pos, '\n');
                char* base;
                if (c != NULL) {
                    *c = '\0';
                    base = c;
                } else {
                    base = strchr(pos, '\0');
                }
                while (base > tmp && *base != '/' && *base != '\\') {
                    --base;
                }
                if (*base == '/' || *base == '\\') {
                    ++base;
                }
                HashElement* el = HashMap_Get(&seen, base);
                if (el->value == NULL) {
                    _printf("%s\n", base);
                    el->value = "";
                }
                if (c == NULL) {
                    break;
                } else {
                    pos = c + 1;
                }
            }
            HashMap_Free(&seen);
            HeapFree(GetProcessHeap(), 0, tmp);
        }
    }

    CloseHandle(in);
    CloseHandle(out);
    return true;
}


int main() {
    wchar_t* args = GetCommandLineW();
    int argc;
    int status = 1;
    wchar_t** argv = parse_command_line(args, &argc);

    bool lib_type[3] = {true, false, false};
    const wchar_t* lib_type_files[3] = {L"index\\symbols_lib.yaml", L"index\\symbols_dll.yaml", L"index\\symbols_obj.yaml"};
    const char* lib_type_names[3] = {"lib", "dll", "object"};

    bool full_names = false;

    if (find_flag(argv, &argc, L"--dlls", L"-d") > 0) {
        lib_type[1] = true;
        lib_type[0] = false;
    }
    if (find_flag(argv, &argc, L"--objects", L"-o") > 0) {
        lib_type[2] = true;
        lib_type[0] = false;
    }
    if (find_flag(argv, &argc, L"--libs", L"-l") > 0) {
        lib_type[0] = true;
    }
    if (find_flag(argv, &argc, L"--all", L"-a") > 0) {
        for (int i = 0; i < 3; ++i) {
            lib_type[i] = true;
        }
    }
    if (find_flag(argv, &argc, L"--full", L"-f") > 0) {
        full_names = true;
    }

    if (argc <= 1) {
        _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Missing argument\n");
        return 1;
    }
    int len = wcslen(argv[1]);
    char* arg = HeapAlloc(GetProcessHeap(), 0, len + 1);
    arg[len] = '\0';
    for (int i = 0; i < len; ++i) {
        wchar_t c = argv[1][i];
        // Only allow ascii
        if (c > 127) {
            _wprintf_h(GetStdHandle(STD_ERROR_HANDLE), L"Invalid argument '%s'\n", argv[1]);
            goto end;
        }
        arg[i] = c;
    }

    status = 0;
    for (int i = 0; i < 3; ++i) {
        if (!lib_type[i]) {
            continue;
        }
        find_symbols(lib_type_files[i], lib_type_names[i], arg, full_names);
    }

end:
    HeapFree(GetProcessHeap(), 0, arg);
    HeapFree(GetProcessHeap(), 0, argv);

    return status;
}
