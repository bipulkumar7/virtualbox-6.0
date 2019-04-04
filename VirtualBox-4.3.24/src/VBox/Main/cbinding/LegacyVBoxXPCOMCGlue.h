/* $Revision: 91907 $ */
/** @file VBoxXPCOMCGlue.h
 * Glue for dynamically linking with VBoxCAPI, LEGACY VARIANT.
 */

/*
 * Copyright (C) 2008-2014 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ___VBoxXPCOMCGlue_h
#define ___VBoxXPCOMCGlue_h

#undef VBOX_WITH_GLUE
#include "VBoxCAPI_v4_3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The so/dynsym/dll handle for VBoxCAPI. */
#ifndef WIN32
extern void *g_hVBoxCAPI;
#else
extern HMODULE g_hVBoxCAPI;
#endif
/** The last load error. */
extern char g_szVBoxErrMsg[256];
/** Pointer to the VBOXCAPI function table. */
extern PCVBOXCAPI g_pVBoxFuncs;
/** Pointer to VBoxGetCAPIFunctions for the loaded VBoxCAPI so/dylib/dll. */
extern PFNVBOXGETCAPIFUNCTIONS g_pfnGetFunctions;


int VBoxCGlueInit(void);
void VBoxCGlueTerm(void);


#ifdef __cplusplus
}
#endif

#endif

