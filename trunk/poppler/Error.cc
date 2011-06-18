//========================================================================
//
// Error.cc
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
// Copyright (C) 2007 Krzysztof Kowalczyk <kkowalczyk@gmail.com>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include "Error.h"

void error(int pos, const char *msg, ...) {
  va_list args;
  va_start(args, msg);
	if (pos >= 0) {
		fprintf(stderr, "Error (%d): ", pos);
	} else {
		fprintf(stderr, "Error: ");
	}
	vfprintf(stderr, msg, args);
	fprintf(stderr, "\n");
	fflush(stderr);
  va_end(args);
}

void warning(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  vprintf(msg, args);
  va_end(args);
}
