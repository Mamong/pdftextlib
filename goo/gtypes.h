/*
 * gtypes.h
 *
 * Some useful simple types.
 *
 * Copyright 1996-2003 Glyph & Cog, LLC
 */

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2010 Patrick Spendrin <ps_ml@gmx.de>
// Copyright (C) 2010 Albert Astals Cid <aacid@kde.org>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifndef GTYPES_H
#define GTYPES_H

/*
 * These have stupid names to avoid conflicts with some (but not all)
 * C++ compilers which define them.
 */
typedef bool GBool;
#define gTrue true
#define gFalse false

/*
 * These have stupid names to avoid conflicts with <sys/types.h>,
 * which on various systems defines some random subset of these.
 */
typedef unsigned char Guchar;
typedef unsigned short Gushort;
typedef unsigned int Guint;
typedef unsigned long Gulong;

// Unicode character.
typedef unsigned int Unicode;

// Character ID for CID character collections.
typedef unsigned int CID;

// This is large enough to hold any of the following:
// - 8-bit char code
// - 16-bit CID
// - Unicode
typedef unsigned int CharCode;

#endif
