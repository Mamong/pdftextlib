//========================================================================
//
// GlobalParams.h
//
// Copyright 2001-2003 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2005, 2007-2010 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2005 Jonathan Blandford <jrb@redhat.com>
// Copyright (C) 2006 Takashi Iwai <tiwai@suse.de>
// Copyright (C) 2006 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2007 Krzysztof Kowalczyk <kkowalczyk@gmail.com>
// Copyright (C) 2009 Jonathan Kew <jonathan_kew@sil.org>
// Copyright (C) 2009 Petr Gajdos <pgajdos@novell.com>
// Copyright (C) 2009 William Bader <williambader@hotmail.com>
// Copyright (C) 2010 Hib Eris <hib@hiberis.nl>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifndef GLOBALPARAMS_H
#define GLOBALPARAMS_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include <assert.h>
#include <stdio.h>
#include "gtypes.h"

#if MULTITHREADED
#include "GooMutex.h"
#endif

class GooString;
class GooList;
class GooHash;
class NameToCharCode;
class CharCodeToUnicode;
class CharCodeToUnicodeCache;
class CMap;
class CMapCache;
class GlobalParams;
class GfxFont;
class Stream;

//------------------------------------------------------------------------

// The global parameters object.
extern GlobalParams *globalParams;

//------------------------------------------------------------------------

class GlobalParams {
public:

  // Initialize the global parameters by attempting to read a config
  // file.
  GlobalParams(const char *baseDir = "./");

  ~GlobalParams();

  //----- accessors

  CharCode getMacRomanCharCode(const char *charName);
  Unicode mapNameToUnicode(const char *charName);
  FILE *findCMapFile(GooString *collection, GooString *cMapName);
  FILE *findToUnicodeFile(GooString *name);
  GBool getTextKeepTinyChars();
  GBool getMapNumericCharNames();
  GBool getMapUnknownCharNames();

  CharCodeToUnicode *getCIDToUnicode(GooString *collection);
  CharCodeToUnicode *getUnicodeToUnicode(GooString *fontName);
  CMap *getCMap(GooString *collection, GooString *cMapName, Stream *stream = NULL);

  void setTextKeepTinyChars(GBool keep);
  void setMapNumericCharNames(GBool map);
  void setMapUnknownCharNames(GBool map);

private:

  void parseNameToUnicode(GooString *name);

  void scanEncodingDirs();
  void addCIDToUnicode(GooString *collection, GooString *fileName);
  void addUnicodeMap(GooString *encodingName, GooString *fileName);
  void addCMapDir(GooString *collection, GooString *dir);

  //----- static tables

  NameToCharCode *		// mapping from char name to
  macRomanReverseMap;		//   MacRomanEncoding index

  //----- user-modifiable settings

  NameToCharCode *		// mapping from char name to Unicode
    nameToUnicode;
  GooHash *cidToUnicodes;		// files for mappings from char collections
				//   to Unicode, indexed by collection name
				//   [GooString]
  GooHash *unicodeToUnicodes;	// files for Unicode-to-Unicode mappings,
				//   indexed by font name pattern [GooString]
  GooHash *cMapDirs;		// list of CMap dirs, indexed by collection
				//   name [GooList[GooString]]
  GooList *toUnicodeDirs;		// list of ToUnicode CMap dirs [GooString]

  GBool textKeepTinyChars;	// keep all characters in text output
  GBool mapNumericCharNames;	// map numeric char names (from font subsets)?
  GBool mapUnknownCharNames;	// map unknown char names?

  CharCodeToUnicodeCache *cidToUnicodeCache;
  CharCodeToUnicodeCache *unicodeToUnicodeCache;
  CMapCache *cMapCache;
  
#if MULTITHREADED
  GooMutex mutex;
  GooMutex unicodeMapCacheMutex;
  GooMutex cMapCacheMutex;
#endif

  GooString *popplerDataDir;
};

#endif
