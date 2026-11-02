#ifndef TPATHEXP_H
#define TPATHEXP_H

#include <tcl.h>

#if TCL_MAJOR_VERSION < 9
typedef int Tcl_Size;
#endif

#ifdef __cplusplus
extern "C" {
#endif

DLLEXPORT int Tpathexp_Init(Tcl_Interp *interp);

#ifdef __cplusplus
}
#endif
#endif
