#include "tpathexp.h"
#include "path_regexp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    path_regexp_t compiled;
    Tcl_Command token;
} matcher_t;

static int fail(Tcl_Interp *interp, const char *code, const char *message) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(message, -1));
    Tcl_SetErrorCode(interp, "TPATHEXP", code, NULL);
    return TCL_ERROR;
}

static const char *text_argument(Tcl_Interp *interp, Tcl_Obj *obj) {
    Tcl_Size length;
    const Tcl_UniChar *chars = Tcl_GetUnicodeFromObj(obj, &length);
    for (Tcl_Size i = 0; i < length; ++i) {
        if (chars[i] == 0) {
            fail(interp, "NUL", "path expressions and paths must not contain NUL characters");
            return NULL;
        }
    }
    return Tcl_GetString(obj);
}

static void delete_matcher(ClientData data) {
    matcher_t *matcher = data;
    path_regexp_destroy(&matcher->compiled);
    free(matcher);
}

static int match_path(matcher_t *matcher, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    const char *path = text_argument(interp, objv[2]);
    if (path == NULL) return TCL_ERROR;

    const path_regexp_t *compiled = &matcher->compiled;
    size_t count = compiled->capture_count + 1;
    regmatch_t *matches = calloc(count, sizeof(*matches));
    if (matches == NULL) return fail(interp, "MEMORY", "unable to allocate match captures");
    int matched = path_regexp_match(compiled, path, matches, count);
    if (matched < 0) {
        free(matches);
        return fail(interp, "MATCH", "regular expression execution failed");
    }

    Tcl_Obj *params = Tcl_NewDictObj();
    Tcl_IncrRefCount(params);
    if (matched && objc == 4) {
        for (size_t i = 0; i < compiled->key_count; ++i) {
            regmatch_t capture = matches[compiled->keys[i].capture_index];
            if (capture.rm_so < 0) continue;
            Tcl_DictObjPut(interp, params, Tcl_NewStringObj(compiled->keys[i].name, -1),
                          Tcl_NewStringObj(path + capture.rm_so, (int)(capture.rm_eo - capture.rm_so)));
        }
    }
    free(matches);

    /* A variable trace may delete this matcher, so do not access it afterward. */
    int status = TCL_OK;
    if (objc == 4 && Tcl_ObjSetVar2(interp, objv[3], NULL, params, TCL_LEAVE_ERR_MSG) == NULL) {
        status = TCL_ERROR;
    }
    Tcl_DecrRefCount(params);
    if (status == TCL_OK) Tcl_SetObjResult(interp, Tcl_NewBooleanObj(matched));
    return status;
}

static int matcher_command(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    static const char *const commands[] = {"match", "keys", "pattern", "destroy", NULL};
    int index;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "match|keys|pattern|destroy ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], commands, "subcommand", TCL_EXACT, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((index == 0 && objc != 3 && objc != 4) || (index != 0 && objc != 2)) {
        Tcl_WrongNumArgs(interp, 2, objv, index == 0 ? "path ?paramsVar?" : "");
        return TCL_ERROR;
    }

    matcher_t *matcher = data;
    switch (index) {
        case 0:
            return match_path(matcher, interp, objc, objv);
        case 1: {
            Tcl_Obj *keys = Tcl_NewListObj(0, NULL);
            for (size_t i = 0; i < matcher->compiled.key_count; ++i) {
                Tcl_ListObjAppendElement(interp, keys, Tcl_NewStringObj(matcher->compiled.keys[i].name, -1));
            }
            Tcl_SetObjResult(interp, keys);
            break;
        }
        case 2:
            Tcl_SetObjResult(interp, Tcl_NewStringObj(matcher->compiled.pattern, -1));
            break;
        case 3:
            Tcl_DeleteCommandFromToken(interp, matcher->token);
            break;
    }
    return TCL_OK;
}

static int compile_command(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    static const char *const options[] = {"-strict", "-start", "-end", "-nocase", NULL};
    static const int bits[] = {PATH_REGEXP_STRICT, PATH_REGEXP_START, PATH_REGEXP_END, PATH_REGEXP_NOCASE};
    int flags = PATH_REGEXP_START | PATH_REGEXP_END;
    int i = 1;
    while (i < objc - 1) {
        if (strcmp(Tcl_GetString(objv[i]), "--") == 0) {
            ++i;
            break;
        }
        int option, enabled;
        if (Tcl_GetIndexFromObj(interp, objv[i], options, "option", TCL_EXACT, &option) != TCL_OK ||
            Tcl_GetBooleanFromObj(interp, objv[i + 1], &enabled) != TCL_OK) return TCL_ERROR;
        if (enabled) flags |= bits[option];
        else flags &= ~bits[option];
        i += 2;
    }
    if (i != objc - 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-strict boolean? ?-start boolean? ?-end boolean? ?-nocase boolean? ?--? expression");
        return TCL_ERROR;
    }
    const char *expression = text_argument(interp, objv[i]);
    if (expression == NULL) return TCL_ERROR;
    matcher_t *matcher = calloc(1, sizeof(*matcher));
    if (matcher == NULL) return fail(interp, "MEMORY", "unable to allocate matcher");
    char error[512] = {0};
    if (path_regexp_compile(&matcher->compiled, expression, flags, error, sizeof(error)) != 0) {
        delete_matcher(matcher);
        return fail(interp, "COMPILE", error);
    }

    unsigned long long *next_id = data;
    char name[80];
    do {
        snprintf(name, sizeof(name), "::tpathexp::matcher%llu", ++*next_id);
    } while (Tcl_FindCommand(interp, name, NULL, TCL_GLOBAL_ONLY) != NULL);
    matcher->token = Tcl_CreateObjCommand(interp, name, matcher_command, matcher, delete_matcher);
    if (matcher->token == NULL) {
        delete_matcher(matcher);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(name, -1));
    return TCL_OK;
}

static void delete_counter(ClientData data) {
    free(data);
}

int Tpathexp_Init(Tcl_Interp *interp) {
#if TCL_MAJOR_VERSION >= 9
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
#else
    if (Tcl_InitStubs(interp, "8.6", 0) == NULL) return TCL_ERROR;
#endif
    if (Tcl_FindNamespace(interp, "::tpathexp", NULL, TCL_GLOBAL_ONLY) == NULL &&
        Tcl_CreateNamespace(interp, "::tpathexp", NULL, NULL) == NULL) return TCL_ERROR;
    unsigned long long *next_id = calloc(1, sizeof(*next_id));
    if (next_id == NULL) return fail(interp, "MEMORY", "unable to initialize package");
    if (Tcl_CreateObjCommand(interp, "::tpathexp::compile", compile_command, next_id, delete_counter) == NULL) {
        free(next_id);
        return TCL_ERROR;
    }
    return Tcl_PkgProvide(interp, "tpathexp::native", "1.0.0");
}
