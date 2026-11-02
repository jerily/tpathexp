#ifndef TPATHEXP_INT_H
#define TPATHEXP_INT_H

#include "tpathexp.h"

int Tpathexp_Error(Tcl_Interp *interp, const char *code, const char *message);
const char *Tpathexp_Text(Tcl_Interp *interp, Tcl_Obj *obj);
int Tpathexp_Flags(Tcl_Interp *interp, int first, int end, Tcl_Obj *const objv[], int *flags);
int Tpathexp_RouterInit(Tcl_Interp *interp);

#endif
