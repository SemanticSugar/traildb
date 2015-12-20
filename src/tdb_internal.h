
#ifndef __TDB_INTERNAL_H__
#define __TDB_INTERNAL_H__

#include <stdint.h>

#include <Judy.h>

#include "traildb.h"
#include "arena.h"

#include "ddb_profile.h"
#define TDB_TIMER_DEF   DDB_TIMER_DEF
#define TDB_TIMER_START DDB_TIMER_START
#define TDB_TIMER_END   DDB_TIMER_END

typedef struct {
    uint64_t item_zero;
    uint32_t num_items;
    uint32_t timestamp;
    uint64_t prev_event_idx;
} tdb_cons_event;

typedef struct {
    uint64_t item_zero;
    uint32_t num_items;
    uint32_t timestamp;
    uint64_t cookie_id;
} tdb_event;

struct _tdb_cons {
    char *root;
    char *ofield_names;
    struct arena events;
    struct arena items;
    uint32_t min_timestamp;
    uint32_t max_timestamp;
    uint32_t max_timedelta;
    uint64_t num_cookies;
    uint64_t num_events;
    uint32_t num_ofields;
    uint64_t *cookie_pointers;
    Pvoid_t cookie_index;
    struct judy_str_map *lexicons;
    //uint32_t **lexicon_maps;
    char tempfile[TDB_MAX_PATH_SIZE];
    //uint8_t overflow_str[TDB_MAX_VALUE_SIZE];
};

struct tdb_file {
    const char *data;
    uint64_t size;
};

struct tdb_lexicon {
    uint32_t size;
    const uint32_t *toc;
    const char *data;
};

struct _tdb {
    uint32_t min_timestamp;
    uint32_t max_timestamp;
    uint32_t max_timestamp_delta;
    uint64_t num_cookies;
    uint64_t num_events;
    uint32_t num_fields;
    uint32_t *previous_items;

    struct tdb_file cookies;
    struct tdb_file cookie_index;
    struct tdb_file codebook;
    struct tdb_file trails;
    struct tdb_file toc;
    struct tdb_file *lexicons;

    const char **field_names;
    struct field_stats *field_stats;

    uint32_t *filter;
    uint32_t filter_len;

    int error_code;
    char error[TDB_MAX_ERROR_SIZE];
};

#if 0
struct _tdb_split {
    tdb_cons **cons;
    tdb_fold_fn split_fn;
    unsigned int num_parts;
    char *values;
};
#endif

void tdb_lexicon_read(const tdb *db, tdb_field field, struct tdb_lexicon *lex);

const char *tdb_lexicon_get(const struct tdb_lexicon *lex,
                            uint32_t i,
                            uint32_t *length);

void tdb_err(tdb *db, char *fmt, ...);
void tdb_path(char path[TDB_MAX_PATH_SIZE], char *fmt, ...);
int tdb_mmap(const char *path, struct tdb_file *dst, tdb *db);

void tdb_encode(tdb_cons *cons, tdb_item *items);

uint32_t edge_encode_items(const tdb_item *items,
                           uint32_t **encoded,
                           uint32_t *encoded_size,
                           tdb_item *prev_items,
                           const tdb_event *ev);

#endif /* __TDB_INTERNAL_H__ */
