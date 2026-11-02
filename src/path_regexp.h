#ifndef TPATHEXP_PATH_REGEXP_H
#define TPATHEXP_PATH_REGEXP_H

#include <regex.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PATH_REGEXP_STRICT = 1,
    PATH_REGEXP_START = 2,
    PATH_REGEXP_END = 4,
    PATH_REGEXP_NOCASE = 8,
};

typedef struct {
    char *name;
    size_t capture_index;
} path_regexp_key_t;

typedef struct {
    regex_t regex;
    char *pattern;
    path_regexp_key_t *keys;
    size_t key_count;
    size_t capture_count;
} path_regexp_t;

int path_regexp_compile(path_regexp_t *compiled,
                        const char *path_expr,
                        int flags,
                        char *error,
                        size_t error_capacity);

void path_regexp_destroy(path_regexp_t *compiled);

int path_regexp_match(const path_regexp_t *compiled,
                      const char *path,
                      regmatch_t *matches,
                      size_t match_count);

int path_regexp_copy_match(const path_regexp_t *compiled,
                           const char *path,
                           const regmatch_t *matches,
                           size_t match_count,
                           size_t key_index,
                           char *buffer,
                           size_t buffer_capacity);

#ifdef __cplusplus
}
#endif

#endif
