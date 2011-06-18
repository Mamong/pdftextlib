//========================================================================
//
// PDFDoc.cc
//
//========================================================================

//========================================================================
//
// Modified by Guangda Hu, 2011 tarlou.gd@gmail.com
// From the Poppler project - http://poppler.freedesktop.org
//
//========================================================================


#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include "Page.h"
#include "Catalog.h"
#include "Stream.h"
#include "XRef.h"
#include "Linearization.h"
#include "OutputDev.h"
#include "Error.h"
#include "ErrorCodes.h"
#include "Lexer.h"
#include "Parser.h"
#include "SecurityHandler.h"
#include "Decrypt.h"
#include "PDFDoc.h"
#include "Hints.h"

//------------------------------------------------------------------------

#define headerSearchSize 1024	// read this many bytes at beginning of
				//   file to look for '%PDF'

#define linearizationSearchSize 1024	// read this many bytes at beginning of
					// file to look for linearization
					// dictionary

#define xrefSearchSize 1024	// read this many bytes at end of file
				//   to look for 'startxref'

//------------------------------------------------------------------------
// PDFDoc
//------------------------------------------------------------------------

void PDFDoc::init()
{
  ok = gFalse;
  errCode = errNone;
  file = NULL;
  str = NULL;
  xref = NULL;
  linearization = NULL;
  catalog = NULL;
  hints = NULL;
  startXRefPos = ~(Guint)0;
  secHdlr = NULL;
  pageCache = NULL;
}

PDFDoc::PDFDoc(const char *fileName, const char *ownerPassword, const char *userPassword) {
  Object obj;
  int size = 0;

  init();

  struct stat buf;
  if (stat(fileName, &buf) == 0) {
     size = buf.st_size;
  }

  // try to open file
  file = fopen(fileName, "rb");
  if (file == NULL) {
    // fopen() has failed.
    // Keep a copy of the errno returned by fopen so that it can be 
    // referred to later.
    fopenErrno = errno;
    error(-1, "Couldn't open file '%s': %s.", fileName, strerror(errno));
    errCode = errOpenFile;
    return;
  }

  // create stream
  obj.initNull();
  str = new FileStream(file, 0, gFalse, size, &obj);

  ok = setup(ownerPassword, userPassword);
}

GBool PDFDoc::setup(const char *ownerPassword, const char *userPassword) {
  str->setPos(0, -1);
  if (str->getPos() < 0)
  {
    error(-1, "Document base stream is not seekable");
    return gFalse;
  }

  str->reset();

  // check footer
  // Adobe does not seem to enforce %%EOF, so we do the same
//  if (!checkFooter()) return gFalse;
  
  // check header
  checkHeader();

  GBool wasReconstructed = false;

  // read xref table
  xref = new XRef(str, getStartXRef(), getMainXRefEntriesOffset(), &wasReconstructed);
  if (!xref->isOk()) {
    error(-1, "Couldn't read xref table");
    errCode = xref->getErrorCode();
    return gFalse;
  }

  // check for encryption
  if (!checkEncryption(ownerPassword, userPassword)) {
    errCode = errEncrypted;
    return gFalse;
  }

  // read catalog
  catalog = new Catalog(xref);
  if (catalog && !catalog->isOk()) {
    if (!wasReconstructed)
    {
      // try one more time to contruct the Catalog, maybe the problem is damaged XRef 
      delete catalog;
      delete xref;
      xref = new XRef(str, 0, 0, NULL, true);
      catalog = new Catalog(xref);
    }

    if (catalog && !catalog->isOk()) {
      error(-1, "Couldn't read page catalog");
      errCode = errBadCatalog;
      return gFalse;
    }
  }

  // done
  return gTrue;
}

PDFDoc::~PDFDoc() {
  if (pageCache) {
    for (int i = 0; i < getNumPages(); i++) {
      if (pageCache[i]) {
        delete pageCache[i];
      }
    }
    gfree(pageCache);
  }
  delete secHdlr;
  if (catalog) {
    delete catalog;
  }
  if (xref) {
    delete xref;
  }
  if (hints) {
    delete hints;
  }
  if (linearization) {
    delete linearization;
  }
  if (str) {
    delete str;
  }
  if (file) {
    fclose(file);
  }
}


// Check for a %%EOF at the end of this stream
GBool PDFDoc::checkFooter() {
  // we look in the last 1024 chars because Adobe does the same
  char *eof = new char[1025];
  int pos = str->getPos();
  str->setPos(1024, -1);
  int i, ch;
  for (i = 0; i < 1024; i++)
  {
    ch = str->getChar();
    if (ch == EOF)
      break;
    eof[i] = ch;
  }
  eof[i] = '\0';

  bool found = false;
  for (i = i - 5; i >= 0; i--) {
    if (strncmp (&eof[i], "%%EOF", 5) == 0) {
      found = true;
      break;
    }
  }
  if (!found)
  {
    error(-1, "Document has not the mandatory ending %%EOF");
    errCode = errDamaged;
    delete[] eof;
    return gFalse;
  }
  delete[] eof;
  str->setPos(pos);
  return gTrue;
}
  
// Check for a PDF header on this stream.  Skip past some garbage
// if necessary.
void PDFDoc::checkHeader() {
  char hdrBuf[headerSearchSize+1];
  char *p;
  char *tokptr;
  int i;

  pdfMajorVersion = 0;
  pdfMinorVersion = 0;
  for (i = 0; i < headerSearchSize; ++i) {
    hdrBuf[i] = str->getChar();
  }
  hdrBuf[headerSearchSize] = '\0';
  for (i = 0; i < headerSearchSize - 5; ++i) {
    if (!strncmp(&hdrBuf[i], "%PDF-", 5)) {
      break;
    }
  }
  if (i >= headerSearchSize - 5) {
    error(-1, "May not be a PDF file (continuing anyway)");
    return;
  }
  str->moveStart(i);
  if (!(p = strtok_r(&hdrBuf[i+5], " \t\n\r", &tokptr))) {
    error(-1, "May not be a PDF file (continuing anyway)");
    return;
  }
  sscanf(p, "%d.%d", &pdfMajorVersion, &pdfMinorVersion);
  // We don't do the version check. Don't add it back in.
}

GBool PDFDoc::checkEncryption(const char *ownerPassword, const char *userPassword) {
  Object encrypt;
  GBool encrypted;
  GBool ret;

  xref->getTrailerDict()->dictLookup("Encrypt", &encrypt);
  if ((encrypted = encrypt.isDict())) {
    if ((secHdlr = SecurityHandler::make(this, &encrypt))) {
      if (secHdlr->checkEncryption(ownerPassword, userPassword)) {
	// authorization succeeded
       	xref->setEncryption(secHdlr->getPermissionFlags(),
			    secHdlr->getOwnerPasswordOk(),
			    secHdlr->getFileKey(),
			    secHdlr->getFileKeyLength(),
			    secHdlr->getEncVersion(),
			    secHdlr->getEncRevision(),
			    secHdlr->getEncAlgorithm());
	ret = gTrue;
      } else {
	// authorization failed
	ret = gFalse;
      }
    } else {
      // couldn't find the matching security handler
      ret = gFalse;
    }
  } else {
    // document is not encrypted
    ret = gTrue;
  }
  encrypt.free();
  return ret;
}

void PDFDoc::displayPage(OutputDev *out, int page,
			 double hDPI, double vDPI, int rotate,
			 GBool useMediaBox, GBool crop, GBool printing,
			 GBool (*abortCheckCbk)(void *data),
			 void *abortCheckCbkData) {
  if (getPage(page))
    getPage(page)->display(out, hDPI, vDPI,
				    rotate, useMediaBox, crop, printing, catalog,
				    abortCheckCbk, abortCheckCbkData);
}

void PDFDoc::displayPages(OutputDev *out, int firstPage, int lastPage,
			  double hDPI, double vDPI, int rotate,
			  GBool useMediaBox, GBool crop, GBool printing,
			  GBool (*abortCheckCbk)(void *data),
			  void *abortCheckCbkData) {
  int page;

  for (page = firstPage; page <= lastPage; ++page) {
    displayPage(out, page, hDPI, vDPI, rotate, useMediaBox, crop, printing,
		abortCheckCbk, abortCheckCbkData);
  }
}

void PDFDoc::displayPageSlice(OutputDev *out, int page,
			      double hDPI, double vDPI, int rotate,
			      GBool useMediaBox, GBool crop, GBool printing,
			      int sliceX, int sliceY, int sliceW, int sliceH,
			      GBool (*abortCheckCbk)(void *data),
			      void *abortCheckCbkData) {
  if (getPage(page))
    getPage(page)->displaySlice(out, hDPI, vDPI,
					 rotate, useMediaBox, crop,
					 sliceX, sliceY, sliceW, sliceH,
					 printing, catalog,
					 abortCheckCbk, abortCheckCbkData);
}

Linearization *PDFDoc::getLinearization()
{
  if (!linearization) {
    linearization = new Linearization(str);
  }
  return linearization;
}

GBool PDFDoc::isLinearized() {
  if ((str->getLength()) &&
      (getLinearization()->getLength() == str->getLength()))
    return gTrue;
  else
    return gFalse;
}

Hints *PDFDoc::getHints()
{
  if (!hints && isLinearized()) {
    hints = new Hints(str, getLinearization(), getXRef(), secHdlr);
  }

  return hints;
}

Guint PDFDoc::strToUnsigned(char *s) {
  Guint x;
  char *p;
  int i;

  x = 0;
  for (p = s, i = 0; *p && isdigit(*p) && i < 10; ++p, ++i) {
    x = 10 * x + (*p - '0');
  }
  return x;
}

// Read the 'startxref' position.
Guint PDFDoc::getStartXRef()
{
	char *buf = NULL;
  if (startXRefPos == ~(Guint)0) {

    if (isLinearized()) {
      buf = new char[linearizationSearchSize + 1];
      int c, n, i;

      str->setPos(0);
      for (n = 0; n < linearizationSearchSize; ++n) {
        if ((c = str->getChar()) == EOF) {
          break;
        }
        buf[n] = c;
      }
      buf[n] = '\0';

      // find end of first obj
      startXRefPos = 0;
      for (i = 0; i < n; i++) {
        if (!strncmp("endobj", &buf[i], 6)) {
           startXRefPos = i+6;
           break;
        }
      }
    } else {
      buf = new char[xrefSearchSize + 1];
      char *p;
      int c, n, i;

      // read last xrefSearchSize bytes
      str->setPos(xrefSearchSize, -1);
      for (n = 0; n < xrefSearchSize; ++n) {
        if ((c = str->getChar()) == EOF) {
          break;
        }
        buf[n] = c;
      }
      buf[n] = '\0';

      // find startxref
      for (i = n - 9; i >= 0; --i) {
        if (!strncmp(&buf[i], "startxref", 9)) {
          break;
        }
      }
      if (i < 0) {
        startXRefPos = 0;
      }
      for (p = &buf[i+9]; isspace(*p); ++p) ;
      startXRefPos =  strToUnsigned(p);
    }

  }
	if (buf) delete [] buf;
  return startXRefPos;
}

Guint PDFDoc::getMainXRefEntriesOffset()
{
  Guint mainXRefEntriesOffset = 0;

  if (isLinearized()) {
    mainXRefEntriesOffset = getLinearization()->getMainXRefEntriesOffset();
  }

  return mainXRefEntriesOffset;
}

int PDFDoc::getNumPages()
{
  if (isLinearized()) {
    int n;
    if ((n = getLinearization()->getNumPages())) {
      return n;
    }
  }

  return catalog->getNumPages();
}

Page *PDFDoc::parsePage(int page)
{
  Page *p = NULL;
  Object obj;
  Ref pageRef;
  Dict *pageDict;

  pageRef.num = getHints()->getPageObjectNum(page);
  if (!pageRef.num) {
    error(-1, "Failed to get object num from hint tables for page %d", page);
    return NULL;
  }

  // check for bogus ref - this can happen in corrupted PDF files
  if (pageRef.num < 0 || pageRef.num >= xref->getNumObjects()) {
    error(-1, "Invalid object num (%d) for page %d", pageRef.num, page);
    return NULL;
  }

  pageRef.gen = xref->getEntry(pageRef.num)->gen;
  xref->fetch(pageRef.num, pageRef.gen, &obj);
  if (!obj.isDict()) {
    obj.free();
    error(-1, "Object (%d %d) is not a pageDict", pageRef.num, pageRef.gen);
    return NULL;
  }
  pageDict = obj.getDict();

  p = new Page(xref, page, pageDict, new PageAttrs(NULL, pageDict));
  obj.free();

  return p;
}

Page *PDFDoc::getPage(int page)
{
  if ((page < 1) || page > getNumPages()) return NULL;

  if (isLinearized()) {
    if (!pageCache) {
      pageCache = (Page **) gmallocn(getNumPages(), sizeof(Page *));
      for (int i = 0; i < getNumPages(); i++) {
        pageCache[i] = NULL;
      }
    }
    if (!pageCache[page-1]) {
      pageCache[page-1] = parsePage(page);
    }
    if (pageCache[page-1]) {
       return pageCache[page-1];
    } else {
       error(-1, "Failed parsing page %d using hint tables", page);
    }
  }

  return catalog->getPage(page);
}
