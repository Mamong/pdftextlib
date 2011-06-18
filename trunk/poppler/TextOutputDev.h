//========================================================================
//
// TextOutputDev.h
//
//========================================================================

//========================================================================
//
// Modified by Guangda Hu, 2011 tarlou.gd@gmail.com
// From the Poppler project - http://poppler.freedesktop.org
//
//========================================================================

#ifndef TEXTOUTPUTDEV_H
#define TEXTOUTPUTDEV_H

#ifdef USE_GCC_PRAGMAS
#pragma interface
#endif

#include "gtypes.h"
#include "OutputDev.h"

class GooString;
class GooList;
class Gfx;
class GfxFont;
class GfxState;
class PDFDoc;

class TextWord;
class TextPool;
class TextLine;
class TextBlock;
class TextPage;

//------------------------------------------------------------------------
// TextFontInfo
//------------------------------------------------------------------------

class TextFontInfo {
private:
	TextFontInfo(GfxState *state);
	~TextFontInfo();
	GBool matches(GfxState *state);
	GfxFont *gfxFont;
	friend class TextWord;
	friend class TextPool;
	friend class TextLine;
	friend class TextBlock;
	friend class TextPage;
};

//------------------------------------------------------------------------
// TextWord
//------------------------------------------------------------------------

class TextWord {
private:
	TextWord(GfxState *state, int rotA, double x0, double y0, int charPosA,
			 TextFontInfo *fontA, double fontSize);
	~TextWord();
	void addChar(GfxState *state, double x, double y, double dx, double dy,
				 CharCode c, Unicode u);
	void merge(TextWord *word);
	int primaryCmp(TextWord *word);
	double primaryDelta(TextWord *word);
	TextWord *nextWord();
	// str must be normlized, if caseSen = gFalse, it must also be uppercase.
	GBool startWith(Unicode *str, int length, GBool caseSen);
	GBool endWith(Unicode *str, int length, GBool caseSen);
	GBool strEq(Unicode *str, int length, GBool caseSen);
	GBool contain(Unicode *str, int length, GBool caseSen);
	
	TextLine *line;
	TextWord *next;
	TextWord *prev;
	double xMin, xMax, yMin, yMax;
	double xMinPre, xMaxPre, yMinPre, yMaxPre;
	double xMinPost, xMaxPost, yMinPost, yMaxPost;
	int index;
	
	Unicode *text;
	Unicode *norm;
	double *edge;
	int len;
	int size;
	int rot;
	double base;
	int charPos;
	int charLen;
	int normLen;
	TextFontInfo *font;
	double fontSize;
	GBool spaceAfter;

	friend class TextPool;
	friend class TextLine;
	friend class TextBlock;
	friend class TextPage;
};

//------------------------------------------------------------------------
// TextPool
//------------------------------------------------------------------------

class TextPool {
private:
	TextPool();
	~TextPool();
	TextWord *getPool(int baseIdx) { return pool[baseIdx - minBaseIdx]; }
	void setPool(int baseIdx, TextWord *p) { pool[baseIdx - minBaseIdx] = p; }
	int getBaseIdx(double base);
	void addWord(TextWord *word);
	int minBaseIdx;
	int maxBaseIdx;
	TextWord **pool;
	TextWord *cursor;
	int cursorBaseIdx;
	friend class TextWord;
	friend class TextLine;
	friend class TextBlock;
	friend class TextPage;
};

//------------------------------------------------------------------------
// TextLine
//------------------------------------------------------------------------

class TextLine {
private:
	TextLine(TextBlock *blkA, int rotA, double baseA);
	~TextLine();
	void addWord(TextWord *word);
	int primaryCmp(TextLine *line);
	int secondaryCmp(TextLine *line);
	int cmpYX(TextLine *line);
	void coalesce();
	
	TextBlock *blk;
	TextLine *next;
	TextLine *prev;
	TextWord *words;
	TextWord *lastWord;
	double xMin, xMax, yMin, yMax;
	double xMinPre, xMaxPre, yMinPre, yMaxPre;
	double xMinPost, xMaxPost, yMinPost, yMaxPost;
	
	int rot;
	double base;
	int charCount;
	
	friend class TextWord;
	friend class TextPool;
	friend class TextBlock;
	friend class TextPage;
};

//------------------------------------------------------------------------
// TextBlock
//------------------------------------------------------------------------

class TextBlock {
private:
	TextBlock(TextPage *pageA, int rotA);
	~TextBlock();
	void addWord(TextWord *word);
	void coalesce();
	void updatePriMinMax(TextBlock *blk);
	static int cmpXYPrimaryRot(const void *p1, const void *p2);
	double secondaryDelta(TextBlock *blk);
	GBool isBelow(TextBlock *blk);
	GBool isBeforeByRule1(TextBlock *blk1);
	GBool isBeforeByRule2(TextBlock *blk1);
	int visitDepthFirst(TextBlock *blkList, int pos1, TextBlock **sorted, int sortPos,
						GBool *visited);
	
	TextPage *page;
	TextBlock *next;
	TextBlock *prev;
	TextLine *lines;
	TextLine *lastLine;
	
	double xMin, xMax, yMin, yMax;
	double xMinPre, xMaxPre, yMinPre, yMaxPre;
	double xMinPost, xMaxPost, yMinPost, yMaxPost;
	
	int rot;
	int charCount;
	double priMin, priMax;
	double ExMin, ExMax, EyMin, EyMax;
	int tableId;
	GBool tableEnd;
	TextPool *pool;
	
	friend class TextWord;
	friend class TextPool;
	friend class TextLine;
	friend class TextPage;
};

//------------------------------------------------------------------------
// TextPage
//------------------------------------------------------------------------

class TextPage : public OutputDev {
public:
	TextPage(PDFDoc *doc, int pageNum);
	~TextPage();
	virtual GBool isOk() { return ok; }
	virtual GBool upsideDown() { return gTrue; }
	virtual GBool useDrawChar() { return gTrue; }
	virtual GBool interpretType3Chars() { return gFalse; }
	virtual GBool needNonText() { return gFalse; }
	virtual void startPage(int pageNum, GfxState *state);
	virtual void endPage();
	virtual void updateFont(GfxState *state);
	virtual void drawChar(GfxState *state, double x, double y, double dx, double dy,
						  double originX, double originY, CharCode c, int nBytes,
						  Unicode *u, int uLen);
	virtual void beginMarkedContent(const char *name, Dict *properties);
	virtual void endMarkedContent(GfxState *state);
	GooList *searchText(Unicode *str, int length, GBool caseSen);
	void startSelection(double x, double y);
	GBool moveSelEndTo(double x, double y);
	int getSelStartIdx() { return selStart ? selStart->index + selIdx1 : -1; }
	int getSelEndIdx() { return selEnd ? selEnd->index + selIdx2 : -1; }
	GooList *getSelectedRegion();
	Unicode *getSelectedText(GBool normalize, int *length);
	
private:
	void beginWord(GfxState *state, double x0, double y0);
	void addChar(GfxState *state, double x, double y, double dx, double dy, CharCode c,
				 int nBytes, Unicode *u, int uLen);
	void appendSpace();
	void endWord();
	void addWord(TextWord *word);
	void coalesce();
	TextWord *findNearest(double x, double y, TextWord *start = NULL);
	int calIdx(double x, double y, TextWord *&word);
	
	int selIdx1, selIdx2, selIdxSave;
	TextWord *selStart, *selEnd;
	
	double pageWidth, pageHeight;
	TextBlock *blocks;
	TextBlock *lastBlk;
	int primaryRot;
	GBool primaryLR;
	
	GBool ok;
	TextWord *curWord;
	int charPos;
	TextFontInfo *curFont;
	double curFontSize;
	int nest;
	int nTinyChars;
	GBool lastCharOverlap;
	TextPool *pools[4];
	GooList *fonts;
	int actualTextBMCLevel;
	GooString *actualText;
	GBool newActualTextSpan;
	double actualText_x, actualText_y;
	double actualText_dx, actualText_dy;
	
	friend class TextWord;
	friend class TextPool;
	friend class TextLine;
	friend class TextBlock;
};

#endif
