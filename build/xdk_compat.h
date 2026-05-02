// xdk_compat.h -- Compatibility shims for building Xbox XDK code with clang

#ifndef _XDK_COMPAT_H
#define _XDK_COMPAT_H

// excpt.h defines EXCEPTION_DISPOSITION which ntdef.h needs.
#include <crt/excpt.h>

// Pull in the full NT kernel type chain.  This provides NTSTATUS, STRING,
// OBJECT_ATTRIBUTES, RTL_CRITICAL_SECTION, IO_STATUS_BLOCK, DEVICE_OBJECT,
// and every other kernel type -- exactly what std.h does for the main
// Theseus source files.  Without this, xtl.h -> winbase.h hits missing types.
#ifdef __cplusplus
extern "C" {
#endif
#include <ntos.h>
#include <ntrtl.h>
#include <nturtl.h>
#ifdef __cplusplus
}
#endif

// ntos.h defines _NTOS_ which winbase.h checks to skip Interlocked macros
// and other kernel-provided declarations.

// The NT kernel headers above define the core Win32 types (_CONTEXT,
// _LIST_ENTRY, _LARGE_INTEGER, etc.). Prevent the local XDK winnt.h
// from redefining them by setting its include guard.
#define _WINNT_

// Mark the Interlocked functions as already declared so winbase.h
// doesn't redeclare them with conflicting signatures.
#define _INTERLOCKED_DEFINED

// Tell toolbox/xboxinternals.h that all kernel types are available.
#define _THESEUS_STD_H

#endif
