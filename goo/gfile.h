//========================================================================
//
// gfile.h
//
// Miscellaneous file and directory name manipulation.
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
// Copyright (C) 2006 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2009 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2009 Kovid Goyal <kovid@kovidgoyal.net>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifndef GFILE_H
#define GFILE_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <dirent.h>
#include "gtypes.h"

class GooString;

//------------------------------------------------------------------------

// Append a file name to a path string.  <path> may be an empty
// string, denoting the current directory).  Returns <path>.
extern GooString *appendToPath(GooString *path, const char *fileName);

// Just like fgets, but handles Unix, Mac, and/or DOS end-of-line
// conventions.
extern char *getLine(char *buf, int size, FILE *f);

//------------------------------------------------------------------------
// GDir and GDirEntry
//------------------------------------------------------------------------

class GDirEntry {
public:

  GDirEntry(const char *dirPath, const char *nameA, GBool doStat);
  ~GDirEntry();
  GooString *getName() { return name; }
  GooString *getFullPath() { return fullPath; }
  GBool isDir() { return dir; }

private:

  GooString *name;		// dir/file name
  GooString *fullPath;
  GBool dir;			// is it a directory?
};

class GDir {
public:

  GDir(const char *name, GBool doStatA = gTrue);
  ~GDir();
  GDirEntry *getNextEntry();   // entries start with '.' are ignored

private:

  GooString *path;		// directory path
  GBool doStat;			// call stat() for each entry?
  DIR *dir;			// the DIR structure from opendir()
};

#endif
