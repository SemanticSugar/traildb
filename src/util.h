
#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <Judy.h>

#define DSFMT_MEXP 521
#include "dsfmt/dSFMT.h"

#include "judy_128_map.h"

#define MAX_PATH_SIZE 1024

#define DIE(msg, ...)\
    do { fprintf(stderr, "FAIL: "msg"\n", ##__VA_ARGS__);   \
         exit(EXIT_FAILURE); } while (0)

#define INFO(msg, ...) fprintf(stderr, msg"\n", ##__VA_ARGS__)
#define WARN(msg, ...) fprintf(stderr, msg"\n", ##__VA_ARGS__)

#define SAFE_OPEN(f, path, mode)\
    if ((f = fopen(path, mode)) == NULL){\
        DIE("Could not open %s", path);\
    }

#define SAFE_WRITE(ptr, size, path, f)\
    if (fwrite(ptr, size, 1, f) != 1){\
        DIE("Writing to %s failed (%p)", path, ptr);      \
    }

#define SAFE_FPRINTF(f, path, fmt, ...)\
    if (fprintf(f, fmt, ##__VA_ARGS__) < 1){\
        DIE("Writing to %s failed", path);\
    }

#define SAFE_FREAD(f, path, buf, size)\
    if (fread(buf, size, 1, f) < 1){\
        DIE("Reading from %s failed", path);\
    }

#define SAFE_SEEK(f, offset, path)\
    if (offset > LONG_MAX || fseek(f, (long)(offset), SEEK_SET) == -1){\
        DIE("Seeking to %llu in %s failed", (unsigned long long)offset, path);\
    }

#define SAFE_SEEK_END(f, path)\
    if (fseek(f, 0, SEEK_END) == -1){\
        DIE("Seeking to the end of %s failed", path);\
    }

#define SAFE_TELL(f, val, path)\
    if ((val = ftell(f)) == -1){\
        DIE("Checking file position of %s failed", path);\
    }

#define SAFE_CLOSE(f, path)\
    if (fclose(f)){\
        DIE("Closing %s failed", path);\
    }

#define SAFE_FLUSH(f, path)\
    if (fflush(f)){\
        DIE("Flushing %s failed", path);\
    }

struct sortpair{
    __uint128_t key;
    Word_t value;
};

struct sortpair *sort_judyl(const Pvoid_t judy, Word_t *num_items);

struct sortpair *sort_j128m(const struct judy_128_map *j128m,
                            uint64_t *num_items);

uint8_t bits_needed(uint64_t max);

uint64_t parse_uint64(const char *str, const char *ctx);

void dsfmt_shuffle(uint64_t *arr, uint64_t len, uint32_t seed);

char *dupstrs(const char *strs, size_t num);

const char *mmap_file(const char *path, uint64_t *size);

void make_path(char path[MAX_PATH_SIZE], char *fmt, ...);

#endif /* __UTIL_H__ */
