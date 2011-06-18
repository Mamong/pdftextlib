//========================================================================
//
// Catalog.h
//
// Copyright 1996-2007 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2005 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2005, 2007, 2009, 2010 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2005 Jonathan Blandford <jrb@redhat.com>
// Copyright (C) 2005, 2006, 2008 Brad Hards <bradh@frogmouth.net>
// Copyright (C) 2007 Julien Rebetez <julienr@svn.gnome.org>
// Copyright (C) 2008 Pino Toscano <pino@kde.org>
// Copyright (C) 2010 Hib Eris <hib@hiberis.nl>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifndef CATALOG_H
#define CATALOG_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include <vector>

class XRef;
class Object;
class Page;
class PageAttrs;
struct Ref;
class OCGs;

//------------------------------------------------------------------------
// Catalog
//------------------------------------------------------------------------

class Catalog {
public:

  // Constructor.
  Catalog(XRef *xrefA);

  // Destructor.
  ~Catalog();

  // Is catalog valid?
  GBool isOk() { return ok; }

  // Get number of pages.
  int getNumPages();

  // Get a page.
  Page *getPage(int i);

  // Get the reference for a page object.
  Ref *getPageRef(int i);

  OCGs *getOptContentConfig() { return optContent; }

private:

  XRef *xref;			// the xref table for this PDF file
  Page **pages;			// array of pages
  int lastCachedPage;
  std::vector<Dict *> *pagesList;
  std::vector<PageAttrs *> *attrsList;
  std::vector<int> *kidsIdxList;
  int numPages;			// number of pages
  int pagesSize;		// size of pages array
  OCGs *optContent;		// Optional Content groups
  GBool ok;			// true if catalog is valid

  GBool cachePageTree(int page); // Cache first <page> pages.
};

#endif
