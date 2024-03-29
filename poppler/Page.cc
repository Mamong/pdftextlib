//========================================================================
//
// Page.cc
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
// Copyright (C) 2005 Jeff Muizelaar <jeff@infidigm.net>
// Copyright (C) 2005-2010 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2006-2008 Pino Toscano <pino@kde.org>
// Copyright (C) 2006 Nickolay V. Shmyrev <nshmyrev@yandex.ru>
// Copyright (C) 2006 Scott Turner <scotty1024@mac.com>
// Copyright (C) 2006-2010 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2007 Julien Rebetez <julienr@svn.gnome.org>
// Copyright (C) 2008 Iñigo Martínez <inigomartinez@gmail.com>
// Copyright (C) 2008 Brad Hards <bradh@kde.org>
// Copyright (C) 2008 Ilya Gorenbein <igorenbein@finjan.com>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <stddef.h>
#include <limits.h>
#include "Object.h"
#include "Array.h"
#include "Dict.h"
#include "XRef.h"
#include "OutputDev.h"
#include "Gfx.h"
#include "GfxState.h"
#include "Error.h"
#include "Page.h"
#include "Catalog.h"

//------------------------------------------------------------------------
// PDFRectangle
//------------------------------------------------------------------------

void PDFRectangle::clipTo(PDFRectangle *rect) {
  if (x1 < rect->x1) {
    x1 = rect->x1;
  } else if (x1 > rect->x2) {
    x1 = rect->x2;
  }
  if (x2 < rect->x1) {
    x2 = rect->x1;
  } else if (x2 > rect->x2) {
    x2 = rect->x2;
  }
  if (y1 < rect->y1) {
    y1 = rect->y1;
  } else if (y1 > rect->y2) {
    y1 = rect->y2;
  }
  if (y2 < rect->y1) {
    y2 = rect->y1;
  } else if (y2 > rect->y2) {
    y2 = rect->y2;
  }
}

//------------------------------------------------------------------------
// PageAttrs
//------------------------------------------------------------------------

PageAttrs::PageAttrs(PageAttrs *attrs, Dict *dict) {
  Object obj1;
  PDFRectangle mBox;
  const GBool isPage = dict->is("Page");

  // get old/default values
  if (attrs) {
    mediaBox = attrs->mediaBox;
    cropBox = attrs->cropBox;
    haveCropBox = attrs->haveCropBox;
    rotate = attrs->rotate;
    attrs->resources.copy(&resources);
  } else {
    // set default MediaBox to 8.5" x 11" -- this shouldn't be necessary
    // but some (non-compliant) PDF files don't specify a MediaBox
    mediaBox.x1 = 0;
    mediaBox.y1 = 0;
    mediaBox.x2 = 612;
    mediaBox.y2 = 792;
    cropBox.x1 = cropBox.y1 = cropBox.x2 = cropBox.y2 = 0;
    haveCropBox = gFalse;
    rotate = 0;
    resources.initNull();
  }

  // media box
  if (readBox(dict, "MediaBox", &mBox)) {
    mediaBox = mBox;
  }

  // crop box
  if (readBox(dict, "CropBox", &cropBox)) {
    haveCropBox = gTrue;
  }
  if (!haveCropBox) {
    cropBox = mediaBox;
  }

  if (isPage) {
    // cropBox can not be bigger than mediaBox
    if (cropBox.x2 - cropBox.x1 > mediaBox.x2 - mediaBox.x1)
    {
      cropBox.x1 = mediaBox.x1;
      cropBox.x2 = mediaBox.x2;
    }
    if (cropBox.y2 - cropBox.y1 > mediaBox.y2 - mediaBox.y1)
    {
      cropBox.y1 = mediaBox.y1;
      cropBox.y2 = mediaBox.y2;
    }
  }

  // other boxes
  bleedBox = cropBox;
  readBox(dict, "BleedBox", &bleedBox);
  trimBox = cropBox;
  readBox(dict, "TrimBox", &trimBox);
  artBox = cropBox;
  readBox(dict, "ArtBox", &artBox);

  if (isPage) {
    // clip all other boxes to the media box
    cropBox.clipTo(&mediaBox);
    bleedBox.clipTo(&mediaBox);
    trimBox.clipTo(&mediaBox);
    artBox.clipTo(&mediaBox);
  }

  // rotate
  dict->lookup("Rotate", &obj1);
  if (obj1.isInt()) {
    rotate = obj1.getInt();
  }
  obj1.free();
  while (rotate < 0) {
    rotate += 360;
  }
  while (rotate >= 360) {
    rotate -= 360;
  }

  // misc attributes
  dict->lookup("LastModified", &lastModified);
  dict->lookup("BoxColorInfo", &boxColorInfo);
  dict->lookup("Group", &group);
  dict->lookup("Metadata", &metadata);
  dict->lookup("PieceInfo", &pieceInfo);
  dict->lookup("SeparationInfo", &separationInfo);

  // resource dictionary
  dict->lookup("Resources", &obj1);
  if (obj1.isDict()) {
    resources.free();
    obj1.copy(&resources);
  }
  obj1.free();
}

PageAttrs::~PageAttrs() {
  lastModified.free();
  boxColorInfo.free();
  group.free();
  metadata.free();
  pieceInfo.free();
  separationInfo.free();
  resources.free();
}

GBool PageAttrs::readBox(Dict *dict, const char *key, PDFRectangle *box) {
  PDFRectangle tmp;
  double t;
  Object obj1, obj2;
  GBool ok;

  dict->lookup(key, &obj1);
  if (obj1.isArray() && obj1.arrayGetLength() == 4) {
    ok = gTrue;
    obj1.arrayGet(0, &obj2);
    if (obj2.isNum()) {
      tmp.x1 = obj2.getNum();
    } else {
      ok = gFalse;
    }
    obj2.free();
    obj1.arrayGet(1, &obj2);
    if (obj2.isNum()) {
      tmp.y1 = obj2.getNum();
    } else {
      ok = gFalse;
    }
    obj2.free();
    obj1.arrayGet(2, &obj2);
    if (obj2.isNum()) {
      tmp.x2 = obj2.getNum();
    } else {
      ok = gFalse;
    }
    obj2.free();
    obj1.arrayGet(3, &obj2);
    if (obj2.isNum()) {
      tmp.y2 = obj2.getNum();
    } else {
      ok = gFalse;
    }
    obj2.free();
    if (tmp.x1 == 0 && tmp.x2 == 0 && tmp.y1 == 0 && tmp.y2 == 0)
      ok = gFalse;
    if (ok) {
      if (tmp.x1 > tmp.x2) {
	t = tmp.x1; tmp.x1 = tmp.x2; tmp.x2 = t;
      }
      if (tmp.y1 > tmp.y2) {
	t = tmp.y1; tmp.y1 = tmp.y2; tmp.y2 = t;
      }
      *box = tmp;
    }
  } else {
    ok = gFalse;
  }
  obj1.free();
  return ok;
}

//------------------------------------------------------------------------
// Page
//------------------------------------------------------------------------

Page::Page(XRef *xrefA, int numA, Dict *pageDict, PageAttrs *attrsA) {
  ok = gFalse;
  xref = xrefA;
  num = numA;

  pageObj.initDict(pageDict);

  // get attributes
  attrs = attrsA;

  // contents
  pageDict->lookupNF("Contents", &contents);
  if (!(contents.isRef() || contents.isArray() ||
	contents.isNull())) {
    error(-1, "Page contents object (page %d) is wrong type (%s)",
	  num, contents.getTypeName());
    contents.free();
    contents.initNull();
    return;
  }
  ok = gTrue;
  return;
}

Page::~Page() {
  delete attrs;
  pageObj.free();
  contents.free();
}

void Page::display(OutputDev *out, double hDPI, double vDPI,
		   int rotate, GBool useMediaBox, GBool crop,
		   GBool printing, Catalog *catalog,
		   GBool (*abortCheckCbk)(void *data),
		   void *abortCheckCbkData) {
  displaySlice(out, hDPI, vDPI, rotate, useMediaBox, crop, -1, -1, -1, -1, printing, catalog,
	       abortCheckCbk, abortCheckCbkData);
}

Gfx *Page::createGfx(OutputDev *out, double hDPI, double vDPI,
		     int rotate, GBool useMediaBox, GBool crop,
		     int sliceX, int sliceY, int sliceW, int sliceH,
		     GBool printing, Catalog *catalog,
		     GBool (*abortCheckCbk)(void *data),
		     void *abortCheckCbkData) {
  PDFRectangle *mediaBox, *cropBox;
  PDFRectangle box;
  Gfx *gfx;

  rotate += getRotate();
  if (rotate >= 360) {
    rotate -= 360;
  } else if (rotate < 0) {
    rotate += 360;
  }

  makeBox(hDPI, vDPI, rotate, useMediaBox, out->upsideDown(),
	  sliceX, sliceY, sliceW, sliceH, &box, &crop);
  cropBox = getCropBox();
  mediaBox = getMediaBox();

#if 0
    printf("***** MediaBox = ll:%g,%g ur:%g,%g\n",
	    mediaBox->x1, mediaBox->y1, mediaBox->x2, mediaBox->y2);
      printf("***** CropBox = ll:%g,%g ur:%g,%g\n",
	     cropBox->x1, cropBox->y1, cropBox->x2, cropBox->y2);
    printf("***** Rotate = %d\n", attrs->getRotate());
#endif

  gfx = new Gfx(xref, out, num, attrs->getResourceDict(), catalog,
		hDPI, vDPI, &box, crop ? cropBox : (PDFRectangle *)NULL,
		rotate, abortCheckCbk, abortCheckCbkData);

  return gfx;
}

void Page::displaySlice(OutputDev *out, double hDPI, double vDPI,
			int rotate, GBool useMediaBox, GBool crop,
			int sliceX, int sliceY, int sliceW, int sliceH,
			GBool printing, Catalog *catalog,
			GBool (*abortCheckCbk)(void *data),
			void *abortCheckCbkData) {
  Gfx *gfx;
  Object obj;
  
  if (!out->checkPageSlice(this, hDPI, vDPI, rotate, useMediaBox, crop,
			   sliceX, sliceY, sliceW, sliceH,
			   printing, catalog,
			   abortCheckCbk, abortCheckCbkData)) {
    return;
  }

  gfx = createGfx(out, hDPI, vDPI, rotate, useMediaBox, crop,
		  sliceX, sliceY, sliceW, sliceH,
		  printing, catalog,
		  abortCheckCbk, abortCheckCbkData);

  contents.fetch(xref, &obj);
  if (!obj.isNull()) {
    gfx->saveState();
    gfx->display(&obj);
    gfx->restoreState();
  }
  obj.free();
  delete gfx;
}

void Page::display(Gfx *gfx) {
  Object obj;

  contents.fetch(xref, &obj);
  if (!obj.isNull()) {
    gfx->saveState();
    gfx->display(&obj);
    gfx->restoreState();
  }
  obj.free();
}

void Page::makeBox(double hDPI, double vDPI, int rotate,
		   GBool useMediaBox, GBool upsideDown,
		   double sliceX, double sliceY, double sliceW, double sliceH,
		   PDFRectangle *box, GBool *crop) {
  PDFRectangle *mediaBox, *cropBox, *baseBox;
  double kx, ky;

  mediaBox = getMediaBox();
  cropBox = getCropBox();
  if (sliceW >= 0 && sliceH >= 0) {
    baseBox = useMediaBox ? mediaBox : cropBox;
    kx = 72.0 / hDPI;
    ky = 72.0 / vDPI;
    if (rotate == 90) {
      if (upsideDown) {
	box->x1 = baseBox->x1 + ky * sliceY;
	box->x2 = baseBox->x1 + ky * (sliceY + sliceH);
      } else {
	box->x1 = baseBox->x2 - ky * (sliceY + sliceH);
	box->x2 = baseBox->x2 - ky * sliceY;
      }
      box->y1 = baseBox->y1 + kx * sliceX;
      box->y2 = baseBox->y1 + kx * (sliceX + sliceW);
    } else if (rotate == 180) {
      box->x1 = baseBox->x2 - kx * (sliceX + sliceW);
      box->x2 = baseBox->x2 - kx * sliceX;
      if (upsideDown) {
	box->y1 = baseBox->y1 + ky * sliceY;
	box->y2 = baseBox->y1 + ky * (sliceY + sliceH);
      } else {
	box->y1 = baseBox->y2 - ky * (sliceY + sliceH);
	box->y2 = baseBox->y2 - ky * sliceY;
      }
    } else if (rotate == 270) {
      if (upsideDown) {
	box->x1 = baseBox->x2 - ky * (sliceY + sliceH);
	box->x2 = baseBox->x2 - ky * sliceY;
      } else {
	box->x1 = baseBox->x1 + ky * sliceY;
	box->x2 = baseBox->x1 + ky * (sliceY + sliceH);
      }
      box->y1 = baseBox->y2 - kx * (sliceX + sliceW);
      box->y2 = baseBox->y2 - kx * sliceX;
    } else {
      box->x1 = baseBox->x1 + kx * sliceX;
      box->x2 = baseBox->x1 + kx * (sliceX + sliceW);
      if (upsideDown) {
	box->y1 = baseBox->y2 - ky * (sliceY + sliceH);
	box->y2 = baseBox->y2 - ky * sliceY;
      } else {
	box->y1 = baseBox->y1 + ky * sliceY;
	box->y2 = baseBox->y1 + ky * (sliceY + sliceH);
      }
    }
  } else if (useMediaBox) {
    *box = *mediaBox;
  } else {
    *box = *cropBox;
    *crop = gFalse;
  }
}
