/* $Id: nt-and-windows.h $ */
/** @file
 * IPRT - Header for code using both NT native and Windows APIs.
 */

/*
 * Copyright (C) 2013 Oracle Corporation
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

#ifndef ___iprt_nt_nt_and_windows_h
#define ___iprt_nt_nt_and_windows_h

#if (_MSC_VER >= 1400) && !defined(VBOX_WITH_PATCHED_DDK)
# define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
# define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
# define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
# define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
# define _interlockedbittestandset      _interlockedbittestandset_StupidDDKVsCompilerCrap
# define _interlockedbittestandreset    _interlockedbittestandreset_StupidDDKVsCompilerCrap
# define _interlockedbittestandset64    _interlockedbittestandset64_StupidDDKVsCompilerCrap
# define _interlockedbittestandreset64  _interlockedbittestandreset64_StupidDDKVsCompilerCrap
# pragma warning(disable : 4163)
# endif

#define _PEB    IncompleteWindows__PEB
#define PEB     IncompleteWindows_PEB
#define PPEB    IncompleteWindows_PPEB

#define _TEB    IncompleteWindows__TEB
#define TEB     IncompleteWindows_TEB
#define PTEB    IncompleteWindows_PTEB

#define IPRT_NT_USE_WINTERNL
#define WIN32_NO_STATUS
#include <Windows.h>
#undef WIN32_NO_STATUS

#undef _PEB
#undef PEB
#undef PPEB

#undef _TEB
#undef TEB
#undef PTEB

#include <iprt/nt/nt.h>

#if (_MSC_VER >= 1400) && !defined(VBOX_WITH_PATCHED_DDK)
# pragma warning(default : 4163)
# undef  _InterlockedExchange
# undef  _InterlockedExchangeAdd
# undef  _InterlockedCompareExchange
# undef  _InterlockedAddLargeStatistic
# undef  _interlockedbittestandset
# undef  _interlockedbittestandreset
# undef  _interlockedbittestandset64
# undef  _interlockedbittestandreset64
#endif

#endif

