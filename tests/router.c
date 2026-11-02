#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

int main(void) {
    int specific = 1, general = 2, post = 3, head = 4;
    char mutable_expression[] = "/users/me";
    const tpathexp_route_t routes[] = {
        {"/users/:id", TPATHEXP_POST, TPATHEXP_DEFAULT_FLAGS, &post},
        {"/users/me", TPATHEXP_HEAD, TPATHEXP_DEFAULT_FLAGS, &head},
        {mutable_expression, TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, &specific},
        {"/users/:id", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, &general},
        {"/:first((ab|cd)+)/:second([0-9]+)", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, NULL},
        {"/optional/:id?", TPATHEXP_ANY, TPATHEXP_DEFAULT_FLAGS, NULL},
        {"/files/:path(.*)", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, NULL},
        {"/Case/:id", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS | PATH_REGEXP_STRICT | PATH_REGEXP_NOCASE, NULL}
    };
    char error[512] = {0};
    tpathexp_router_t *router = NULL;
    CHECK(tpathexp_router_create(&router, routes, sizeof(routes) / sizeof(routes[0]), error, sizeof(error)) == 0);
    mutable_expression[1] = 'X';
    tpathexp_match_t match = {0};
    CHECK(tpathexp_router_match(router, "GET", "/users/me", &match, error, sizeof(error)) == 0);
    CHECK(match.status_code == 200 && match.user_data == &specific);
    CHECK(strcmp(match.expression, "/users/me") == 0);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "HEAD", "/users/me", &match, error, sizeof(error)) == 0);
    CHECK(match.user_data == &head);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "HEAD", "/users/42", &match, error, sizeof(error)) == 0);
    CHECK(match.user_data == &general && strcmp(tpathexp_match_param(&match, "id"), "42") == 0);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "DELETE", "/users/42", &match, error, sizeof(error)) == 0);
    CHECK(match.status_code == 405 && match.allowed_methods == (TPATHEXP_GET | TPATHEXP_HEAD | TPATHEXP_POST));
    char allow[80];
    CHECK(tpathexp_allow_header(match.allowed_methods, allow, sizeof(allow)) == 0);
    CHECK(strcmp(allow, "GET, HEAD, POST") == 0);
    CHECK(tpathexp_allow_header(match.allowed_methods, allow, 2) == -1 && allow[0] == '\0');
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "GET", "/missing", &match, error, sizeof(error)) == 0);
    CHECK(match.status_code == 404 && match.params == NULL);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "GET", "/abcd/42", &match, error, sizeof(error)) == 0);
    CHECK(strcmp(tpathexp_match_param(&match, "first"), "abcd") == 0);
    CHECK(strcmp(tpathexp_match_param(&match, "second"), "42") == 0);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "PATCH", "/optional/", &match, error, sizeof(error)) == 0);
    CHECK(match.status_code == 200 && tpathexp_match_param(&match, "id") == NULL);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "GET", "/files/", &match, error, sizeof(error)) == 0);
    CHECK(strcmp(tpathexp_match_param(&match, "path"), "") == 0);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "GET", "/case/42", &match, error, sizeof(error)) == 0 && match.status_code == 200);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_match(router, "GET", "/case/42/", &match, error, sizeof(error)) == 0 && match.status_code == 404);
    tpathexp_match_destroy(&match);
    const tpathexp_route_t invalid = {"/:id/:id", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, NULL};
    CHECK(tpathexp_router_add(router, &invalid, error, sizeof(error)) == -1);
    CHECK(tpathexp_router_match(router, "POST", "/users/99", &match, error, sizeof(error)) == 0 && match.user_data == &post);
    tpathexp_match_destroy(&match);
    tpathexp_router_destroy(router);
    tpathexp_route_t broken[] = {routes[0], invalid};
    CHECK(tpathexp_router_create(&router, broken, 2, error, sizeof(error)) == -1 && router == NULL);
    CHECK(tpathexp_router_create(&router, NULL, 0, error, sizeof(error)) == 0);
    CHECK(tpathexp_router_match(router, "GET", "/", &match, error, sizeof(error)) == 0 && match.status_code == 404);
    tpathexp_match_destroy(&match);
    CHECK(tpathexp_router_add(router, NULL, error, sizeof(error)) == -1);
    CHECK(tpathexp_router_match(router, NULL, "/", &match, error, sizeof(error)) == -1);
    tpathexp_match_destroy(&match);
    tpathexp_router_destroy(router);
    puts("native route table checks passed");
    return 0;
}
