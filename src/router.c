#include "router.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *expression;
    tpathexp_methods_t methods;
    path_regexp_t pattern;
    void *user_data;
} route_entry_t;

struct tpathexp_router {
    route_entry_t *routes;
    size_t count;
};

static const char *const method_names[] = {
    "GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "PATCH", "TRACE", "CONNECT"
};

static int fail(char *error, size_t capacity, const char *message) {
    if (error != NULL && capacity > 0) snprintf(error, capacity, "%s", message);
    return -1;
}

tpathexp_methods_t tpathexp_method(const char *name) {
    if (name == NULL) return 0;
    for (size_t i = 0; i < sizeof(method_names) / sizeof(method_names[0]); ++i) {
        if (strcmp(name, method_names[i]) == 0) return 1u << i;
    }
    return 0;
}

int tpathexp_allow_header(tpathexp_methods_t methods, char *buffer, size_t capacity) {
    if (buffer == NULL || capacity == 0) return -1;
    buffer[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < sizeof(method_names) / sizeof(method_names[0]); ++i) {
        if ((methods & (1u << i)) == 0) continue;
        int written = snprintf(buffer + used, capacity - used, "%s%s",
                               used == 0 ? "" : ", ", method_names[i]);
        if (written < 0 || (size_t)written >= capacity - used) {
            buffer[0] = '\0';
            return -1;
        }
        used += (size_t)written;
    }
    return 0;
}

int tpathexp_router_add(tpathexp_router_t *router, const tpathexp_route_t *route,
                       char *error, size_t error_capacity) {
    if (router == NULL || route == NULL || route->expression == NULL ||
        route->methods == 0 || (route->methods & ~TPATHEXP_ANY) != 0 ||
        (route->flags & ~(PATH_REGEXP_STRICT | PATH_REGEXP_START | PATH_REGEXP_END | PATH_REGEXP_NOCASE)) != 0) {
        return fail(error, error_capacity, "invalid route definition");
    }
    if (router->count >= SIZE_MAX / sizeof(route_entry_t)) {
        return fail(error, error_capacity, "route table is too large");
    }
    char *expression = malloc(strlen(route->expression) + 1);
    if (expression == NULL) return fail(error, error_capacity, "unable to allocate route expression");
    strcpy(expression, route->expression);
    route_entry_t *entries = realloc(router->routes, (router->count + 1) * sizeof(*entries));
    if (entries == NULL) {
        free(expression);
        return fail(error, error_capacity, "unable to grow route table");
    }
    router->routes = entries;
    route_entry_t *entry = &entries[router->count];
    if (path_regexp_compile(&entry->pattern, expression, route->flags, error, error_capacity) != 0) {
        free(expression);
        return -1;
    }
    entry->expression = expression;
    entry->methods = route->methods;
    if ((entry->methods & TPATHEXP_GET) != 0) entry->methods |= TPATHEXP_HEAD;
    entry->user_data = route->user_data;
    router->count += 1;
    return 0;
}

int tpathexp_router_create(tpathexp_router_t **out, const tpathexp_route_t *routes,
                          size_t count, char *error, size_t error_capacity) {
    if (out == NULL) return fail(error, error_capacity, "router output is required");
    *out = NULL;
    if (count != 0 && routes == NULL) return fail(error, error_capacity, "route definitions are required");
    tpathexp_router_t *router = calloc(1, sizeof(*router));
    if (router == NULL) return fail(error, error_capacity, "unable to allocate route table");
    for (size_t i = 0; i < count; ++i) {
        if (tpathexp_router_add(router, &routes[i], error, error_capacity) != 0) {
            tpathexp_router_destroy(router);
            return -1;
        }
    }
    *out = router;
    return 0;
}

void tpathexp_router_destroy(tpathexp_router_t *router) {
    if (router == NULL) return;
    for (size_t i = 0; i < router->count; ++i) {
        path_regexp_destroy(&router->routes[i].pattern);
        free(router->routes[i].expression);
    }
    free(router->routes);
    free(router);
}

void tpathexp_match_destroy(tpathexp_match_t *result) {
    if (result == NULL) return;
    for (size_t i = 0; i < result->param_count; ++i) free(result->params[i].value);
    free(result->params);
    memset(result, 0, sizeof(*result));
}

const char *tpathexp_match_param(const tpathexp_match_t *result, const char *name) {
    if (result == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < result->param_count; ++i) {
        if (strcmp(result->params[i].name, name) == 0) return result->params[i].value;
    }
    return NULL;
}

static int extract_params(const route_entry_t *entry, const char *path,
                          const regmatch_t *captures, tpathexp_match_t *result) {
    if (entry->pattern.key_count == 0) return 0;
    result->params = calloc(entry->pattern.key_count, sizeof(*result->params));
    if (result->params == NULL) return -1;
    for (size_t i = 0; i < entry->pattern.key_count; ++i) {
        regmatch_t capture = captures[entry->pattern.keys[i].capture_index];
        if (capture.rm_so < 0) continue;
        size_t length = (size_t)(capture.rm_eo - capture.rm_so);
        char *value = malloc(length + 1);
        if (value == NULL) return -1;
        memcpy(value, path + capture.rm_so, length);
        value[length] = '\0';
        result->params[result->param_count++] = (tpathexp_param_t){entry->pattern.keys[i].name, value};
    }
    return 0;
}

int tpathexp_router_match(const tpathexp_router_t *router, const char *method,
                         const char *path, tpathexp_match_t *result,
                         char *error, size_t error_capacity) {
    if (result == NULL) return fail(error, error_capacity, "match output is required");
    memset(result, 0, sizeof(*result));
    if (router == NULL || method == NULL || path == NULL) {
        return fail(error, error_capacity, "router, method and path are required");
    }
    tpathexp_methods_t requested = tpathexp_method(method);
    for (size_t i = 0; i < router->count; ++i) {
        const route_entry_t *entry = &router->routes[i];
        int matched = path_regexp_match(&entry->pattern, path, NULL, 0);
        if (matched < 0) return fail(error, error_capacity, "regular expression execution failed");
        if (!matched) continue;
        result->allowed_methods |= entry->methods;
        if ((entry->methods & requested) == 0) continue;
        size_t count = entry->pattern.capture_count + 1;
        regmatch_t *captures = calloc(count, sizeof(*captures));
        if (captures == NULL) return fail(error, error_capacity, "unable to allocate match captures");
        matched = path_regexp_match(&entry->pattern, path, captures, count);
        int extracted = matched == 1 ? extract_params(entry, path, captures, result) : -1;
        free(captures);
        if (extracted != 0) {
            tpathexp_match_destroy(result);
            return fail(error, error_capacity, "unable to extract route parameters");
        }
        result->status_code = 200;
        result->expression = entry->expression;
        result->user_data = entry->user_data;
        return 0;
    }
    result->status_code = result->allowed_methods == 0 ? 404 : 405;
    return 0;
}
