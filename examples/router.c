#include "router.h"
#include <stdio.h>

int main(void) {
    const char handler_name[] = "get_user";
    const tpathexp_route_t routes[] = {
        {"/users/:id([0-9]+)", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, (void *)handler_name},
        {"/files/:path(.*)", TPATHEXP_GET, TPATHEXP_DEFAULT_FLAGS, NULL}
    };
    tpathexp_router_t *router = NULL;
    char error[512] = {0};
    if (tpathexp_router_create(&router, routes, sizeof(routes) / sizeof(routes[0]), error, sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    tpathexp_match_t match = {0};
    int rc = tpathexp_router_match(router, "GET", "/users/42", &match, error, sizeof(error));
    if (rc != 0) {
        fprintf(stderr, "%s\n", error);
    } else if (match.status_code == 200) {
        printf("%s: id=%s\n", (const char *)match.user_data, tpathexp_match_param(&match, "id"));
    }
    tpathexp_match_destroy(&match);
    tpathexp_router_destroy(router);
    return rc == 0 ? 0 : 1;
}
