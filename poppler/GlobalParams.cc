//========================================================================
//
// GlobalParams.cc
//
// Copyright 2001-2003 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// Copyright (C) 2005 Martin Kretzschmar <martink@gnome.org>
// Copyright (C) 2005, 2006 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2005, 2007-2010 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2005 Jonathan Blandford <jrb@redhat.com>
// Copyright (C) 2006, 2007 Jeff Muizelaar <jeff@infidigm.net>
// Copyright (C) 2006 Takashi Iwai <tiwai@suse.de>
// Copyright (C) 2006 Ed Catmur <ed@catmur.co.uk>
// Copyright (C) 2007 Krzysztof Kowalczyk <kkowalczyk@gmail.com>
// Copyright (C) 2007, 2009 Jonathan Kew <jonathan_kew@sil.org>
// Copyright (C) 2009 Petr Gajdos <pgajdos@novell.com>
// Copyright (C) 2009 William Bader <williambader@hotmail.com>
// Copyright (C) 2009 Kovid Goyal <kovid@kovidgoyal.net>
// Copyright (C) 2010 Hib Eris <hib@hiberis.nl>
// Copyright (C) 2010 Patrick Spendrin <ps_ml@gmx.de>
// Copyright (C) 2010 Jakub Wilk <ubanus@users.sf.net>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "gmem.h"
#include "GooString.h"
#include "GooList.h"
#include "GooHash.h"
#include "gfile.h"
#include "Error.h"
#include "NameToCharCode.h"
#include "NameToUnicodeTable.h"
#include "CharCodeToUnicode.h"
#include "CMap.h"
#include "BuiltinFontTables.h"
#include "FontEncodingTables.h"
#include "GlobalParams.h"
#include "GfxFont.h"

#include <strings.h>

#if MULTITHREADED
#  define lockGlobalParams            gLockMutex(&mutex)
#  define lockUnicodeMapCache         gLockMutex(&unicodeMapCacheMutex)
#  define lockCMapCache               gLockMutex(&cMapCacheMutex)
#  define unlockGlobalParams          gUnlockMutex(&mutex)
#  define unlockUnicodeMapCache       gUnlockMutex(&unicodeMapCacheMutex)
#  define unlockCMapCache             gUnlockMutex(&cMapCacheMutex)
#else
#  define lockGlobalParams
#  define lockUnicodeMapCache
#  define lockCMapCache
#  define unlockGlobalParams
#  define unlockUnicodeMapCache
#  define unlockCMapCache
#endif

//------------------------------------------------------------------------

#define cidToUnicodeCacheSize     4
#define unicodeToUnicodeCacheSize 4

#define POPPLER_DATADIR "poppler-data"

//------------------------------------------------------------------------

GlobalParams *globalParams = NULL;

//------------------------------------------------------------------------
// parsing
//------------------------------------------------------------------------

GlobalParams::GlobalParams(const char *baseDir)
{
  int i;

#if MULTITHREADED
  gInitMutex(&mutex);
  gInitMutex(&unicodeMapCacheMutex);
  gInitMutex(&cMapCacheMutex);
#endif

  initBuiltinFontTables();

  // scan the encoding in reverse because we want the lowest-numbered
  // index for each char name ('space' is encoded twice)
  macRomanReverseMap = new NameToCharCode();
  for (i = 255; i >= 0; --i) {
    if (macRomanEncoding[i]) {
      macRomanReverseMap->add(macRomanEncoding[i], (CharCode)i);
    }
  }

  nameToUnicode = new NameToCharCode();
  cidToUnicodes = new GooHash(gTrue);
  unicodeToUnicodes = new GooHash(gTrue);
  cMapDirs = new GooHash(gTrue);
  toUnicodeDirs = new GooList();
  textKeepTinyChars = gFalse;
  mapNumericCharNames = gTrue;
  mapUnknownCharNames = gTrue;

  cidToUnicodeCache = new CharCodeToUnicodeCache(cidToUnicodeCacheSize);
  unicodeToUnicodeCache =
      new CharCodeToUnicodeCache(unicodeToUnicodeCacheSize);
  cMapCache = new CMapCache();

  // set up the initial nameToUnicode table
  for (i = 0; nameToUnicodeTab[i].name; ++i) {
    nameToUnicode->add(nameToUnicodeTab[i].name, nameToUnicodeTab[i].u);
  }

	popplerDataDir = new GooString(baseDir);
	appendToPath(popplerDataDir, POPPLER_DATADIR);
	scanEncodingDirs();
}

void GlobalParams::scanEncodingDirs() {
  GDir *dir;
  GDirEntry *entry;
  const char *dataRoot = popplerDataDir->getCString();
  
  // allocate buffer large enough to append "/nameToUnicode"
  size_t bufSize = strlen(dataRoot) + strlen("/nameToUnicode") + 1;
  char *dataPathBuffer = new char[bufSize];
  
  snprintf(dataPathBuffer, bufSize, "%s/nameToUnicode", dataRoot);
  dir = new GDir(dataPathBuffer, gTrue);
  while (entry = dir->getNextEntry(), entry != NULL) {
    if (!entry->isDir()) {
      parseNameToUnicode(entry->getFullPath());
    }
    delete entry;
  }
  delete dir;

  snprintf(dataPathBuffer, bufSize, "%s/cidToUnicode", dataRoot);
  dir = new GDir(dataPathBuffer, gFalse);
  while (entry = dir->getNextEntry(), entry != NULL) {
    addCIDToUnicode(entry->getName(), entry->getFullPath());
    delete entry;
  }
  delete dir;

  snprintf(dataPathBuffer, bufSize, "%s/cMap", dataRoot);
  dir = new GDir(dataPathBuffer, gFalse);
  while (entry = dir->getNextEntry(), entry != NULL) {
    addCMapDir(entry->getName(), entry->getFullPath());
    toUnicodeDirs->append(entry->getFullPath()->copy());
    delete entry;
  }
  delete dir;
  
  delete[] dataPathBuffer;
}

void GlobalParams::parseNameToUnicode(GooString *name) {
  char *tok1, *tok2;
  FILE *f;
  char buf[256];
  int line;
  Unicode u;
  char *tokptr;

  if (!(f = fopen(name->getCString(), "r"))) {
    error(-1, "Couldn't open 'nameToUnicode' file '%s'",
	  name->getCString());
    return;
  }
  line = 1;
  while (getLine(buf, sizeof(buf), f)) {
    tok1 = strtok_r(buf, " \t\r\n", &tokptr);
    tok2 = strtok_r(NULL, " \t\r\n", &tokptr);
    if (tok1 && tok2) {
      sscanf(tok1, "%x", &u);
      nameToUnicode->add(tok2, u);
    } else {
      error(-1, "Bad line in 'nameToUnicode' file (%s:%d)",
	    name->getCString(), line);
    }
    ++line;
  }
  fclose(f);
}

void GlobalParams::addCIDToUnicode(GooString *collection,
				   GooString *fileName) {
  GooString *old;

  if ((old = (GooString *)cidToUnicodes->remove(collection))) {
    delete old;
  }
  cidToUnicodes->add(collection->copy(), fileName->copy());
}

void GlobalParams::addCMapDir(GooString *collection, GooString *dir) {
  GooList *list;

  if (!(list = (GooList *)cMapDirs->lookup(collection))) {
    list = new GooList();
    cMapDirs->add(collection->copy(), list);
  }
  list->append(dir->copy());
}

GlobalParams::~GlobalParams() {
  freeBuiltinFontTables();

  delete macRomanReverseMap;

  delete nameToUnicode;
  deleteGooHash(cidToUnicodes, GooString);
  deleteGooHash(unicodeToUnicodes, GooString);
  deleteGooList(toUnicodeDirs, GooString);
  GooHashIter *iter;
  GooString *key;
  cMapDirs->startIter(&iter);
  void *val;
  while (cMapDirs->getNext(&iter, &key, &val)) {
    GooList* list = (GooList*)val;
    deleteGooList(list, GooString);
  }
  delete cMapDirs;

  delete cidToUnicodeCache;
  delete unicodeToUnicodeCache;
  delete cMapCache;
	
	delete popplerDataDir;

#if MULTITHREADED
  gDestroyMutex(&mutex);
  gDestroyMutex(&unicodeMapCacheMutex);
  gDestroyMutex(&cMapCacheMutex);
#endif
}


//------------------------------------------------------------------------
// accessors
//------------------------------------------------------------------------

CharCode GlobalParams::getMacRomanCharCode(const char *charName) {
  // no need to lock - macRomanReverseMap is constant
  return macRomanReverseMap->lookup(charName);
}

Unicode GlobalParams::mapNameToUnicode(const char *charName) {
  // no need to lock - nameToUnicode is constant
  return nameToUnicode->lookup(charName);
}

FILE *GlobalParams::findCMapFile(GooString *collection, GooString *cMapName) {
  GooList *list;
  GooString *dir;
  GooString *fileName;
  FILE *f;
  int i;

  lockGlobalParams;
  if (!(list = (GooList *)cMapDirs->lookup(collection))) {
    unlockGlobalParams;
    return NULL;
  }
  for (i = 0; i < list->getLength(); ++i) {
    dir = (GooString *)list->get(i);
    fileName = appendToPath(dir->copy(), cMapName->getCString());
    f = fopen(fileName->getCString(), "r");
    delete fileName;
    if (f) {
      unlockGlobalParams;
      return f;
    }
  }
  unlockGlobalParams;
  return NULL;
}

FILE *GlobalParams::findToUnicodeFile(GooString *name) {
  GooString *dir, *fileName;
  FILE *f;
  int i;

  lockGlobalParams;
  for (i = 0; i < toUnicodeDirs->getLength(); ++i) {
    dir = (GooString *)toUnicodeDirs->get(i);
    fileName = appendToPath(dir->copy(), name->getCString());
    f = fopen(fileName->getCString(), "r");
    delete fileName;
    if (f) {
      unlockGlobalParams;
      return f;
    }
  }
  unlockGlobalParams;
  return NULL;
}

GBool GlobalParams::getTextKeepTinyChars() {
  GBool tiny;

  lockGlobalParams;
  tiny = textKeepTinyChars;
  unlockGlobalParams;
  return tiny;
}

GBool GlobalParams::getMapNumericCharNames() {
  GBool map;

  lockGlobalParams;
  map = mapNumericCharNames;
  unlockGlobalParams;
  return map;
}

GBool GlobalParams::getMapUnknownCharNames() {
  GBool map;

  lockGlobalParams;
  map = mapUnknownCharNames;
  unlockGlobalParams;
  return map;
}

CharCodeToUnicode *GlobalParams::getCIDToUnicode(GooString *collection) {
  GooString *fileName;
  CharCodeToUnicode *ctu;

  lockGlobalParams;
  if (!(ctu = cidToUnicodeCache->getCharCodeToUnicode(collection))) {
    if ((fileName = (GooString *)cidToUnicodes->lookup(collection)) &&
	(ctu = CharCodeToUnicode::parseCIDToUnicode(fileName, collection))) {
      cidToUnicodeCache->add(ctu);
    }
  }
  unlockGlobalParams;
  return ctu;
}

CharCodeToUnicode *GlobalParams::getUnicodeToUnicode(GooString *fontName) {
  lockGlobalParams;
  GooHashIter *iter;
  unicodeToUnicodes->startIter(&iter);
  GooString *fileName = NULL;
  GooString *fontPattern;
  void *val;
  while (!fileName && unicodeToUnicodes->getNext(&iter, &fontPattern, &val)) {
    if (strstr(fontName->getCString(), fontPattern->getCString())) {
      unicodeToUnicodes->killIter(&iter);
      fileName = (GooString*)val;
    }
  }
  CharCodeToUnicode *ctu = NULL;
  if (fileName) {
    ctu = unicodeToUnicodeCache->getCharCodeToUnicode(fileName);
    if (!ctu) {
      ctu = CharCodeToUnicode::parseUnicodeToUnicode(fileName);
      if (ctu)
         unicodeToUnicodeCache->add(ctu);
    }
  }
  unlockGlobalParams;
  return ctu;
}

CMap *GlobalParams::getCMap(GooString *collection, GooString *cMapName, Stream *stream) {
  CMap *cMap;

  lockCMapCache;
  cMap = cMapCache->getCMap(collection, cMapName, stream);
  unlockCMapCache;
  return cMap;
}

//------------------------------------------------------------------------
// functions to set parameters
//------------------------------------------------------------------------

void GlobalParams::setTextKeepTinyChars(GBool keep) {
  lockGlobalParams;
  textKeepTinyChars = keep;
  unlockGlobalParams;
}

void GlobalParams::setMapNumericCharNames(GBool map) {
  lockGlobalParams;
  mapNumericCharNames = map;
  unlockGlobalParams;
}

void GlobalParams::setMapUnknownCharNames(GBool map) {
  lockGlobalParams;
  mapUnknownCharNames = map;
  unlockGlobalParams;
}
