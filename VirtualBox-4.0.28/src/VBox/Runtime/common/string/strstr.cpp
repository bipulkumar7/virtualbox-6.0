/* $Id: strstr.cpp $ */
/** @file
 * IPRT - CRT Strings, strstr().
 */

/*
 * Copyright (C) 2006-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/string.h>


/**
 * Find the first occurence of @a pszNeedle in @a pszHaystack.
 *
 * @returns Pointer to the needle position within the haystack. NULL if needle
 *          not found
 * @param   pszHaystack     What to search within.
 * @param   pszNeedle       What to search for.
 */
#ifdef _MSC_VER
# if _MSC_VER >= 1400
__checkReturn char *__cdecl strstr(__in_z const char *pszHaystack, __in_z const char *pszNeedle)
# else
char *strstr(const char *pszHaystack, const char *pszNeedle)
# endif
#else
char *strstr(const char *pszHaystack, const char *pszNeedle)
#endif
{
    size_t const    cchNeedle = strlen(pszNeedle);
    char const      chFirst   = *pszNeedle;
    char            ch;

    while ((ch = *pszHaystack) != '\0')
    {
        if (   ch == chFirst
            && strncmp(pszHaystack, pszNeedle, cchNeedle) == 0)
            return (char *)pszHaystack;
        pszHaystack++;
    }

    return NULL;
}


