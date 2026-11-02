#include "tpathexp.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    Tcl_FindExecutable(argv[0]);
    Tcl_Interp *interp = Tcl_CreateInterp();
    Tcl_SetVar(interp, "argv", "", TCL_GLOBAL_ONLY);
    Tcl_SetVar(interp, "argc", "0", TCL_GLOBAL_ONLY);
    Tcl_SetVar(interp, "argv0", argv[argc - 1], TCL_GLOBAL_ONLY);
    int rc = Tcl_Init(interp);
    if (rc == TCL_OK) rc = Tpathexp_Init(interp);
    if (rc == TCL_OK) rc = Tcl_EvalFile(interp, argv[1]);
    if (rc == TCL_OK && argc == 3) rc = Tcl_EvalFile(interp, argv[2]);
    if (rc == TCL_OK) rc = Tcl_Eval(interp,
        "set r [tpathexp::router]\n"
        "$r add GET /users/:id unusedHandler\n"
        "set result [$r match GET /users/42]\n"
        "if {[dict get $result pathParameters id] ne {42}} {error {wrong parameter}}\n"
        "$r destroy\n");
    if (rc != TCL_OK) fprintf(stderr, "%s\n", Tcl_GetStringResult(interp));
    Tcl_DeleteInterp(interp);
    Tcl_Finalize();
    return rc == TCL_OK ? 0 : 1;
}
