
#ifndef __TRAILDB_UTIL_H__
#define __TRAILDB_UTIL_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <Judy.h>

#define DSFMT_MEXP 521
#include "dsfmt/dSFMT.h"

#include "ddb_profile.h"
#include "traildb.h"

#define DIE_ON_ERROR(msg)\
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define DIE(msg, ...)\
    do { fprintf(stderr, "FAIL: ");\
         fprintf(stderr, msg, ##__VA_ARGS__);\
         exit(EXIT_FAILURE); } while (0)

#define SAFE_WRITE(ptr, size, path, f)\
    if (fwrite(ptr, size, 1, f) != 1){\
        DIE("Writing to %s failed\n", path);\
    }

#define SAFE_FPRINTF(f, path, fmt, ...)\
    if (fprintf(f, fmt, ##__VA_ARGS__) < 1){\
        DIE("Writing to %s failed\n", path);\
    }

#define SAFE_FREAD(f, path, buf, size)\
    if (fread(buf, size, 1, f) < 1){\
        DIE("Reading from %s failed\n", path);\
    }

#define SAFE_SEEK(f, offset, path)\
    if (fseek(f, offset, SEEK_SET) == -1){\
        DIE("Seeking to %llu in %s failed\n", (unsigned long long)offset, path);\
    }

#define SAFE_TELL(f, val, path)\
    if ((val = ftell(f)) == 1){\
        DIE("Checking file position of %s failed\n", path);\
    }

#define SAFE_CLOSE(f, path)\
    if (fclose(f)){\
        DIE("Closing %s failed\n", path);\
    }

#define SAFE_FLUSH(f, path)\
    if (fflush(f)){\
        DIE("Flushing %s failed\n", path);\
    }

struct sortpair{
    Word_t key;
    Word_t value;
};

void bderror(struct breadcrumbs *bd, char *fmt, ...);

struct sortpair *sort_judyl(const Pvoid_t judy, Word_t *num_items);

void make_path(char path[MAX_PATH_SIZE], char *fmt, ...);

int mmap_file(const char *path, struct bdfile *dst, struct breadcrumbs *bd);

uint32_t bits_needed(uint32_t max);

uint64_t parse_uint64(const char *str, const char *ctx);

void dsfmt_shuffle(uint64_t *arr, uint64_t len, uint32_t seed);

#endif /* __TRAILDB_UTIL_H__ */
