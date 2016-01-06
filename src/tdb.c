#define _GNU_SOURCE /* for getline() */

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if 0
#ifdef ENABLE_UUID_INDEX
#include <cmph.h>
#endif
#endif

#include "tdb_internal.h"
#include "tdb_error.h"
#include "tdb_io.h"
#include "tdb_huffman.h"

int tdb_mmap(const char *path, struct tdb_file *dst)
{
    int fd, ret = 0;
    struct stat stats;

    if ((fd = open(path, O_RDONLY)) == -1)
        return -1;

    if (fstat(fd, &stats)){
        ret = -1;
        goto done;
    }

    dst->size = (uint64_t)stats.st_size;
    dst->data = MAP_FAILED;

    if (dst->size > 0)
        dst->data = mmap(NULL, dst->size, PROT_READ, MAP_SHARED, fd, 0);

    if (dst->data == MAP_FAILED){
        ret = -1;
        goto done;
    }

done:
    close(fd);
    return ret;
}

void tdb_lexicon_read(const tdb *db, tdb_field field, struct tdb_lexicon *lex)
{
    lex->version = db->version;
    lex->data = db->lexicons[field - 1].data;
    memcpy(&lex->size, lex->data, 4);
    lex->toc = (const uint32_t*)&lex->data[4];
}

const char *tdb_lexicon_get(const struct tdb_lexicon *lex,
                            tdb_val i,
                            uint64_t *length)
{
    if (lex->version == TDB_VERSION_V0){
        /* backwards compatibility with 0-terminated strings in v0 */
        *length = (uint64_t)strlen(&lex->data[lex->toc[i]]);
    }else
        *length = lex->toc[i + 1] - lex->toc[i];
    return &lex->data[lex->toc[i]];
}

static int tdb_fields_open(tdb *db, const char *root, char *path)
{
    FILE *f = NULL;
    char *line = NULL;
    size_t n = 0;
    tdb_field i, num_ofields = 0;
    int ret = 0;

    TDB_PATH(path, "%s/fields", root);
    if (!(f = fopen(path, "r")))
        return TDB_ERR_INVALID_FIELDS_FILE;

    while (getline(&line, &n, f) != -1)
        ++num_ofields;
    db->num_fields = num_ofields + 1U;

    if (!feof(f)){
        /* we can get here if malloc fails inside getline() */
        ret = TDB_ERR_NOMEM;
        goto done;
    }

    if (!(db->field_names = calloc(db->num_fields, sizeof(char*)))){
        ret = TDB_ERR_NOMEM;
        goto done;
    }

    if (num_ofields){
        if (!(db->lexicons = calloc(num_ofields, sizeof(struct tdb_file)))){
            ret = TDB_ERR_NOMEM;
            goto done;
        }
    }else
        db->lexicons = NULL;

    if (!(db->previous_items = calloc(db->num_fields, sizeof(tdb_item)))){
        ret = TDB_ERR_NOMEM;
        goto done;
    }

    rewind(f);

    db->field_names[0] = "time";

    for (i = 1; getline(&line, &n, f) != -1 && i < db->num_fields; i++){

        line[strlen(line) - 1] = 0;

        /* let's be paranoid and sanity check the fieldname again */
        if (is_fieldname_invalid(line)){
            ret = TDB_ERR_INVALID_FIELDS_FILE;
            goto done;
        }

        if (!(db->field_names[i] = strdup(line))){
            ret = TDB_ERR_NOMEM;
            goto done;
        }

        TDB_PATH(path, "%s/lexicon.%s", root, line);
        if (tdb_mmap(path, &db->lexicons[i - 1])){
            ret = TDB_ERR_INVALID_LEXICON_FILE;
            goto done;
        }
    }

    if (i != db->num_fields){
        ret = TDB_ERR_INVALID_FIELDS_FILE;
        goto done;
    }

done:
    free(line);
    if (f)
        fclose(f);
    return ret;
}

static int init_field_stats(tdb *db)
{
    uint64_t *field_cardinalities = NULL;
    tdb_field i;
    int ret = 0;

    if (db->num_fields > 1){
        if (!(field_cardinalities = calloc(db->num_fields - 1, 8)))
            return TDB_ERR_NOMEM;
    }

    for (i = 1; i < db->num_fields; i++){
        struct tdb_lexicon lex;
        tdb_lexicon_read(db, i, &lex);
        field_cardinalities[i - 1] = lex.size;
    }

    if (!(db->field_stats = huff_field_stats(field_cardinalities,
                                             db->num_fields,
                                             db->max_timestamp_delta)))
        ret = TDB_ERR_NOMEM;

    free(field_cardinalities);
    return ret;
}

static int read_version(tdb *db, const char *path)
{
    FILE *f;
    int ret = 0;

    if (!(f = fopen(path, "r")))
        return TDB_ERR_INVALID_VERSION_FILE;

    if (fscanf(f, "%"PRIu64, &db->version) != 1)
        ret = TDB_ERR_INVALID_VERSION_FILE;
    else if (db->version > TDB_VERSION_LATEST)
        ret = TDB_ERR_INCOMPATIBLE_VERSION;

    fclose(f);
    return ret;
}

static int read_info(tdb *db, const char *path)
{
    FILE *f;
    int ret = 0;

    if (!(f = fopen(path, "r")))
        return TDB_ERR_INVALID_INFO_FILE;

    if (fscanf(f,
               "%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64,
               &db->num_trails,
               &db->num_events,
               &db->min_timestamp,
               &db->max_timestamp,
               &db->max_timestamp_delta) != 5)
        ret = TDB_ERR_INVALID_INFO_FILE;
    fclose(f);
    return ret;
}

tdb *tdb_init(void)
{
    return calloc(1, sizeof(tdb));
}

int tdb_open(tdb *db, const char *root)
{
    char path[TDB_MAX_PATH_SIZE];
    int ret = 0;

    /*
    by handling the "db == NULL" case here gracefully, we allow the return
    value of tdb_init() to be used unchecked like here:

    int err;
    tdb *db = tdb_init();
    if ((err = tdb_open(db, path)))
        printf("Opening tbd failed: %s", tdb_error(err));
    */
    if (!db)
        return TDB_ERR_HANDLE_IS_NULL;

    if (db->num_fields)
        return TDB_ERR_HANDLE_ALREADY_OPENED;

    TDB_PATH(path, "%s/info", root);
    if ((ret = read_info(db, path)))
        goto done;

    TDB_PATH(path, "%s/version", root);
    if (access(path, F_OK))
        db->version = TDB_VERSION_V0;
    else if ((ret = read_version(db, path)))
        goto done;

    if ((ret = tdb_fields_open(db, root, path)))
        goto done;

    if ((ret = init_field_stats(db)))
        goto done;

    if (db->num_trails) {
        /* backwards compatibility: UUIDs used to be called cookies */
        TDB_PATH(path, "%s/cookies", root);
        if (access(path, F_OK))
            TDB_PATH(path, "%s/uuids", root);
        if (tdb_mmap(path, &db->uuids)){
            ret = TDB_ERR_INVALID_UUIDS_FILE;
            goto done;
        }

        #if 0
        /* TODO deprecate .index */
        tdb_path(path, "%s/cookies.index", root);
        if (access(path, F_OK))
            tdb_path(path, "%s/uuids.index", root);
        if (tdb_mmap(path, &db->uuid_index, db))
            db->uuid_index.data = NULL;
        #endif

        TDB_PATH(path, "%s/trails.codebook", root);
        if (tdb_mmap(path, &db->codebook)){
            ret = TDB_ERR_INVALID_CODEBOOK_FILE;
            goto done;
        }
        if (db->version == TDB_VERSION_V0)
            if ((ret = huff_convert_v0_codebook(&db->codebook)))
                goto done;

        TDB_PATH(path, "%s/trails.data", root);
        if (tdb_mmap(path, &db->trails)){
            ret = TDB_ERR_INVALID_TRAILS_FILE;
            goto done;
        }

        TDB_PATH(path, "%s/trails.toc", root);
        if (access(path, F_OK)) // backwards compat
            TDB_PATH(path, "%s/trails.data", root);
        if (tdb_mmap(path, &db->toc)){
            ret = TDB_ERR_INVALID_TRAILS_FILE;
            goto done;
        }
    }
done:
    return ret;
}

void tdb_willneed(const tdb *db)
{
    if (db){
        tdb_field i;
        for (i = 0; i < db->num_fields - 1; i++)
            madvise(db->lexicons[i].data,
                    db->lexicons[i].size,
                    MADV_WILLNEED);

        madvise(db->uuids.data, db->uuids.size, MADV_WILLNEED);
        madvise(db->codebook.data, db->codebook.size, MADV_WILLNEED);
        madvise(db->trails.data, db->trails.size, MADV_WILLNEED);
        madvise(db->toc.data, db->toc.size, MADV_WILLNEED);
    }
}

void tdb_dontneed(const tdb *db)
{
    if (db){
        tdb_field i;
        for (i = 0; i < db->num_fields - 1; i++)
            madvise(db->lexicons[i].data,
                    db->lexicons[i].size,
                    MADV_DONTNEED);

        madvise(db->uuids.data, db->uuids.size, MADV_DONTNEED);
        madvise(db->codebook.data, db->codebook.size, MADV_DONTNEED);
        madvise(db->trails.data, db->trails.size, MADV_DONTNEED);
        madvise(db->toc.data, db->toc.size, MADV_DONTNEED);
    }
}

void tdb_close(tdb *db)
{
    if (db){
        tdb_field i;
        for (i = 0; i < db->num_fields - 1; i++){
            free(db->field_names[i + 1]);
            munmap(db->lexicons[i].data, db->lexicons[i].size);
        }

        munmap(db->uuids.data, db->uuids.size);
        munmap(db->codebook.data, db->codebook.size);
        munmap(db->trails.data, db->trails.size);
        munmap(db->toc.data, db->toc.size);

        free(db->lexicons);
        free(db->previous_items);
        free(db->field_names);
        free(db->field_stats);
        free(db->filter);
        free(db);
    }
}

uint64_t tdb_lexicon_size(const tdb *db, tdb_field field)
{
    if (field == 0 || field >= db->num_fields)
        return 0;
    else{
        struct tdb_lexicon lex;
        tdb_lexicon_read(db, field, &lex);
        /* +1 refers to the implicit NULL value (empty string) */
        return lex.size + 1;
    }
}

int tdb_get_field(const tdb *db, const char *field_name, tdb_field *field)
{
    tdb_field i;
    for (i = 0; i < db->num_fields; i++)
        if (!strcmp(field_name, db->field_names[i])){
            *field = i;
            return 0;
        }
    return TDB_ERR_UNKNOWN_FIELD;
}

const char *tdb_get_field_name(const tdb *db, tdb_field field)
{
    if (field < db->num_fields)
        return db->field_names[field];
    return NULL;
}

tdb_item tdb_get_item(const tdb *db,
                      tdb_field field,
                      const char *value,
                      uint64_t value_length)
{
    if (!value_length)
        /* NULL value for this field */
        return field;
    else if (field == 0 || field >= db->num_fields)
        return 0;
    else{
        struct tdb_lexicon lex;
        tdb_val i;
        tdb_lexicon_read(db, field, &lex);

        for (i = 0; i < lex.size; i++){
            uint64_t length;
            const char *token = tdb_lexicon_get(&lex, i, &length);
            if (length == value_length && !memcmp(&token, value, length))
                return field | ((i + 1) << 8);
        }
        return 0;
    }
}

const char *tdb_get_value(const tdb *db,
                          tdb_field field,
                          tdb_val val,
                          uint64_t *value_length)
{
    if (field == 0 || field >= db->num_fields)
        return NULL;
    else if (!val){
        /* a valid NULL value for a valid field */
        *value_length = 0;
        return "";
    }else{
        struct tdb_lexicon lex;
        tdb_lexicon_read(db, field, &lex);
        if ((val - 1) < lex.size)
            return tdb_lexicon_get(&lex, val - 1, value_length);
        else
            return NULL;
    }
}

const char *tdb_get_item_value(const tdb *db,
                               tdb_item item,
                               uint64_t *value_length)
{
    return tdb_get_value(db,
                         tdb_item_field(item),
                         tdb_item_val(item),
                         value_length);
}

const uint8_t *tdb_get_uuid(const tdb *db, uint64_t trail_id)
{
    if (trail_id < db->num_trails)
        return (const uint8_t *)&db->uuids.data[trail_id * 16];
    return NULL;
}

int64_t tdb_get_trail_id(const tdb *db, const uint8_t *uuid)
{
    /* TODO implement binary search */
    /* TODO remove UUID index */
    uint64_t i;
#ifdef ENABLE_UUID_INDEX
    /* (void*) cast is horrible below. I don't know why cmph_search_packed
       can't have a const modifier. This will segfault loudly if cmph tries to
       modify the read-only mmap'ed uuid_index. */
    if (db->uuid_index.data){
        i = cmph_search_packed((void*)db->uuid_index.data,
                               (const char*)uuid,
                               16);

        if (i < db->num_trails){
            if (!memcmp(tdb_get_uuid(db, i), uuid, 16))
                return (int64_t)i;
        }
        return -1;
    }
#endif
    for (i = 0; i < db->num_trails; i++)
        if (!memcmp(tdb_get_uuid(db, i), uuid, 16))
            return (int64_t)i;
    return TDB_ERR_UNKNOWN_UUID;
}

/* TODO obsolete with binary search */
int tdb_has_uuid_index(const tdb *db __attribute__((unused)))
{
#ifdef ENABLE_UUID_INDEX
    return db->uuid_index.data ? 1 : 0;
#else
    return 0;
#endif
}

const char *tdb_error(int errcode)
{
    /* TODO implement this */
    switch (errcode){
        default:
            return "Unknown error";
    }
}

uint64_t tdb_num_trails(const tdb *db)
{
    return db->num_trails;
}

uint64_t tdb_num_events(const tdb *db)
{
    return db->num_events;
}

uint64_t tdb_num_fields(const tdb *db)
{
    return db->num_fields;
}

uint64_t tdb_min_timestamp(const tdb *db)
{
    return db->min_timestamp;
}

uint64_t tdb_max_timestamp(const tdb *db)
{
    return db->max_timestamp;
}

uint64_t tdb_version(const tdb *db)
{
    return db->version;
}

int tdb_set_filter(tdb *db, const tdb_item *filter, uint64_t filter_len)
{
    /* TODO check that filter is valid: sum(clauses) < filter_len */
    free(db->filter);
    if (filter){
        if (!(db->filter = malloc(filter_len * sizeof(tdb_item))))
            return TDB_ERR_NOMEM;
        memcpy(db->filter, filter, filter_len * sizeof(tdb_item));
        db->filter_len = filter_len;
    }else{
        db->filter = NULL;
        db->filter_len = 0;
    }
    return 0;
}

const tdb_item *tdb_get_filter(const tdb *db, uint64_t *filter_len)
{
    *filter_len = db->filter_len;
    return db->filter;
}

