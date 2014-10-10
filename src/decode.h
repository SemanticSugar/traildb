
#ifndef __TRAILDB_DECODE_H__
#define __TRAILDB_DECODE_H__

#include <stdint.h>

struct lexicon{
    uint32_t size;
    const uint32_t *toc;
    const char *data;
};

struct breadcrumbs *bd_open(const char *root);
void bd_close(struct breadcrumbs *bd);

int open_lexicon(const struct breadcrumbs *bd,
                 struct lexicon *lex,
                 uint32_t field);

uint8_t bd_field_value(uint32_t value);
uint8_t bd_field_index(uint32_t value);
int bd_lookup_field_index(const struct breadcrumbs *bd, const char *field_name);

const char *bd_lookup_value(const struct breadcrumbs *bd,
                            uint32_t value);

const char *bd_lookup_value2(const struct breadcrumbs *bd,
                            uint32_t field,
                            uint32_t index);

uint32_t bd_lookup_token(const struct breadcrumbs *bd,
                         const char *token,
                         uint32_t field);

const char *bd_lookup_cookie(const struct breadcrumbs *bd,
                             uint32_t cookie_index);

int bd_lexicon_size(const struct breadcrumbs *bd,
                    uint32_t field,
                    uint32_t *size);

/* Returns the row index (int) if the given cookie is found, -1 if not,
or -2 if cookie_index is disabled. */
int64_t bd_rlookup_cookie(const struct breadcrumbs *bd, const uint8_t key[16]);

uint32_t bd_trail_decode(struct breadcrumbs *bd,
                         uint32_t trail_index,
                         uint32_t *dst,
                         uint32_t dst_size,
                         int raw_values);

uint32_t bd_trail_value_freqs(const struct breadcrumbs *bd,
                              uint32_t *trail_indices,
                              uint32_t num_trail_indices,
                              uint32_t *dst_values,
                              uint32_t *dst_freqs,
                              uint32_t dst_size);

const char *bd_error(const struct breadcrumbs *bd);

/* Checks if the cookie index is enabled in advance.
Returns 1 if the db has a cookie index. */
int bd_has_cookie_index(const struct breadcrumbs *bd);

uint32_t bd_num_cookies(const struct breadcrumbs *bd);
uint32_t bd_num_loglines(const struct breadcrumbs *bd);
uint32_t bd_num_fields(const struct breadcrumbs *bd);
uint32_t bd_min_timestamp(const struct breadcrumbs *bd);
uint32_t bd_max_timestamp(const struct breadcrumbs *bd);

#endif /* __TRAILDB_DECODE_H__ */
