//========================================================================
//
// gfile.cc
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
// Copyright (C) 2006 Takashi Iwai <tiwai@suse.de>
// Copyright (C) 2006 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2008 Adam Batkin <adam@batkin.net>
// Copyright (C) 2008, 2010 Hib Eris <hib@hiberis.nl>
// Copyright (C) 2009 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2009 Kovid Goyal <kovid@kovidgoyal.net>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include "GooString.h"
#include "gfile.h"

//------------------------------------------------------------------------

GooString *appendToPath(GooString *path, const char *fileName) {
  int i;

  // appending "." does nothing
  if (!strcmp(fileName, "."))
    return path;

  // appending ".." goes up one directory
  if (!strcmp(fileName, "..")) {
    for (i = path->getLength() - 2; i >= 0; --i) {
      if (path->getChar(i) == '/')
	break;
    }
    if (i <= 0) {
      if (path->getChar(0) == '/') {
	path->del(1, path->getLength() - 1);
      } else {
	path->clear();
	path->append("..");
      }
    } else {
      path->del(i, path->getLength() - i);
    }
    return path;
  }

  // otherwise, append "/" and new path component
  if (path->getLength() > 0 &&
      path->getChar(path->getLength() - 1) != '/')
    path->append('/');
  path->append(fileName);
  return path;
}

char *getLine(char *buf, int size, FILE *f) {
	int c, i;
	
	i = 0;
	while (i < size - 1) {
		if ((c = fgetc(f)) == EOF) {
			break;
		}
		buf[i++] = (char)c;
		if (c == '\x0a') {
			break;
		}
		if (c == '\x0d') {
			c = fgetc(f);
			if (c == '\x0a' && i < size - 1) {
				buf[i++] = (char)c;
			} else if (c != EOF) {
				ungetc(c, f);
			}
			break;
		}
	}
	buf[i] = '\0';
	if (i == 0) {
		return NULL;
	}
	return buf;
}

//------------------------------------------------------------------------
// GDir and GDirEntry
//------------------------------------------------------------------------

GDirEntry::GDirEntry(const char *dirPath, const char *nameA, GBool doStat) {
  struct stat st;
  name = new GooString(nameA);
  dir = gFalse;
  fullPath = new GooString(dirPath);
  appendToPath(fullPath, nameA);
  if (doStat) {
    if (stat(fullPath->getCString(), &st) == 0)
      dir = S_ISDIR(st.st_mode);
  }
}

GDirEntry::~GDirEntry() {
  delete fullPath;
  delete name;
}

GDir::GDir(const char *name, GBool doStatA) {
  path = new GooString(name);
  doStat = doStatA;
  dir = opendir(name);
}

GDir::~GDir() {
  delete path;
  if (dir)
    closedir(dir);
}

GDirEntry *GDir::getNextEntry() {
  GDirEntry *e;
  struct dirent *ent;
  e = NULL;
  if (dir) {
    do {
      ent = readdir(dir);
    }
    //while (ent && (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")));
	  while (ent && ent->d_name[0] == '.');
    if (ent) {
      e = new GDirEntry(path->getCString(), ent->d_name, doStat);
    }
  }
  return e;
}

