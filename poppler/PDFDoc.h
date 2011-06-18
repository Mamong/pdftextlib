//========================================================================
//
// PDFDoc.h
//
//========================================================================

//========================================================================
//
// Modified by Guangda Hu, 2011 tarlou.gd@gmail.com
// From the Poppler project - http://poppler.freedesktop.org
//
//========================================================================


#ifndef PDFDOC_H
#define PDFDOC_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include <stdio.h>
#include "XRef.h"
#include "Catalog.h"
#include "Page.h"
#include "OptionalContent.h"

class BaseStream;
class OutputDev;
class Outline;
class Linearization;
class SecurityHandler;
class Hints;

//------------------------------------------------------------------------
// PDFDoc
//------------------------------------------------------------------------

class PDFDoc {
public:

  PDFDoc(const char *fileName, const char *ownerPassword = NULL, const char *userPassword = NULL);

  ~PDFDoc();

  // Was PDF document successfully opened?
  GBool isOk() { return ok; }

  // Get the error code (if isOk() returns false).
  int getErrorCode() { return errCode; }

  // Get the error code returned by fopen() (if getErrorCode() == 
  // errOpenFile).
  int getFopenErrno() { return fopenErrno; }

  // Get the linearization table.
  Linearization *getLinearization();

  // Get the xref table.
  XRef *getXRef() { return xref; }

  // Get catalog.
  Catalog *getCatalog() { return catalog; }

  // Get optional content configuration
  OCGs *getOptContentConfig() { return catalog->getOptContentConfig(); }

  // Get base stream.
  BaseStream *getBaseStream() { return str; }

  // Get page parameters.
  double getPageMediaWidth(int page)
    { return getPage(page) ? getPage(page)->getMediaWidth() : 0.0 ; }
  double getPageMediaHeight(int page)
    { return getPage(page) ? getPage(page)->getMediaHeight() : 0.0 ; }
  double getPageCropWidth(int page)
    { return getPage(page) ? getPage(page)->getCropWidth() : 0.0 ; }
  double getPageCropHeight(int page)
    { return getPage(page) ? getPage(page)->getCropHeight() : 0.0 ; }
  int getPageRotate(int page)
    { return getPage(page) ? getPage(page)->getRotate() : 0 ; }

  // Get number of pages.
  int getNumPages();

  // Get page.
  Page *getPage(int page);

  // Display a page.
  void displayPage(OutputDev *out, int page,
		   double hDPI, double vDPI, int rotate,
		   GBool useMediaBox, GBool crop, GBool printing,
		   GBool (*abortCheckCbk)(void *data) = NULL,
		   void *abortCheckCbkData = NULL);

  // Display a range of pages.
  void displayPages(OutputDev *out, int firstPage, int lastPage,
		    double hDPI, double vDPI, int rotate,
		    GBool useMediaBox, GBool crop, GBool printing,
		    GBool (*abortCheckCbk)(void *data) = NULL,
		    void *abortCheckCbkData = NULL);

  // Display part of a page.
  void displayPageSlice(OutputDev *out, int page,
			double hDPI, double vDPI, int rotate, 
			GBool useMediaBox, GBool crop, GBool printing,
			int sliceX, int sliceY, int sliceW, int sliceH,
			GBool (*abortCheckCbk)(void *data) = NULL,
			void *abortCheckCbkData = NULL);

  // Is the file encrypted?
  GBool isEncrypted() { return xref->isEncrypted(); }

  // Is this document linearized?
  GBool isLinearized();

  // Return the document's Info dictionary (if any).
  Object *getDocInfo(Object *obj) { return xref->getDocInfo(obj); }
  Object *getDocInfoNF(Object *obj) { return xref->getDocInfoNF(obj); }

  // Return the PDF version specified by the file.
  int getPDFMajorVersion() { return pdfMajorVersion; }
  int getPDFMinorVersion() { return pdfMinorVersion; }

private:
  Page *parsePage(int page);

  // Get hints.
  Hints *getHints();
  void init();
  GBool setup(const char *ownerPassword, const char *userPassword);
  GBool checkFooter();
  void checkHeader();
  GBool checkEncryption(const char *ownerPassword, const char *userPassword);
  // Get the offset of the start xref table.
  Guint getStartXRef();
  // Get the offset of the entries in the main XRef table of a
  // linearized document (0 for non linearized documents).
  Guint getMainXRefEntriesOffset();
  Guint strToUnsigned(char *s);

  FILE *file;
  BaseStream *str;
  void *guiData;
  int pdfMajorVersion;
  int pdfMinorVersion;
  Linearization *linearization;
  XRef *xref;
  SecurityHandler *secHdlr;
  Catalog *catalog;
  Hints *hints;
  Page **pageCache;

  GBool ok;
  int errCode;
  //If there is an error opening the PDF file with fopen() in the constructor, 
  //then the POSIX errno will be here.
  int fopenErrno;

  Guint startXRefPos;		// offset of last xref table
};

#endif
