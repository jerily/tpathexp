#include "tpathexpInt.h"
#include "router.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    tpathexp_router_t *table;
    Tcl_Obj *handlers;
    Tcl_Command token;
} router_command_t;

static int dict_get(Tcl_Interp *interp, Tcl_Obj *dict, const char *name,
                    int required, Tcl_Obj **value) {
    Tcl_Obj *key = Tcl_NewStringObj(name, -1);
    Tcl_IncrRefCount(key);
    int rc = Tcl_DictObjGet(interp, dict, key, value);
    Tcl_DecrRefCount(key);
    if (rc == TCL_OK && required && *value == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing required field: %s", name));
        Tcl_SetErrorCode(interp, "TPATHEXP", "FIELD", name, NULL);
        return TCL_ERROR;
    }
    return rc;
}

static void dict_put(Tcl_Obj *dict, const char *name, Tcl_Obj *value) {
    Tcl_DictObjPut(NULL, dict, Tcl_NewStringObj(name, -1), value);
}

static Tcl_Obj *params_dict(const tpathexp_match_t *match) {
    Tcl_Obj *params = Tcl_NewDictObj();
    for (size_t i = 0; i < match->param_count; ++i) {
        dict_put(params, match->params[i].name, Tcl_NewStringObj(match->params[i].value, -1));
    }
    return params;
}

static int match_route(router_command_t *router, Tcl_Interp *interp,
                       Tcl_Obj *method_obj, Tcl_Obj *path_obj, tpathexp_match_t *match) {
    const char *method = Tpathexp_Text(interp, method_obj);
    if (method == NULL) return TCL_ERROR;
    const char *path = Tpathexp_Text(interp, path_obj);
    if (path == NULL) return TCL_ERROR;
    char error[512] = {0};
    if (tpathexp_router_match(router->table, method, path, match, error, sizeof(error)) != 0) {
        return Tpathexp_Error(interp, "MATCH", error);
    }
    return TCL_OK;
}

static int add_route(router_command_t *router, Tcl_Interp *interp,
                     int objc, Tcl_Obj *const objv[]) {
    Tcl_Size count;
    Tcl_Obj **methods;
    if (Tcl_ListObjGetElements(interp, objv[2], &count, &methods) != TCL_OK) return TCL_ERROR;
    if (count == 0) return Tpathexp_Error(interp, "METHOD", "at least one method is required");
    tpathexp_methods_t mask = 0;
    if (count == 1 && strcmp(Tcl_GetString(methods[0]), "ANY") == 0) {
        mask = TPATHEXP_ANY;
    } else {
        for (Tcl_Size i = 0; i < count; ++i) {
            const char *name = Tpathexp_Text(interp, methods[i]);
            if (name == NULL) return TCL_ERROR;
            tpathexp_methods_t method = tpathexp_method(name);
            if (method == 0) return Tpathexp_Error(interp, "METHOD", "unsupported route method");
            mask |= method;
        }
    }
    if (Tcl_ListObjLength(interp, objv[4], &count) != TCL_OK) return TCL_ERROR;
    if (count == 0) return Tpathexp_Error(interp, "HANDLER", "handler must be a nonempty command prefix");
    int flags;
    if (Tpathexp_Flags(interp, 5, objc, objv, &flags) != TCL_OK) return TCL_ERROR;
    const char *expression = Tpathexp_Text(interp, objv[3]);
    if (expression == NULL) return TCL_ERROR;
    tpathexp_route_t route = {expression, mask, flags, objv[4]};
    char error[512] = {0};
    if (tpathexp_router_add(router->table, &route, error, sizeof(error)) != 0) {
        return Tpathexp_Error(interp, "COMPILE", error);
    }
    Tcl_ListObjAppendElement(interp, router->handlers, objv[4]);
    Tcl_Obj *name = Tcl_NewObj();
    Tcl_GetCommandFullName(interp, router->token, name);
    Tcl_SetObjResult(interp, name);
    return TCL_OK;
}

static int match_command(router_command_t *router, Tcl_Interp *interp,
                         Tcl_Obj *method, Tcl_Obj *path) {
    tpathexp_match_t match = {0};
    if (match_route(router, interp, method, path, &match) != TCL_OK) return TCL_ERROR;
    Tcl_Obj *result = Tcl_NewDictObj();
    dict_put(result, "statusCode", Tcl_NewIntObj(match.status_code));
    if (match.status_code == 200) {
        dict_put(result, "handler", match.user_data);
        dict_put(result, "expression", Tcl_NewStringObj(match.expression, -1));
        dict_put(result, "pathParameters", params_dict(&match));
    } else if (match.status_code == 405) {
        char allow[80];
        tpathexp_allow_header(match.allowed_methods, allow, sizeof(allow));
        dict_put(result, "allow", Tcl_NewStringObj(allow, -1));
    }
    tpathexp_match_destroy(&match);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int normalize_response(Tcl_Interp *interp, Tcl_Obj *response, int head) {
    Tcl_Obj *status, *body, *headers, *encoded;
    if (dict_get(interp, response, "statusCode", 1, &status) != TCL_OK ||
        dict_get(interp, response, "body", 1, &body) != TCL_OK ||
        dict_get(interp, response, "headers", 0, &headers) != TCL_OK ||
        dict_get(interp, response, "isBase64Encoded", 0, &encoded) != TCL_OK) return TCL_ERROR;
    int code, boolean;
    if (Tcl_GetIntFromObj(interp, status, &code) != TCL_OK) return TCL_ERROR;
    if (code < 100 || code > 599) {
        return Tpathexp_Error(interp, "RESPONSE", "handler statusCode must be an integer from 100 to 599");
    }
    Tcl_Size size;
    if (headers != NULL && Tcl_DictObjSize(interp, headers, &size) != TCL_OK) return TCL_ERROR;
    if (encoded != NULL && Tcl_GetBooleanFromObj(interp, encoded, &boolean) != TCL_OK) return TCL_ERROR;
    if (headers == NULL) dict_put(response, "headers", Tcl_NewDictObj());
    if (encoded == NULL) dict_put(response, "isBase64Encoded", Tcl_NewStringObj("false", -1));
    if (head) dict_put(response, "body", Tcl_NewObj());
    Tcl_SetObjResult(interp, response);
    return TCL_OK;
}

static int dispatch(router_command_t *router, Tcl_Interp *interp,
                    Tcl_Obj *request, Tcl_Obj *context) {
    Tcl_Obj *method, *path;
    if (dict_get(interp, request, "method", 1, &method) != TCL_OK ||
        dict_get(interp, request, "path", 1, &path) != TCL_OK) return TCL_ERROR;
    int head = strcmp(Tcl_GetString(method), "HEAD") == 0;
    tpathexp_match_t match = {0};
    if (match_route(router, interp, method, path, &match) != TCL_OK) return TCL_ERROR;
    Tcl_Obj *response;
    if (match.status_code == 200) {
        Tcl_Obj *params = params_dict(&match);
        Tcl_Obj *handler_request = Tcl_DuplicateObj(request);
        Tcl_Obj *command = Tcl_DuplicateObj(match.user_data);
        Tcl_IncrRefCount(command);
        dict_put(handler_request, "pathParameters", params);
        Tcl_ListObjAppendElement(interp, command, handler_request);
        Tcl_ListObjAppendElement(interp, command, context);
        Tcl_ListObjAppendElement(interp, command, params);
        tpathexp_match_destroy(&match);
        /* Handlers may delete this router or recursively dispatch requests. */
        int rc = Tcl_EvalObjEx(interp, command, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
        Tcl_DecrRefCount(command);
        if (rc != TCL_OK) return rc;
        response = Tcl_DuplicateObj(Tcl_GetObjResult(interp));
    } else {
        response = Tcl_NewDictObj();
        Tcl_Obj *headers = Tcl_NewDictObj();
        dict_put(headers, "content-type", Tcl_NewStringObj("text/plain; charset=utf-8", -1));
        if (match.status_code == 405) {
            char allow[80];
            tpathexp_allow_header(match.allowed_methods, allow, sizeof(allow));
            dict_put(headers, "Allow", Tcl_NewStringObj(allow, -1));
        }
        dict_put(response, "statusCode", Tcl_NewIntObj(match.status_code));
        dict_put(response, "headers", headers);
        dict_put(response, "body", Tcl_NewStringObj(match.status_code == 404 ? "not found\n" : "method not allowed\n", -1));
        tpathexp_match_destroy(&match);
    }
    Tcl_IncrRefCount(response);
    int rc = normalize_response(interp, response, head);
    Tcl_DecrRefCount(response);
    return rc;
}

static int lambda_dispatch(router_command_t *router, Tcl_Interp *interp,
                           Tcl_Obj *event, Tcl_Obj *context) {
    Tcl_Obj *version, *method, *path;
    if (dict_get(interp, event, "version", 0, &version) != TCL_OK) return TCL_ERROR;
    if (version != NULL && strcmp(Tcl_GetString(version), "2.0") == 0) {
        Tcl_Obj *request_context, *http;
        if (dict_get(interp, event, "requestContext", 1, &request_context) != TCL_OK ||
            dict_get(interp, request_context, "http", 1, &http) != TCL_OK ||
            dict_get(interp, http, "method", 1, &method) != TCL_OK ||
            dict_get(interp, event, "rawPath", 1, &path) != TCL_OK) return TCL_ERROR;
    } else if (version == NULL || strcmp(Tcl_GetString(version), "1.0") == 0) {
        if (dict_get(interp, event, "httpMethod", 1, &method) != TCL_OK ||
            dict_get(interp, event, "path", 1, &path) != TCL_OK) return TCL_ERROR;
    } else {
        return Tpathexp_Error(interp, "EVENT", "unsupported Lambda HTTP payload version");
    }
    Tcl_Obj *request = Tcl_NewDictObj();
    Tcl_IncrRefCount(request);
    dict_put(request, "method", method);
    dict_put(request, "path", path);
    dict_put(request, "event", event);
    static const char *const fields[] = {
        "headers", "body", "isBase64Encoded", "queryStringParameters", "multiValueHeaders",
        "multiValueQueryStringParameters", "cookies", "rawQueryString", "requestContext", NULL
    };
    for (size_t i = 0; fields[i] != NULL; ++i) {
        Tcl_Obj *value;
        dict_get(interp, event, fields[i], 0, &value);
        if (value != NULL) dict_put(request, fields[i], value);
    }
    int rc = dispatch(router, interp, request, context);
    Tcl_DecrRefCount(request);
    return rc;
}

static void delete_router(ClientData data) {
    router_command_t *router = data;
    tpathexp_router_destroy(router->table);
    Tcl_DecrRefCount(router->handlers);
    free(router);
}

static int router_command(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    static const char *const commands[] = {"add", "match", "dispatch", "lambda", "destroy", NULL};
    static const char *const arguments[] = {
        "methods expression handler ?options?", "method path", "request ?context?", "event ?context?", ""
    };
    int index;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "add|match|dispatch|lambda|destroy ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], commands, "subcommand", TCL_EXACT, &index) != TCL_OK) return TCL_ERROR;
    if ((index == 0 && objc < 5) || (index == 1 && objc != 4) ||
        ((index == 2 || index == 3) && objc != 3 && objc != 4) || (index == 4 && objc != 2)) {
        Tcl_WrongNumArgs(interp, 2, objv, arguments[index]);
        return TCL_ERROR;
    }
    router_command_t *router = data;
    if (index == 0) return add_route(router, interp, objc, objv);
    if (index == 1) return match_command(router, interp, objv[2], objv[3]);
    if (index == 4) {
        Tcl_DeleteCommandFromToken(interp, router->token);
        return TCL_OK;
    }
    Tcl_Obj *context = objc == 4 ? objv[3] : Tcl_NewObj();
    Tcl_IncrRefCount(context);
    int rc = index == 2 ? dispatch(router, interp, objv[2], context) : lambda_dispatch(router, interp, objv[2], context);
    Tcl_DecrRefCount(context);
    return rc;
}

static int create_router(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    router_command_t *router = calloc(1, sizeof(*router));
    if (router == NULL) return Tpathexp_Error(interp, "MEMORY", "unable to allocate router");
    char error[512] = {0};
    if (tpathexp_router_create(&router->table, NULL, 0, error, sizeof(error)) != 0) {
        free(router);
        return Tpathexp_Error(interp, "MEMORY", error);
    }
    router->handlers = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(router->handlers);
    unsigned long long *next_id = data;
    Tcl_Obj *name;
    do {
        name = Tcl_ObjPrintf("::tpathexp::router%llu", ++*next_id);
        Tcl_IncrRefCount(name);
        if (Tcl_FindCommand(interp, Tcl_GetString(name), NULL, TCL_GLOBAL_ONLY) == NULL) break;
        Tcl_DecrRefCount(name);
    } while (1);
    router->token = Tcl_CreateObjCommand(interp, Tcl_GetString(name), router_command, router, delete_router);
    int rc = TCL_OK;
    if (router->token == NULL) {
        delete_router(router);
        rc = TCL_ERROR;
    } else {
        Tcl_SetObjResult(interp, name);
    }
    Tcl_DecrRefCount(name);
    return rc;
}

static void delete_counter(ClientData data) { free(data); }

int Tpathexp_RouterInit(Tcl_Interp *interp) {
    unsigned long long *counter = calloc(1, sizeof(*counter));
    if (counter == NULL) return Tpathexp_Error(interp, "MEMORY", "unable to initialize routers");
    if (Tcl_CreateObjCommand(interp, "::tpathexp::router", create_router, counter, delete_counter) == NULL) {
        free(counter);
        return TCL_ERROR;
    }
    return TCL_OK;
}
