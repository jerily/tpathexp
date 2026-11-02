#include "path_regexp.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} string_builder_t;

/*
 * This is a standalone, deliberately small path-to-regexp style compiler.
 */

static void path_regexp_set_error(char *error, size_t error_capacity, const char *format, ...) {
    if (error == NULL || error_capacity == 0) {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(error, error_capacity, format, args);
    va_end(args);
}

static void string_builder_free(string_builder_t *builder) {
    free(builder->data);
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
}

static int string_builder_reserve(string_builder_t *builder, size_t extra) {
    if (extra > SIZE_MAX - builder->length - 1) {
        return -1;
    }
    size_t required = builder->length + extra + 1;
    if (required <= builder->capacity) {
        return 0;
    }

    size_t next_capacity = builder->capacity == 0 ? 64 : builder->capacity;
    while (next_capacity < required) {
        if (next_capacity > SIZE_MAX / 2) {
            next_capacity = required;
            break;
        }
        next_capacity *= 2;
    }

    char *next_data = (char *) realloc(builder->data, next_capacity);
    if (next_data == NULL) {
        return -1;
    }

    builder->data = next_data;
    builder->capacity = next_capacity;
    return 0;
}

static int string_builder_append(string_builder_t *builder, const char *text, size_t length) {
    if (string_builder_reserve(builder, length) != 0) {
        return -1;
    }

    memcpy(builder->data + builder->length, text, length);
    builder->length += length;
    builder->data[builder->length] = '\0';
    return 0;
}

static int string_builder_append_char(string_builder_t *builder, char value) {
    return string_builder_append(builder, &value, 1);
}

static bool regex_literal_char_needs_escape(char value) {
    switch (value) {
        case '.':
        case '[':
        case ']':
        case '(':
        case ')':
        case '{':
        case '}':
        case '+':
        case '*':
        case '?':
        case '^':
        case '$':
        case '|':
        case '\\':
            return true;
        default:
            return false;
    }
}

static int string_builder_append_escaped_literal(string_builder_t *builder, char value) {
    if (regex_literal_char_needs_escape(value)) {
        if (string_builder_append_char(builder, '\\') != 0) {
            return -1;
        }
    }

    return string_builder_append_char(builder, value);
}

static int path_regexp_add_key(path_regexp_t *compiled,
                               const char *name,
                               size_t name_length,
                               char *error,
                               size_t error_capacity) {
    for (size_t i = 0; i < compiled->key_count; ++i) {
        if (strlen(compiled->keys[i].name) == name_length &&
            memcmp(compiled->keys[i].name, name, name_length) == 0) {
            path_regexp_set_error(error, error_capacity, "duplicate parameter name");
            return -1;
        }
    }
    path_regexp_key_t *next_keys = (path_regexp_key_t *) realloc(compiled->keys,
                                                                 sizeof(path_regexp_key_t) *
                                                                 (compiled->key_count + 1));
    if (next_keys == NULL) {
        path_regexp_set_error(error, error_capacity, "out of memory while recording route keys");
        return -1;
    }

    compiled->keys = next_keys;
    compiled->keys[compiled->key_count].name = (char *) malloc(name_length + 1);
    if (compiled->keys[compiled->key_count].name == NULL) {
        path_regexp_set_error(error, error_capacity, "out of memory while recording route keys");
        return -1;
    }

    memcpy(compiled->keys[compiled->key_count].name, name, name_length);
    compiled->keys[compiled->key_count].name[name_length] = '\0';
    compiled->keys[compiled->key_count].capture_index = ++compiled->capture_count;
    compiled->key_count += 1;
    return 0;
}

static int path_regexp_append_parameter(string_builder_t *builder,
                                        const char *pattern,
                                        size_t pattern_length,
                                        char modifier) {
    if (string_builder_append_char(builder, '(') != 0) {
        return -1;
    }

    if (string_builder_append(builder, pattern, pattern_length) != 0) {
        return -1;
    }

    if (string_builder_append_char(builder, ')') != 0) {
        return -1;
    }

    if (modifier != '\0' && string_builder_append_char(builder, modifier) != 0) {
        return -1;
    }

    return 0;
}

int path_regexp_compile(path_regexp_t *compiled,
                        const char *path_expr,
                        int flags,
                        char *error,
                        size_t error_capacity) {
    string_builder_t pattern_builder = {0};
    memset(compiled, 0, sizeof(*compiled));

    /*
     * Record capture indexes separately from key order: custom patterns may
     * contain nested capturing groups.
     */

    if ((flags & PATH_REGEXP_START) != 0 && string_builder_append_char(&pattern_builder, '^') != 0) {
        path_regexp_set_error(error, error_capacity, "out of memory while building route pattern");
        return -1;
    }

    const char *cursor = path_expr;
    while (*cursor != '\0') {
        if (*cursor == '\\') {
            cursor += 1;
            if (*cursor == '\0') {
                path_regexp_set_error(error, error_capacity, "dangling escape in path expression");
                string_builder_free(&pattern_builder);
                path_regexp_destroy(compiled);
                return -1;
            }

            if (string_builder_append_escaped_literal(&pattern_builder, *cursor) != 0) {
                path_regexp_set_error(error, error_capacity, "out of memory while building route pattern");
                string_builder_free(&pattern_builder);
                path_regexp_destroy(compiled);
                return -1;
            }

            cursor += 1;
            continue;
        }

        if (*cursor == '{' || *cursor == '}') {
            path_regexp_set_error(error,
                                  error_capacity,
                                  "brace groups are not supported in this standalone path_regexp implementation");
            string_builder_free(&pattern_builder);
            path_regexp_destroy(compiled);
            return -1;
        }

        if (*cursor == ':') {
            const char *name_start = cursor + 1;
            const char *name_cursor = name_start;
            while (*name_cursor != '\0' && (isalnum((unsigned char) *name_cursor) || *name_cursor == '_')) {
                name_cursor += 1;
            }

            if (name_cursor == name_start) {
                path_regexp_set_error(error, error_capacity, "parameter name missing after ':'");
                string_builder_free(&pattern_builder);
                path_regexp_destroy(compiled);
                return -1;
            }

            if (path_regexp_add_key(compiled,
                                    name_start,
                                    (size_t) (name_cursor - name_start),
                                    error,
                                    error_capacity) != 0) {
                string_builder_free(&pattern_builder);
                path_regexp_destroy(compiled);
                return -1;
            }

            const char *pattern_start = "[^/]+";
            size_t pattern_length = strlen(pattern_start);
            cursor = name_cursor;

            if (*cursor == '(') {
                const char *group_start = cursor + 1;
                const char *group_cursor = group_start;
                int depth = 1;

                if (*group_cursor == '?') {
                    path_regexp_set_error(error,
                                          error_capacity,
                                          "custom parameter pattern cannot start with '?' in %s",
                                          path_expr);
                    string_builder_free(&pattern_builder);
                    path_regexp_destroy(compiled);
                    return -1;
                }

                while (*group_cursor != '\0') {
                    if (*group_cursor == '\\' && group_cursor[1] != '\0') {
                        group_cursor += 2;
                        continue;
                    }

                    if (*group_cursor == '[') {
                        group_cursor += 1;
                        if (*group_cursor == '^') group_cursor += 1;
                        if (*group_cursor == ']') group_cursor += 1;
                        while (*group_cursor != '\0' && *group_cursor != ']') {
                            if (*group_cursor == '[' &&
                                (group_cursor[1] == ':' || group_cursor[1] == '.' || group_cursor[1] == '=')) {
                                char delimiter = group_cursor[1];
                                group_cursor += 2;
                                while (*group_cursor != '\0' &&
                                       !(group_cursor[0] == delimiter && group_cursor[1] == ']')) {
                                    group_cursor += 1;
                                }
                                if (*group_cursor != '\0') group_cursor += 2;
                            } else {
                                if (*group_cursor == '\\' && group_cursor[1] != '\0') group_cursor += 1;
                                group_cursor += 1;
                            }
                        }
                        if (*group_cursor == '\0') break;
                        group_cursor += 1;
                        continue;
                    }

                    if (*group_cursor == '(') {
                        depth += 1;
                        compiled->capture_count += 1;
                    } else if (*group_cursor == ')') {
                        depth -= 1;
                        if (depth == 0) {
                            break;
                        }
                    }

                    group_cursor += 1;
                }

                if (*group_cursor != ')' || depth != 0 || group_cursor == group_start) {
                    path_regexp_set_error(error, error_capacity, "unbalanced parameter pattern in %s", path_expr);
                    string_builder_free(&pattern_builder);
                    path_regexp_destroy(compiled);
                    return -1;
                }

                pattern_start = group_start;
                pattern_length = (size_t) (group_cursor - group_start);
                cursor = group_cursor + 1;
            }

            char modifier = '\0';
            if (*cursor == '?' || *cursor == '+' || *cursor == '*') {
                modifier = *cursor;
                cursor += 1;
            }

            if (path_regexp_append_parameter(&pattern_builder,
                                             pattern_start,
                                             pattern_length,
                                             modifier) != 0) {
                path_regexp_set_error(error, error_capacity, "out of memory while building route pattern");
                string_builder_free(&pattern_builder);
                path_regexp_destroy(compiled);
                return -1;
            }

            continue;
        }

        if ((*cursor == '?' || *cursor == '+' || *cursor == '*') && cursor == path_expr) {
            path_regexp_set_error(error, error_capacity, "modifier '%c' has no parameter", *cursor);
            string_builder_free(&pattern_builder);
            path_regexp_destroy(compiled);
            return -1;
        }

        if (string_builder_append_escaped_literal(&pattern_builder, *cursor) != 0) {
            path_regexp_set_error(error, error_capacity, "out of memory while building route pattern");
            string_builder_free(&pattern_builder);
            path_regexp_destroy(compiled);
            return -1;
        }

        cursor += 1;
    }

    if ((flags & PATH_REGEXP_END) != 0) {
        if ((flags & PATH_REGEXP_STRICT) == 0 && string_builder_append(&pattern_builder, "/?", 2) != 0) {
            path_regexp_set_error(error, error_capacity, "out of memory while finalizing route pattern");
            string_builder_free(&pattern_builder);
            path_regexp_destroy(compiled);
            return -1;
        }

        if (string_builder_append_char(&pattern_builder, '$') != 0) {
            path_regexp_set_error(error, error_capacity, "out of memory while finalizing route pattern");
            string_builder_free(&pattern_builder);
            path_regexp_destroy(compiled);
            return -1;
        }
    }

    if (string_builder_append(&pattern_builder, "", 0) != 0) {
        path_regexp_set_error(error, error_capacity, "out of memory while finalizing route pattern");
        string_builder_free(&pattern_builder);
        path_regexp_destroy(compiled);
        return -1;
    }
    compiled->pattern = pattern_builder.data;

    int regcomp_flags = REG_EXTENDED;
    if ((flags & PATH_REGEXP_NOCASE) != 0) {
        regcomp_flags |= REG_ICASE;
    }

    int compile_rc = regcomp(&compiled->regex, compiled->pattern, regcomp_flags);
    if (compile_rc != 0) {
        char regex_error[256];
        regerror(compile_rc, &compiled->regex, regex_error, sizeof(regex_error));
        path_regexp_set_error(error,
                              error_capacity,
                              "failed to compile route pattern '%s': %s",
                              compiled->pattern,
                              regex_error);
        free(compiled->pattern);
        compiled->pattern = NULL;
        path_regexp_destroy(compiled);
        return -1;
    }

    return 0;
}

void path_regexp_destroy(path_regexp_t *compiled) {
    if (compiled == NULL) {
        return;
    }

    if (compiled->pattern != NULL) {
        regfree(&compiled->regex);
        free(compiled->pattern);
        compiled->pattern = NULL;
    }

    for (size_t index = 0; index < compiled->key_count; ++index) {
        free(compiled->keys[index].name);
    }

    free(compiled->keys);
    compiled->keys = NULL;
    compiled->key_count = 0;
    compiled->capture_count = 0;
}

int path_regexp_match(const path_regexp_t *compiled,
                      const char *path,
                      regmatch_t *matches,
                      size_t match_count) {
    int rc = regexec(&compiled->regex, path, match_count, matches, 0);
    if (rc == 0) {
        return 1;
    }

    if (rc == REG_NOMATCH) {
        return 0;
    }

    return -1;
}

int path_regexp_copy_match(const path_regexp_t *compiled,
                           const char *path,
                           const regmatch_t *matches,
                           size_t match_count,
                           size_t key_index,
                           char *buffer,
                           size_t buffer_capacity) {
    if (key_index >= compiled->key_count ||
        compiled->keys[key_index].capture_index >= match_count || buffer_capacity == 0) {
        return -1;
    }

    regmatch_t match = matches[compiled->keys[key_index].capture_index];
    if (match.rm_so < 0 || match.rm_eo < match.rm_so) {
        return -1;
    }

    size_t length = (size_t) (match.rm_eo - match.rm_so);
    if (length >= buffer_capacity) {
        return -1;
    }

    memcpy(buffer, path + match.rm_so, length);
    buffer[length] = '\0';
    return 0;
}
