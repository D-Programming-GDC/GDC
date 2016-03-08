
/* Compiler implementation of the D programming language
 * Copyright (c) 1999-2014 by Digital Mars
 * All Rights Reserved
 * written by Walter Bright
 * http://www.digitalmars.com
 * Distributed under the Boost Software License, Version 1.0.
 * http://www.boost.org/LICENSE_1_0.txt
 * https://github.com/D-Programming-Language/dmd/blob/master/src/mars.h
 */

#ifndef DMD_ERRORS_H
#define DMD_ERRORS_H

#ifdef __DMC__
#pragma once
#endif

#include "mars.h"

bool isConsoleColorSupported();

#ifdef IN_GCC
__attribute__((format (gnu_printf, 2, 3))) void warning(Loc loc, const char *format, ...);
__attribute__((format (gnu_printf, 2, 3))) void warningSupplemental(Loc loc, const char *format, ...);
__attribute__((format (gnu_printf, 2, 3))) void deprecation(Loc loc, const char *format, ...);
__attribute__((format (gnu_printf, 2, 3))) void deprecationSupplemental(Loc loc, const char *format, ...);
__attribute__((format (gnu_printf, 2, 3))) void error(Loc loc, const char *format, ...);
__attribute__((format (gnu_printf, 2, 3))) void errorSupplemental(Loc loc, const char *format, ...);
__attribute__((format (gnu_printf, 2, 0))) void verror(Loc loc, const char *format, va_list ap, const char *p1 = NULL, const char *p2 = NULL, const char *header = "Error: ");
__attribute__((format (gnu_printf, 2, 0))) void verrorSupplemental(Loc loc, const char *format, va_list ap);
__attribute__((format (gnu_printf, 2, 0))) void vwarning(Loc loc, const char *format, va_list);
__attribute__((format (gnu_printf, 2, 0))) void vwarningSupplemental(Loc loc, const char *format, va_list ap);
__attribute__((format (gnu_printf, 2, 0))) void vdeprecation(Loc loc, const char *format, va_list ap, const char *p1 = NULL, const char *p2 = NULL);
__attribute__((format (gnu_printf, 2, 0))) void vdeprecationSupplemental(Loc loc, const char *format, va_list ap);
#else
void warning(Loc loc, const char *format, ...);
void warningSupplemental(Loc loc, const char *format, ...);
void deprecation(Loc loc, const char *format, ...);
void deprecationSupplemental(Loc loc, const char *format, ...);
void error(Loc loc, const char *format, ...);
void errorSupplemental(Loc loc, const char *format, ...);
void verror(Loc loc, const char *format, va_list ap, const char *p1 = NULL, const char *p2 = NULL, const char *header = "Error: ");
void verrorSupplemental(Loc loc, const char *format, va_list ap);
void vwarning(Loc loc, const char *format, va_list);
void vwarningSupplemental(Loc loc, const char *format, va_list ap);
void vdeprecation(Loc loc, const char *format, va_list ap, const char *p1 = NULL, const char *p2 = NULL);
void vdeprecationSupplemental(Loc loc, const char *format, va_list ap);
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
void fatal();
#elif _MSC_VER
__declspec(noreturn)
void fatal();
#else
void fatal();
#endif

void halt();

#endif /* DMD_ERRORS_H */
