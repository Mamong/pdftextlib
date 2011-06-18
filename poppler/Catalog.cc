//========================================================================
//
// Catalog.cc
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
// Copyright (C) 2005-2010 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2005 Jeff Muizelaar <jrmuizel@nit.ca>
// Copyright (C) 2005 Jonathan Blandford <jrb@redhat.com>
// Copyright (C) 2005 Marco Pesenti Gritti <mpg@redhat.com>
// Copyright (C) 2005, 2006, 2008 Brad Hards <bradh@frogmouth.net>
// Copyright (C) 2006, 2008 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2007 Julien Rebetez <julienr@svn.gnome.org>
// Copyright (C) 2008 Pino Toscano <pino@kde.org>
// Copyright (C) 2009 Ilya Gorenbein <igorenbein@finjan.com>
// Copyright (C) 2010 Hib Eris <hib@hiberis.nl>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stddef.h>
#include <stdlib.h>
#include "gmem.h"
#include "Object.h"
#include "XRef.h"
#include "Array.h"
#include "Dict.h"
#include "Page.h"
#include "Error.h"
#include "Catalog.h"
#include "OptionalContent.h"

//------------------------------------------------------------------------
// Catalog
//------------------------------------------------------------------------

Catalog::Catalog(XRef *xrefA) {
  Object catDict, optContentProps;

  ok = gTrue;
  xref = xrefA;
  pages = NULL;
  numPages = -1;
  pagesSize = 0;
  optContent = NULL;

  pagesList = NULL;
  attrsList = NULL;
  kidsIdxList = NULL;
  lastCachedPage = 0;

  xref->getCatalog(&catDict);
  if (!catDict.isDict()) {
    error(-1, "Catalog object is wrong type (%s)", catDict.getTypeName());
    goto err1;
  }

  // get the Optional Content dictionary
  if (catDict.dictLookup("OCProperties", &optContentProps)->isDict()) {
    optContent = new OCGs(&optContentProps, xref);
    if (!optContent->isOk ()) {
      delete optContent;
      optContent = NULL;
    }
  }
  optContentProps.free();

  catDict.free();
  return;

 err1:
  catDict.free();
  ok = gFalse;
}

Catalog::~Catalog() {
  delete kidsIdxList;
  if (attrsList) {
    std::vector<PageAttrs *>::iterator it;
    for (it = attrsList->begin() ; it < attrsList->end(); it++ ) {
      delete *it;
    }
    delete attrsList;
  }
  if (pagesList) {
    std::vector<Dict *>::iterator it;
    for (it = pagesList->begin() ; it < pagesList->end(); it++ ) {
      if (!(*it)->decRef()) {
         delete *it;
      }
    }
    delete pagesList;
  }
  if (pages) {
    for (int i = 0; i < pagesSize; ++i) {
      if (pages[i]) {
	delete pages[i];
      }
    }
    gfree(pages);
  }
  delete optContent;
}

Page *Catalog::getPage(int i)
{
  if (i < 1) return NULL;

  if (i > lastCachedPage) {
     if (cachePageTree(i) == gFalse) return NULL;
  }
  return pages[i-1];
}

GBool Catalog::cachePageTree(int page)
{
  Dict *pagesDict;

  if (pagesList == NULL) {

    Object catDict;
    Ref pagesRef;

    xref->getCatalog(&catDict);

    Object pagesDictRef;
    if (catDict.dictLookupNF("Pages", &pagesDictRef)->isRef() &&
        pagesDictRef.getRefNum() >= 0 &&
        pagesDictRef.getRefNum() < xref->getNumObjects()) {
      pagesRef = pagesDictRef.getRef();
      pagesDictRef.free();
    } else {
       error(-1, "Catalog dictionary does not contain a valid \"Pages\" entry");
       pagesDictRef.free();
       return gFalse;
    }

    Object obj;
    catDict.dictLookup("Pages", &obj);
    catDict.free();
    // This should really be isDict("Pages"), but I've seen at least one
    // PDF file where the /Type entry is missing.
    if (obj.isDict()) {
      obj.getDict()->incRef();
      pagesDict = obj.getDict();
      obj.free();
    }
    else {
      error(-1, "Top-level pages object is wrong type (%s)", obj.getTypeName());
      obj.free();
      return gFalse;
    }

    pagesSize = getNumPages();
    pages = (Page **)gmallocn(pagesSize, sizeof(Page *));
    for (int i = 0; i < pagesSize; ++i)
      pages[i] = NULL;
  
    pagesList = new std::vector<Dict *>();
    pagesList->push_back(pagesDict);
    attrsList = new std::vector<PageAttrs *>();
    attrsList->push_back(new PageAttrs(NULL, pagesDict));
    kidsIdxList = new std::vector<int>();
    kidsIdxList->push_back(0);
    lastCachedPage = 0;

  }

  while(1) {

    if (page <= lastCachedPage) return gTrue;

    if (pagesList->empty()) return gFalse;

    pagesDict = pagesList->back();
    Object kids;
    pagesDict->lookup("Kids", &kids);
    if (!kids.isArray()) {
      error(-1, "Kids object (page %d) is wrong type (%s)",
            lastCachedPage+1, kids.getTypeName());
      kids.free();
      return gFalse;
    }

    int kidsIdx = kidsIdxList->back();
    if (kidsIdx >= kids.arrayGetLength()) {
       if (!pagesList->back()->decRef()) {
         delete pagesList->back();
       }
       pagesList->pop_back();
       delete attrsList->back();
       attrsList->pop_back();
       kidsIdxList->pop_back();
       if (!kidsIdxList->empty()) kidsIdxList->back()++;
       kids.free();
       continue;
    }

    Object kidRef;
    kids.arrayGetNF(kidsIdx, &kidRef);
    if (!kidRef.isRef()) {
      error(-1, "Kid object (page %d) is not an indirect reference (%s)",
            lastCachedPage+1, kidRef.getTypeName());
      kidRef.free();
      kids.free();
      return gFalse;
    }

    GBool loop = gFalse;;
    if (loop) {
      error(-1, "Loop in Pages tree");
      kidRef.free();
      kids.free();
      kidsIdxList->back()++;
      continue;
    }

    Object kid;
    kids.arrayGet(kidsIdx, &kid);
    kids.free();
    if (kid.isDict("Page") || (kid.isDict() && !kid.getDict()->hasKey("Kids"))) {
      PageAttrs *attrs = new PageAttrs(attrsList->back(), kid.getDict());
      Page *p = new Page(xref, lastCachedPage+1, kid.getDict(), attrs);
      if (!p->isOk()) {
        error(-1, "Failed to create page (page %d)", lastCachedPage+1);
        delete p;
        kidRef.free();
        kid.free();
        return gFalse;
      }

      if (lastCachedPage >= numPages) {
        error(-1, "Page count in top-level pages object is incorrect");
        kidRef.free();
        kid.free();
        return gFalse;
      }

      pages[lastCachedPage] = p;

      lastCachedPage++;
      kidsIdxList->back()++;

    // This should really be isDict("Pages"), but I've seen at least one
    // PDF file where the /Type entry is missing.
    } else if (kid.isDict()) {
      attrsList->push_back(new PageAttrs(attrsList->back(), kid.getDict()));
      kid.getDict()->incRef();
      pagesList->push_back(kid.getDict());
      kidsIdxList->push_back(0);
    } else {
      error(-1, "Kid object (page %d) is wrong type (%s)",
            lastCachedPage+1, kid.getTypeName());
      kidsIdxList->back()++;
    }
    kidRef.free();
    kid.free();

  }

  return gFalse;
}

int Catalog::getNumPages()
{
  if (numPages == -1)
  {
    Object catDict, pagesDict, obj;

    xref->getCatalog(&catDict);
    catDict.dictLookup("Pages", &pagesDict);
    catDict.free();

    // This should really be isDict("Pages"), but I've seen at least one
    // PDF file where the /Type entry is missing.
    if (!pagesDict.isDict()) {
      error(-1, "Top-level pages object is wrong type (%s)",
          pagesDict.getTypeName());
      pagesDict.free();
      return 0;
    }

    pagesDict.dictLookup("Count", &obj);
    // some PDF files actually use real numbers here ("/Count 9.0")
    if (!obj.isNum()) {
      error(-1, "Page count in top-level pages object is wrong type (%s)",
         obj.getTypeName());
      numPages = 0;
    } else {
      numPages = (int)obj.getNum();
    }

    obj.free();
    pagesDict.free();
  }

  return numPages;
}

