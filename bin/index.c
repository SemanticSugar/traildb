
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmph.h>

#include "discodb.h"
#include "util.h"

#define DECODE_BUF_INCREMENT 1000000
#define KEY_BUFFER_SIZE 1000000

#define MIN_NUMBER_OF_COOKIES 100

struct cookie_ctx{
    uint32_t index;
    struct breadcrumbs *db;
    char key[16];
};

void ci_dispose(void *data, char *key, cmph_uint32 l)
{ /* no-op */
}

void ci_rewind(void *data)
{
    struct cookie_ctx *ctx = (struct cookie_ctx*)data;
    ctx->index = 0;
}

int ci_read(void *data, char **p, cmph_uint32 *len)
{
    struct cookie_ctx *ctx = (struct cookie_ctx*)data;
    const char *id = bd_lookup_cookie(ctx->db, ctx->index);

    memcpy(ctx->key, id, 16);
    *p = ctx->key;
    *len = 16;

    ++ctx->index;
    return *len;
}

void create_cookie_index(const char *path, struct breadcrumbs *bd)
{
    void *data;
    cmph_config_t *config;
    cmph_t *cmph;
    FILE *out;
    uint32_t size;

    struct cookie_ctx ctx = {
        .index = 0,
        .db = bd
    };

    cmph_io_adapter_t r = {
        .data = &ctx,
        .nkeys = bd_num_cookies(bd),
        .read = ci_read,
        .dispose = ci_dispose,
        .rewind = ci_rewind
    };

    if (!(out = fopen(path, "w")))
        DIE("Could not open output file at %s\n", path);

    if (!(config = cmph_config_new(&r)))
        DIE("cmph_config failed\n");

    cmph_config_set_algo(config, CMPH_CHM);

    if (getenv("DEBUG_CMPH"))
        cmph_config_set_verbosity(config, 5);

    if (!(cmph = cmph_new(config)))
        DIE("cmph_new failed\n");

    size = cmph_packed_size(cmph);
    if (!(data = malloc(size)))
        DIE("Could not malloc a hash of %u bytes\n", size);

    cmph_pack(cmph, data);
    SAFE_WRITE(data, size, path, out);
    SAFE_CLOSE(out, path);

    cmph_destroy(cmph);
    cmph_config_destroy(config);
    free(data);
}

static void make_key(struct ddb_entry *key,
                     struct breadcrumbs *bd,
                     uint32_t field_value)
{
    static char keybuf[KEY_BUFFER_SIZE];
    const char *str = bd_lookup_value(bd, field_value);
    uint32_t idx = bd_field_index(field_value);

    key->data = keybuf;
    key->length = snprintf(keybuf, KEY_BUFFER_SIZE, "%u:%s", idx, str);

    if (key->length >= KEY_BUFFER_SIZE)
        DIE("Key too long. Increase KEY_BUFFER_SIZE\n");
}

static void add_trails(struct breadcrumbs *bd, struct ddb_cons *cons)
{
    uint32_t *buf = NULL;
    uint32_t buf_size = 0;
    uint32_t i, j;
    uint32_t num_cookies = bd_num_cookies(bd);

    for (i = 0; i < num_cookies; i++){
        const struct ddb_entry value = {.data = (const char*)&i, .length = 4};
        uint32_t n;
        Pvoid_t uniques = NULL;
        int tmp;
        Word_t field_value;

        /* decode trail - no need to edge decode since we are only interest
           in unique field values */
        while ((n = bd_trail_decode(bd, i, buf, buf_size, 1)) == buf_size){
            free(buf);
            buf_size += DECODE_BUF_INCREMENT;
            if (!(buf = malloc(buf_size * 4)))
                DIE("Could not allocate decode buffer of %u items", buf_size);
        }

        /* remove duplicates */
        for (j = 1; j < n; j++){
            /* exclude end-of-event markers */
            if (buf[j]){
                /* exclude null values */
                if (bd_field_value(buf[j]))
                    J1S(tmp, uniques, buf[j]);
            }else
                /* exclude timestamps */
                ++j;
        }

        /* add unique field values for this cookie in the index */
        J1F(tmp, uniques, field_value);
        while (tmp){
            struct ddb_entry key;
            make_key(&key, bd, field_value);
            if (ddb_cons_add(cons, &key, &value))
                DIE("Could not add key (cookie %u)\n", i);
            J1N(tmp, uniques, field_value);
        }
        J1FA(field_value, uniques);

        if (!(i & 65535))
            fprintf(stderr,
                    "%u/%u (%2.1f%%) trails indexed\n",
                    i,
                    num_cookies,
                    100. * i / num_cookies);
    }
    free(buf);
}

static void create_trail_index(const char *path, struct breadcrumbs *bd)
{
    struct ddb_cons *cons;
    uint64_t length;
    char *data;
    FILE *out;

    if (!(out = fopen(path, "w")))
        DIE("Could not open output file at %s\n", path);

    if (!(cons = ddb_cons_new()))
        DIE("Could not allocate discodb\n");

    add_trails(bd, cons);

    if (!(data = ddb_cons_finalize(cons, &length, DDB_OPT_UNIQUE_ITEMS)))
        DIE("Finalizing index failed\n");

    SAFE_WRITE(data, length, path, out);
    SAFE_CLOSE(out, path);

    ddb_cons_free(cons);
    free(data);
}

int main(int argc, char **argv)
{
    char path[MAX_PATH_SIZE];
    struct breadcrumbs *bd;
    DDB_TIMER_DEF

    if (argc < 2)
        DIE("USAGE: %s db-root\n", argv[0]);

    if (!(bd = bd_open(argv[1])))
        DIE("Could not load traildb\n");

    if (bd->error_code)
        DIE("%s\n", bd->error);

    DDB_TIMER_START
    make_path(path, "%s/trails.index", argv[1]);
    create_trail_index(path, bd);
    DDB_TIMER_END("trails.index");

    /*
    CMPH may fail for a very small number of keys. We don't need an
    index for a small number of cookies anyways, so let's not even try.
    */
    if (bd_num_cookies(bd) > MIN_NUMBER_OF_COOKIES){
        DDB_TIMER_START
        make_path(path, "%s/cookies.index", argv[1]);
        create_cookie_index(path, bd);
        DDB_TIMER_END("cookies.index");
    }

    bd_close(bd);
    return 0;
}
