//========================================================================
//
// Error.h
//
// Copyright 1996-2003 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2005, 2007 Jeff Muizelaar <jeff@infidigm.net>
// Copyright (C) 2005 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2005 Kristian Høgsberg <krh@redhat.com>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifndef ERROR_H
#define ERROR_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include <stdarg.h>

extern void error(int pos, const char *msg, ...) __attribute__((__format__(__printf__, 2, 3)));
void warning(const char *msg, ...) __attribute__((__format__(__printf__, 1, 2)));

#endif
