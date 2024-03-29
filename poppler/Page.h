//========================================================================
//
// Page.h
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
// Copyright (C) 2005 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2005 Jeff Muizelaar <jeff@infidigm.net>
// Copyright (C) 2006 Pino Toscano <pino@kde.org>
// Copyright (C) 2006 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2007 Julien Rebetez <julienr@svn.gnome.org>
// Copyright (C) 2008 Iñigo Martínez <inigomartinez@gmail.com>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifndef PAGE_H
#define PAGE_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include "Object.h"

class Dict;
class XRef;
class OutputDev;
class Catalog;
class Gfx;

//------------------------------------------------------------------------

class PDFRectangle {
public:
  double x1, y1, x2, y2;

  PDFRectangle() { x1 = y1 = x2 = y2 = 0; }
  PDFRectangle(double x1A, double y1A, double x2A, double y2A)
    { x1 = x1A; y1 = y1A; x2 = x2A; y2 = y2A; }
  GBool isValid() { return x1 != 0 || y1 != 0 || x2 != 0 || y2 != 0; }
  void clipTo(PDFRectangle *rect);
};

//------------------------------------------------------------------------
// PageAttrs
//------------------------------------------------------------------------

class PageAttrs {
public:

  // Construct a new PageAttrs object by merging a dictionary
  // (of type Pages or Page) into another PageAttrs object.  If
  // <attrs> is NULL, uses defaults.
  PageAttrs(PageAttrs *attrs, Dict *dict);

  // Destructor.
  ~PageAttrs();

  // Accessors.
  PDFRectangle *getMediaBox() { return &mediaBox; }
  PDFRectangle *getCropBox() { return &cropBox; }
  GBool isCropped() { return haveCropBox; }
  PDFRectangle *getBleedBox() { return &bleedBox; }
  PDFRectangle *getTrimBox() { return &trimBox; }
  PDFRectangle *getArtBox() { return &artBox; }
  int getRotate() { return rotate; }
  GooString *getLastModified()
    { return lastModified.isString()
	? lastModified.getString() : (GooString *)NULL; }
  Dict *getBoxColorInfo()
    { return boxColorInfo.isDict() ? boxColorInfo.getDict() : (Dict *)NULL; }
  Dict *getGroup()
    { return group.isDict() ? group.getDict() : (Dict *)NULL; }
  Stream *getMetadata()
    { return metadata.isStream() ? metadata.getStream() : (Stream *)NULL; }
  Dict *getPieceInfo()
    { return pieceInfo.isDict() ? pieceInfo.getDict() : (Dict *)NULL; }
  Dict *getSeparationInfo()
    { return separationInfo.isDict()
	? separationInfo.getDict() : (Dict *)NULL; }
  Dict *getResourceDict()
    { return resources.isDict() ? resources.getDict() : (Dict *)NULL; }

private:

  GBool readBox(Dict *dict, const char *key, PDFRectangle *box);

  PDFRectangle mediaBox;
  PDFRectangle cropBox;
  GBool haveCropBox;
  PDFRectangle bleedBox;
  PDFRectangle trimBox;
  PDFRectangle artBox;
  int rotate;
  Object lastModified;
  Object boxColorInfo;
  Object group;
  Object metadata;
  Object pieceInfo;
  Object separationInfo;
  Object resources;
};

//------------------------------------------------------------------------
// Page
//------------------------------------------------------------------------

class Page {
public:

  // Constructor.
  Page(XRef *xrefA, int numA, Dict *pageDict, PageAttrs *attrsA);

  // Destructor.
  ~Page();

  // Is page valid?
  GBool isOk() { return ok; }

  // Get page parameters.
  int getNum() { return num; }
  PDFRectangle *getMediaBox() { return attrs->getMediaBox(); }
  PDFRectangle *getCropBox() { return attrs->getCropBox(); }
  GBool isCropped() { return attrs->isCropped(); }
  double getMediaWidth() 
    { return attrs->getMediaBox()->x2 - attrs->getMediaBox()->x1; }
  double getMediaHeight()
    { return attrs->getMediaBox()->y2 - attrs->getMediaBox()->y1; }
  double getCropWidth() 
    { return attrs->getCropBox()->x2 - attrs->getCropBox()->x1; }
  double getCropHeight()
    { return attrs->getCropBox()->y2 - attrs->getCropBox()->y1; }
  PDFRectangle *getBleedBox() { return attrs->getBleedBox(); }
  PDFRectangle *getTrimBox() { return attrs->getTrimBox(); }
  PDFRectangle *getArtBox() { return attrs->getArtBox(); }
  int getRotate() { return attrs->getRotate(); }
  GooString *getLastModified() { return attrs->getLastModified(); }
  Dict *getBoxColorInfo() { return attrs->getBoxColorInfo(); }
  Dict *getGroup() { return attrs->getGroup(); }
  Stream *getMetadata() { return attrs->getMetadata(); }
  Dict *getPieceInfo() { return attrs->getPieceInfo(); }
  Dict *getSeparationInfo() { return attrs->getSeparationInfo(); }

  // Get resource dictionary.
  Dict *getResourceDict() { return attrs->getResourceDict(); }

  // Get contents.
  Object *getContents(Object *obj) { return contents.fetch(xref, obj); }

  Gfx *createGfx(OutputDev *out, double hDPI, double vDPI,
		 int rotate, GBool useMediaBox, GBool crop,
		 int sliceX, int sliceY, int sliceW, int sliceH,
		 GBool printing, Catalog *catalog,
		 GBool (*abortCheckCbk)(void *data),
		 void *abortCheckCbkData);

  // Display a page.
  void display(OutputDev *out, double hDPI, double vDPI,
	       int rotate, GBool useMediaBox, GBool crop,
	       GBool printing, Catalog *catalog,
	       GBool (*abortCheckCbk)(void *data) = NULL,
	       void *abortCheckCbkData = NULL);

  // Display part of a page.
  void displaySlice(OutputDev *out, double hDPI, double vDPI,
		    int rotate, GBool useMediaBox, GBool crop,
		    int sliceX, int sliceY, int sliceW, int sliceH,
		    GBool printing, Catalog *catalog,
		    GBool (*abortCheckCbk)(void *data) = NULL,
		    void *abortCheckCbkData = NULL);

  void display(Gfx *gfx);

  void makeBox(double hDPI, double vDPI, int rotate,
	       GBool useMediaBox, GBool upsideDown,
	       double sliceX, double sliceY, double sliceW, double sliceH,
	       PDFRectangle *box, GBool *crop);

private:

  XRef *xref;			// the xref table for this PDF file
  Object pageObj;               // page dictionary
  int num;			// page number
  PageAttrs *attrs;		// page attributes
  Object contents;		// page contents
  GBool ok;			// true if page is valid
};

#endif
