#ifndef TPATHEXP_ROUTER_H
#define TPATHEXP_ROUTER_H

#include "path_regexp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tpathexp_router tpathexp_router_t;
typedef unsigned int tpathexp_methods_t;

enum {
    TPATHEXP_GET = 1u << 0,
    TPATHEXP_HEAD = 1u << 1,
    TPATHEXP_POST = 1u << 2,
    TPATHEXP_PUT = 1u << 3,
    TPATHEXP_DELETE = 1u << 4,
    TPATHEXP_OPTIONS = 1u << 5,
    TPATHEXP_PATCH = 1u << 6,
    TPATHEXP_TRACE = 1u << 7,
    TPATHEXP_CONNECT = 1u << 8,
    TPATHEXP_ANY = (1u << 9) - 1,
    TPATHEXP_DEFAULT_FLAGS = PATH_REGEXP_START | PATH_REGEXP_END
};

typedef struct {
    const char *expression;
    tpathexp_methods_t methods;
    int flags;
    void *user_data;
} tpathexp_route_t;

typedef struct {
    const char *name;
    char *value;
} tpathexp_param_t;

typedef struct {
    int status_code;
    tpathexp_methods_t allowed_methods;
    const char *expression;
    void *user_data;
    tpathexp_param_t *params;
    size_t param_count;
} tpathexp_match_t;

/* Expressions are copied; user_data remains owned by the caller. */
int tpathexp_router_create(tpathexp_router_t **out, const tpathexp_route_t *routes,
                          size_t count, char *error, size_t error_capacity);
int tpathexp_router_add(tpathexp_router_t *router, const tpathexp_route_t *route,
                       char *error, size_t error_capacity);
void tpathexp_router_destroy(tpathexp_router_t *router);

/* Use a fresh or destroyed result. Destroy it before destroying the router.
 * Names, expression and user_data are borrowed; parameter values are owned.
 * An immutable router may be matched concurrently with separate results.
 */
int tpathexp_router_match(const tpathexp_router_t *router, const char *method,
                         const char *path, tpathexp_match_t *result,
                         char *error, size_t error_capacity);
void tpathexp_match_destroy(tpathexp_match_t *result);
const char *tpathexp_match_param(const tpathexp_match_t *result, const char *name);
tpathexp_methods_t tpathexp_method(const char *name);
int tpathexp_allow_header(tpathexp_methods_t methods, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
