//========================================================================
//
// TextOutputDev.cc
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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <float.h>
#include <ctype.h>
#include "gmem.h"
#include "GooString.h"
#include "GooList.h"
#include "Error.h"
#include "UnicodeTypeTable.h"
#include "TextOutputDev.h"
#include "Page.h"
#include "PDFDoc.h"
#include "PDFDocEncoding.h"
#include "GfxFont.h"
#include "GfxState.h"
#include "GlobalParams.h"

//------------------------------------------------------------------------
// parameters
//------------------------------------------------------------------------

// Each bucket in a text pool includes baselines within a range of
// this many points.
#define textPoolStep 4

// Inter-character space width which will cause addChar to start a new
// word.
#define minWordBreakSpace 0.1

// Negative inter-character space width, i.e., overlap, which will
// cause addChar to start a new word.
#define minDupBreakOverlap 0.2

// Max distance between baselines of two lines within a block, as a
// fraction of the font size.
#define maxLineSpacingDelta 1.5

// Max difference in primary font sizes on two lines in the same
// block.  Delta1 is used when examining new lines above and below the
// current block; delta2 is used when examining text that overlaps the
// current block; delta3 is used when examining text to the left and
// right of the current block.
#define maxBlockFontSizeDelta1 0.05
#define maxBlockFontSizeDelta2 0.6
#define maxBlockFontSizeDelta3 0.2

// Max difference in font sizes inside a word.
#define maxWordFontSizeDelta 0.05

// Maximum distance between baselines of two words on the same line,
// e.g., distance between subscript or superscript and the primary
// baseline, as a fraction of the font size.
#define maxIntraLineDelta 0.5

// Minimum inter-word spacing, as a fraction of the font size.  (Only
// used for raw ordering.)
#define minWordSpacing 0.15

// Maximum inter-word spacing, as a fraction of the font size.
#define maxWordSpacing 1.5

// Maximum horizontal spacing which will allow a word to be pulled
// into a block.
#define minColSpacing1 0.3

// Minimum spacing between columns, as a fraction of the font size.
#define minColSpacing2 1.0

// Minimum spacing between characters within a word, as a fraction of
// the font size.
#define minCharSpacing -0.2

// Maximum spacing between characters within a word, as a fraction of
// the font size, when there is no obvious extra-wide character
// spacing.
#define maxCharSpacing 0.03

// When extra-wide character spacing is detected, the inter-character
// space threshold is set to the minimum inter-character space
// multiplied by this constant.
#define maxWideCharSpacingMul 1.3

// Upper limit on spacing between characters in a word.
#define maxWideCharSpacing 0.4

// Max difference in primary,secondary coordinates (as a fraction of
// the font size) allowed for duplicated text (fake boldface, drop
// shadows) which is to be discarded.
#define dupMaxPriDelta 0.1
#define dupMaxSecDelta 0.2

//------------------------------------------------------------------------
// TextFontInfo
//------------------------------------------------------------------------

TextFontInfo::TextFontInfo(GfxState *state) {
	gfxFont = state->getFont();
	if (gfxFont)
		gfxFont->incRefCnt();
}

TextFontInfo::~TextFontInfo() {
	if (gfxFont)
		gfxFont->decRefCnt();
}

GBool TextFontInfo::matches(GfxState *state) {
	return state->getFont() == gfxFont;
}

//------------------------------------------------------------------------
// TextWord
//------------------------------------------------------------------------

TextWord::TextWord(GfxState *state, int rotA, double x0, double y0,
				   int charPosA, TextFontInfo *fontA, double fontSizeA) {
	GfxFont *gfxFont;
	double x, y, ascent, descent;
	
	rot = rotA;
	charPos = charPosA;
	charLen = 0;
	font = fontA;
	fontSize = fontSizeA;
	state->transform(x0, y0, &x, &y);
	if ((gfxFont = font->gfxFont)) {
		ascent = gfxFont->getAscent() * fontSize;
		descent = gfxFont->getDescent() * fontSize;
	} else {
		// this means that the PDF file draws text without a current font,
		// which should never happen
		ascent = 0.95 * fontSize;
		descent = -0.35 * fontSize;
	}
	switch (rot) {
		case 0:
			yMin = y - ascent;
			yMax = y - descent;
			if (yMin == yMax) {
				// this is a sanity check for a case that shouldn't happen -- but
				// if it does happen, we want to avoid dividing by zero later
				yMin = y;
				yMax = y + 1;
			}
			base = y;
			break;
		case 1:
			xMin = x + descent;
			xMax = x + ascent;
			if (xMin == xMax) {
				// this is a sanity check for a case that shouldn't happen -- but
				// if it does happen, we want to avoid dividing by zero later
				xMin = x;
				xMax = x + 1;
			}
			base = x;
			break;
		case 2:
			yMin = y + descent;
			yMax = y + ascent;
			if (yMin == yMax) {
				// this is a sanity check for a case that shouldn't happen -- but
				// if it does happen, we want to avoid dividing by zero later
				yMin = y;
				yMax = y + 1;
			}
			base = y;
			break;
		case 3:
			xMin = x - ascent;
			xMax = x - descent;
			if (xMin == xMax) {
				// this is a sanity check for a case that shouldn't happen -- but
				// if it does happen, we want to avoid dividing by zero later
				xMin = x;
				xMax = x + 1;
			}
			base = x;
			break;
	}
	text = NULL;
	edge = NULL;
	len = size = 0;
	spaceAfter = gFalse;
	next = NULL;
	prev = NULL;
	line = NULL;
	norm = NULL;
}

TextWord::~TextWord() {
	gfree(text);
	gfree(edge);
	gfree(norm);
}

void TextWord::addChar(GfxState *state, double x, double y,
					   double dx, double dy, CharCode c, Unicode u) {
	if (len == size) {
		size += 16;
		text = (Unicode *)greallocn(text, size, sizeof(Unicode));
		edge = (double *)greallocn(edge, (size + 1), sizeof(double));
	}
	text[len] = u;
	switch (rot) {
		case 0:
			if (len == 0) {
				xMin = x;
			}
			edge[len] = x;
			xMax = edge[len+1] = x + dx;
			break;
		case 1:
			if (len == 0) {
				yMin = y;
			}
			edge[len] = y;
			yMax = edge[len+1] = y + dy;
			break;
		case 2:
			if (len == 0) {
				xMax = x;
			}
			edge[len] = x;
			xMin = edge[len+1] = x + dx;
			break;
		case 3:
			if (len == 0) {
				yMax = y;
			}
			edge[len] = y;
			yMin = edge[len+1] = y + dy;
			break;
	}
	++len;
}

void TextWord::merge(TextWord *word) {
	int i;
	
	if (word->xMin < xMin) {
		xMin = word->xMin;
	}
	if (word->yMin < yMin) {
		yMin = word->yMin;
	}
	if (word->xMax > xMax) {
		xMax = word->xMax;
	}
	if (word->yMax > yMax) {
		yMax = word->yMax;
	}
	if (len + word->len > size) {
		size = len + word->len;
		text = (Unicode *)greallocn(text, size, sizeof(Unicode));
		edge = (double *)greallocn(edge, (size + 1), sizeof(double));
	}
	for (i = 0; i < word->len; ++i) {
		text[len + i] = word->text[i];
		edge[len + i] = word->edge[i];
	}
	edge[len + word->len] = word->edge[word->len];
	len += word->len;
	charLen += word->charLen;
}

inline int TextWord::primaryCmp(TextWord *word) {
	double cmp;
	
	cmp = 0; // make gcc happy
	switch (rot) {
		case 0:
			cmp = xMin - word->xMin;
			break;
		case 1:
			cmp = yMin - word->yMin;
			break;
		case 2:
			cmp = word->xMax - xMax;
			break;
		case 3:
			cmp = word->yMax - yMax;
			break;
	}
	return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

double TextWord::primaryDelta(TextWord *word) {
	double delta;
	
	delta = 0; // make gcc happy
	switch (rot) {
		case 0:
			delta = word->xMin - xMax;
			break;
		case 1:
			delta = word->yMin - yMax;
			break;
		case 2:
			delta = xMin - word->xMax;
			break;
		case 3:
			delta = yMin - word->yMax;
			break;
	}
	return delta;
}

TextWord *TextWord::nextWord() {
	if (next)
		return next;
	if (line->next)
		return line->next->words;
	if (line->blk->next)
		return line->blk->next->lines->words;
	return NULL;
}

//------------------------------------------------------------------------
// TextPool
//------------------------------------------------------------------------

TextPool::TextPool() {
	minBaseIdx = 0;
	maxBaseIdx = -1;
	pool = NULL;
	cursor = NULL;
	cursorBaseIdx = -1;
}

TextPool::~TextPool() {
	int baseIdx;
	TextWord *word, *word2;
	
	for (baseIdx = minBaseIdx; baseIdx <= maxBaseIdx; ++baseIdx) {
		for (word = pool[baseIdx - minBaseIdx]; word; word = word2) {
			warning("Left word in pool");
			word2 = word->next;
			delete word;
		}
	}
	gfree(pool);
}

int TextPool::getBaseIdx(double base) {
	int baseIdx;
	
	baseIdx = (int)(base / textPoolStep);
	if (baseIdx < minBaseIdx) {
		return minBaseIdx;
	}
	if (baseIdx > maxBaseIdx) {
		return maxBaseIdx;
	}
	return baseIdx;
}

void TextPool::addWord(TextWord *word) {
	TextWord **newPool;
	int wordBaseIdx, newMinBaseIdx, newMaxBaseIdx, baseIdx;
	TextWord *w0, *w1;
	
	// expand the array if needed
	wordBaseIdx = (int)(word->base / textPoolStep);
	if (minBaseIdx > maxBaseIdx) {
		minBaseIdx = wordBaseIdx - 128;
		maxBaseIdx = wordBaseIdx + 128;
		pool = (TextWord **)gmallocn(maxBaseIdx - minBaseIdx + 1,
									 sizeof(TextWord *));
		for (baseIdx = minBaseIdx; baseIdx <= maxBaseIdx; ++baseIdx) {
			pool[baseIdx - minBaseIdx] = NULL;
		}
	} else if (wordBaseIdx < minBaseIdx) {
		newMinBaseIdx = wordBaseIdx - 128;
		newPool = (TextWord **)gmallocn(maxBaseIdx - newMinBaseIdx + 1,
										sizeof(TextWord *));
		for (baseIdx = newMinBaseIdx; baseIdx < minBaseIdx; ++baseIdx) {
			newPool[baseIdx - newMinBaseIdx] = NULL;
		}
		memcpy(&newPool[minBaseIdx - newMinBaseIdx], pool,
			   (maxBaseIdx - minBaseIdx + 1) * sizeof(TextWord *));
		gfree(pool);
		pool = newPool;
		minBaseIdx = newMinBaseIdx;
	} else if (wordBaseIdx > maxBaseIdx) {
		newMaxBaseIdx = wordBaseIdx + 128;
		pool = (TextWord **)greallocn(pool, newMaxBaseIdx - minBaseIdx + 1,
									  sizeof(TextWord *));
		for (baseIdx = maxBaseIdx + 1; baseIdx <= newMaxBaseIdx; ++baseIdx) {
			pool[baseIdx - minBaseIdx] = NULL;
		}
		maxBaseIdx = newMaxBaseIdx;
	}
	
	// insert the new word
	if (cursor && wordBaseIdx == cursorBaseIdx &&
		word->primaryCmp(cursor) > 0) {
		w0 = cursor;
		w1 = cursor->next;
	} else {
		w0 = NULL;
		w1 = pool[wordBaseIdx - minBaseIdx];
	}
	for (; w1 && word->primaryCmp(w1) > 0; w0 = w1, w1 = w1->next) ;
	word->next = w1;
	if (w0) {
		w0->next = word;
	} else {
		pool[wordBaseIdx - minBaseIdx] = word;
	}
	cursor = word;
	cursorBaseIdx = wordBaseIdx;
}

//------------------------------------------------------------------------
// TextLine
//------------------------------------------------------------------------

TextLine::TextLine(TextBlock *blkA, int rotA, double baseA) {
	blk = blkA;
	rot = rotA;
	base = baseA;
	words = lastWord = NULL;
	charCount = 0;
	next = NULL;
	prev = NULL;
	xMin = yMin = 0;
	xMax = yMax = -1;
}

TextLine::~TextLine() {
	TextWord *word;
	
	while (words) {
		word = words;
		words = words->next;
		delete word;
	}
}

void TextLine::addWord(TextWord *word) {
	if (lastWord) {
		lastWord->next = word;
	} else {
		words = word;
	}
	lastWord = word;
	
	if (xMin > xMax) {
		xMin = word->xMin;
		xMax = word->xMax;
		yMin = word->yMin;
		yMax = word->yMax;
	} else {
		if (word->xMin < xMin) {
			xMin = word->xMin;
		}
		if (word->xMax > xMax) {
			xMax = word->xMax;
		}
		if (word->yMin < yMin) {
			yMin = word->yMin;
		}
		if (word->yMax > yMax) {
			yMax = word->yMax;
		}
	}
}

int TextLine::primaryCmp(TextLine *line) {
	double cmp;
	
	cmp = 0; // make gcc happy
	switch (rot) {
		case 0:
			cmp = xMin - line->xMin;
			break;
		case 1:
			cmp = yMin - line->yMin;
			break;
		case 2:
			cmp = line->xMax - xMax;
			break;
		case 3:
			cmp = line->yMax - yMax;
			break;
	}
	return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextLine::secondaryCmp(TextLine *line) {
	double cmp;
	
	cmp = (rot == 0 || rot == 3) ? base - line->base : line->base - base;
	return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int TextLine::cmpYX(TextLine *line) {
	int cmp;
	
	if ((cmp = secondaryCmp(line))) {
		return cmp;
	}
	return primaryCmp(line);
}

void TextLine::coalesce() {
	TextWord *word0, *word1;
	double space, delta, minSpace;
	
	if (words->next) {
		
		// compute the inter-word space threshold
		if (words->len > 1 || words->next->len > 1) {
			minSpace = 0;
		} else {
			minSpace = words->primaryDelta(words->next);
			for (word0 = words->next, word1 = word0->next;
				 word1 && minSpace > 0;
				 word0 = word1, word1 = word0->next) {
				if (word1->len > 1) {
					minSpace = 0;
				}
				delta = word0->primaryDelta(word1);
				if (delta < minSpace) {
					minSpace = delta;
				}
			}
		}
		if (minSpace <= 0) {
			space = maxCharSpacing * words->fontSize;
		} else {
			space = maxWideCharSpacingMul * minSpace;
			if (space > maxWideCharSpacing * words->fontSize) {
				space = maxWideCharSpacing * words->fontSize;
			}
		}
		
		// merge words
		word0 = words;
		word1 = words->next;
		while (word1) {
			if (word0->primaryDelta(word1) >= space) {
				word0->spaceAfter = gTrue;
				word0 = word1;
				word1 = word1->next;
			} else if (word0->font == word1->font &&
					   fabs(word0->fontSize - word1->fontSize) <
					   maxWordFontSizeDelta * words->fontSize &&
					   word1->charPos == word0->charPos + word0->charLen) {
				word0->merge(word1);
				word0->next = word1->next;
				delete word1;
				word1 = word0->next;
			} else {
				word0 = word1;
				word1 = word1->next;
			}
		}
	}
	
	// build the line text
	charCount = 0;
	for (word0 = NULL, word1 = words; word1; word0 = word1, word1 = word1->next) {
		charCount += word1->len;
		if (word1->spaceAfter) ++charCount;
		word1->line = this;
		word1->prev = word0;
	}
	lastWord = word0;
	
	word0 = words;
	word0->xMinPre = word0->xMin;
	word0->xMaxPre = word0->xMax;
	word0->yMinPre = word0->yMin;
	word0->yMaxPre = word0->yMax;
	for (word1 = word0->next; word1; word1 = word1->next) {
		word1->xMinPre = fmin(word1->xMin, word0->xMinPre);
		word1->xMaxPre = fmax(word1->xMax, word0->xMaxPre);
		word1->yMinPre = fmin(word1->yMin, word0->yMinPre);
		word1->yMaxPre = fmax(word1->yMax, word0->yMaxPre);
		word0 = word1;
	}
	
	word0 = lastWord;
	word0->xMinPost = word0->xMin;
	word0->xMaxPost = word0->xMax;
	word0->yMinPost = word0->yMin;
	word0->yMaxPost = word0->yMax;
	for (word1 = word0->prev; word1; word1 = word1->prev) {
		word1->xMinPost = fmin(word1->xMin, word0->xMinPost);
		word1->xMaxPost = fmax(word1->xMax, word0->xMaxPost);
		word1->yMinPost = fmin(word1->yMin, word0->yMinPost);
		word1->yMaxPost = fmax(word1->yMax, word0->yMaxPost);
		word0 = word1;
	}
}

//------------------------------------------------------------------------
// TextBlock
//------------------------------------------------------------------------

TextBlock::TextBlock(TextPage *pageA, int rotA) {
	page = pageA;
	rot = rotA;
	xMin = yMin = 0;
	xMax = yMax = -1;
	priMin = 0;
	priMax = page->pageWidth;
	pool = new TextPool();
	lines = NULL;
	lastLine = NULL;
	next = NULL;
	prev = NULL;
	tableId = -1;
	tableEnd = gFalse;
}

TextBlock::~TextBlock() {
	TextLine *line;
	if (pool) delete pool;
	while (lines) {
		line = lines;
		lines = lines->next;
		delete line;
	}
}

void TextBlock::addWord(TextWord *word) {
	pool->addWord(word);
	if (xMin > xMax) {
		xMin = word->xMin;
		xMax = word->xMax;
		yMin = word->yMin;
		yMax = word->yMax;
	} else {
		if (word->xMin < xMin) {
			xMin = word->xMin;
		}
		if (word->xMax > xMax) {
			xMax = word->xMax;
		}
		if (word->yMin < yMin) {
			yMin = word->yMin;
		}
		if (word->yMax > yMax) {
			yMax = word->yMax;
		}
	}
}

void TextBlock::coalesce() {
	TextWord *word0, *word1, *word2, *bestWord0, *bestWord1, *lastWord;
	TextLine *line, *line0, *line1, *curLine;
	int poolMinBaseIdx, startBaseIdx, minBaseIdx, maxBaseIdx;
	int baseIdx, bestWordBaseIdx, idx0, idx1;
	double minBase, maxBase;
	double fontSize, delta, priDelta, secDelta;
	GBool found;
	
	// discard duplicated text (fake boldface, drop shadows)
	for (idx0 = pool->minBaseIdx; idx0 <= pool->maxBaseIdx; ++idx0) {
		word0 = pool->getPool(idx0);
		while (word0) {
			priDelta = dupMaxPriDelta * word0->fontSize;
			secDelta = dupMaxSecDelta * word0->fontSize;
			if (rot == 0 || rot == 3) {
				maxBaseIdx = pool->getBaseIdx(word0->base + secDelta);
			} else {
				maxBaseIdx = pool->getBaseIdx(word0->base - secDelta);
			}
			found = gFalse;
			word1 = word2 = NULL; // make gcc happy
			for (idx1 = idx0; idx1 <= maxBaseIdx; ++idx1) {
				if (idx1 == idx0) {
					word1 = word0;
					word2 = word0->next;
				} else {
					word1 = NULL;
					word2 = pool->getPool(idx1);
				}
				for (; word2; word1 = word2, word2 = word2->next) {
					if (word2->len == word0->len &&
						!memcmp(word2->text, word0->text,
								word0->len * sizeof(Unicode))) {
							switch (rot) {
								case 0:
								case 2:
									found = fabs(word0->xMin - word2->xMin) < priDelta &&
									fabs(word0->xMax - word2->xMax) < priDelta &&
									fabs(word0->yMin - word2->yMin) < secDelta &&
									fabs(word0->yMax - word2->yMax) < secDelta;
									break;
								case 1:
								case 3:
									found = fabs(word0->xMin - word2->xMin) < secDelta &&
									fabs(word0->xMax - word2->xMax) < secDelta &&
									fabs(word0->yMin - word2->yMin) < priDelta &&
									fabs(word0->yMax - word2->yMax) < priDelta;
									break;
							}
						}
					if (found) {
						break;
					}
				}
				if (found) {
					break;
				}
			}
			if (found) {
				if (word1) {
					word1->next = word2->next;
				} else {
					pool->setPool(idx1, word2->next);
				}
				delete word2;
			} else {
				word0 = word0->next;
			}
		}
	}
	
	// build the lines
	curLine = NULL;
	poolMinBaseIdx = pool->minBaseIdx;
	charCount = 0;
	while (1) {
		
		// find the first non-empty line in the pool
		for (;
			 poolMinBaseIdx <= pool->maxBaseIdx && !pool->getPool(poolMinBaseIdx);
			 ++poolMinBaseIdx) ;
		if (poolMinBaseIdx > pool->maxBaseIdx) {
			break;
		}
		
		// look for the left-most word in the first four lines of the
		// pool -- this avoids starting with a superscript word
		startBaseIdx = poolMinBaseIdx;
		for (baseIdx = poolMinBaseIdx + 1;
			 baseIdx < poolMinBaseIdx + 4 && baseIdx <= pool->maxBaseIdx;
			 ++baseIdx) {
			if (!pool->getPool(baseIdx)) {
				continue;
			}
			if (pool->getPool(baseIdx)->primaryCmp(pool->getPool(startBaseIdx))
				< 0) {
				startBaseIdx = baseIdx;
			}
		}
		
		// create a new line
		word0 = pool->getPool(startBaseIdx);
		pool->setPool(startBaseIdx, word0->next);
		word0->next = NULL;
		line = new TextLine(this, word0->rot, word0->base);
		line->addWord(word0);
		lastWord = word0;
		
		// compute the search range
		fontSize = word0->fontSize;
		minBase = word0->base - maxIntraLineDelta * fontSize;
		maxBase = word0->base + maxIntraLineDelta * fontSize;
		minBaseIdx = pool->getBaseIdx(minBase);
		maxBaseIdx = pool->getBaseIdx(maxBase);
		
		// find the rest of the words in this line
		while (1) {
			
			// find the left-most word whose baseline is in the range for
			// this line
			bestWordBaseIdx = 0;
			bestWord0 = bestWord1 = NULL;
			for (baseIdx = minBaseIdx; baseIdx <= maxBaseIdx; ++baseIdx) {
				for (word0 = NULL, word1 = pool->getPool(baseIdx);
					 word1;
					 word0 = word1, word1 = word1->next) {
					if (word1->base >= minBase &&
						word1->base <= maxBase &&
						(delta = lastWord->primaryDelta(word1)) >=
						minCharSpacing * fontSize) {
						if (delta < maxWordSpacing * fontSize &&
							(!bestWord1 || word1->primaryCmp(bestWord1) < 0)) {
							bestWordBaseIdx = baseIdx;
							bestWord0 = word0;
							bestWord1 = word1;
						}
						break;
					}
				}
			}
			if (!bestWord1) {
				break;
			}
			
			// remove it from the pool, and add it to the line
			if (bestWord0) {
				bestWord0->next = bestWord1->next;
			} else {
				pool->setPool(bestWordBaseIdx, bestWord1->next);
			}
			bestWord1->next = NULL;
			line->addWord(bestWord1);
			lastWord = bestWord1;
		}
		
		// add the line
		if (curLine && line->cmpYX(curLine) > 0) {
			line0 = curLine;
			line1 = curLine->next;
		} else {
			line0 = NULL;
			line1 = lines;
		}
		for (;
			 line1 && line->cmpYX(line1) > 0;
			 line0 = line1, line1 = line1->next) ;
		if (line0) {
			line0->next = line;
		} else {
			lines = line;
		}
		line->next = line1;
		curLine = line;
		line->coalesce();
		charCount += line->charCount;
	}
	
	delete pool;
	pool = NULL;
	
	for (line0 = NULL, line1 = lines; line1; line0 = line1, line1 = line1->next)
		line1->prev = line0;
	lastLine = line0;
	
	line0 = lines;
	line0->xMinPre = line0->xMin;
	line0->xMaxPre = line0->xMax;
	line0->yMinPre = line0->yMin;
	line0->yMaxPre = line0->yMax;
	for (line1 = line0->next; line1; line1 = line1->next) {
		line1->xMinPre = fmin(line1->xMin, line0->xMinPre);
		line1->xMaxPre = fmax(line1->xMax, line0->xMaxPre);
		line1->yMinPre = fmin(line1->yMin, line0->yMinPre);
		line1->yMaxPre = fmax(line1->yMax, line0->yMaxPre);
		line0 = line1;
	}
	
	line0 = lastLine;
	line0->xMinPost = line0->xMin;
	line0->xMaxPost = line0->xMax;
	line0->yMinPost = line0->yMin;
	line0->yMaxPost = line0->yMax;
	for (line1 = line0->prev; line1; line1 = line1->prev) {
		line1->xMinPost = fmin(line1->xMin, line0->xMinPost);
		line1->xMaxPost = fmax(line1->xMax, line0->xMaxPost);
		line1->yMinPost = fmin(line1->yMin, line0->yMinPost);
		line1->yMaxPost = fmax(line1->yMax, line0->yMaxPost);
		line0 = line1;
	}
}

void TextBlock::updatePriMinMax(TextBlock *blk) {
	double newPriMin, newPriMax;
	GBool gotPriMin, gotPriMax;
	
	gotPriMin = gotPriMax = gFalse;
	newPriMin = newPriMax = 0; // make gcc happy
	switch (page->primaryRot) {
		case 0:
		case 2:
			if (blk->yMin < yMax && blk->yMax > yMin) {
				if (blk->xMin < xMin) {
					newPriMin = blk->xMax;
					gotPriMin = gTrue;
				}
				if (blk->xMax > xMax) {
					newPriMax = blk->xMin;
					gotPriMax = gTrue;
				}
			}
			break;
		case 1:
		case 3:
			if (blk->xMin < xMax && blk->xMax > xMin) {
				if (blk->yMin < yMin) {
					newPriMin = blk->yMax;
					gotPriMin = gTrue;
				}
				if (blk->yMax > yMax) {
					newPriMax = blk->yMin;
					gotPriMax = gTrue;
				}
			}
			break;
	}
	if (gotPriMin) {
		if (newPriMin > xMin) {
			newPriMin = xMin;
		}
		if (newPriMin > priMin) {
			priMin = newPriMin;
		}
	}
	if (gotPriMax) {
		if (newPriMax < xMax) {
			newPriMax = xMax;
		}
		if (newPriMax < priMax) {
			priMax = newPriMax;
		}
	}
}

int TextBlock::cmpXYPrimaryRot(const void *p1, const void *p2) {
	TextBlock *blk1 = *(TextBlock **)p1;
	TextBlock *blk2 = *(TextBlock **)p2;
	double cmp;
	
	cmp = 0; // make gcc happy
	switch (blk1->page->primaryRot) {
		case 0:
			if ((cmp = blk1->xMin - blk2->xMin) == 0) {
				cmp = blk1->yMin - blk2->yMin;
			}
			break;
		case 1:
			if ((cmp = blk1->yMin - blk2->yMin) == 0) {
				cmp = blk2->xMax - blk1->xMax;
			}
			break;
		case 2:
			if ((cmp = blk2->xMax - blk1->xMax) == 0) {
				cmp = blk2->yMin - blk1->yMin;
			}
			break;
		case 3:
			if ((cmp = blk2->yMax - blk1->yMax) == 0) {
				cmp = blk1->xMax - blk2->xMax;
			}
			break;
	}
	return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

double TextBlock::secondaryDelta(TextBlock *blk) {
	double delta;
	
	delta = 0; // make gcc happy
	switch (rot) {
		case 0:
			delta = blk->yMin - yMax;
			break;
		case 1:
			delta = xMin - blk->xMax;
			break;
		case 2:
			delta = yMin - blk->yMax;
			break;
		case 3:
			delta = blk->xMin - xMax;
			break;
	}
	return delta;
}

GBool TextBlock::isBelow(TextBlock *blk) {
	GBool below;
	
	below = gFalse; // make gcc happy
	switch (page->primaryRot) {
		case 0:
			below = xMin >= blk->priMin && xMax <= blk->priMax &&
            yMin > blk->yMin;
			break;
		case 1:
			below = yMin >= blk->priMin && yMax <= blk->priMax &&
            xMax < blk->xMax;
			break;
		case 2:
			below = xMin >= blk->priMin && xMax <= blk->priMax &&
            yMax < blk->yMax;
			break;
		case 3:
			below = yMin >= blk->priMin && yMax <= blk->priMax &&
            xMin > blk->xMin;
			break;
	}
	
	return below;
}

GBool TextBlock::isBeforeByRule1(TextBlock *blk1) {
	GBool before = gFalse;
	GBool overlap = gFalse;
	
	switch (this->page->primaryRot) {
		case 0:
		case 2:
			overlap = ((this->ExMin <= blk1->ExMin) &&
					   (blk1->ExMin <= this->ExMax)) ||
			((blk1->ExMin <= this->ExMin) &&
			 (this->ExMin <= blk1->ExMax));
			break;
		case 1:
		case 3:
			overlap = ((this->EyMin <= blk1->EyMin) &&
					   (blk1->EyMin <= this->EyMax)) ||
			((blk1->EyMin <= this->EyMin) &&
			 (this->EyMin <= blk1->EyMax));
			break;
	}
	switch (this->page->primaryRot) {
		case 0:
			before = overlap && this->EyMin < blk1->EyMin;
			break;
		case 1:
			before = overlap && this->ExMax > blk1->ExMax;
			break;
		case 2:
			before = overlap && this->EyMax > blk1->EyMax;
			break;
		case 3:
			before = overlap && this->ExMin < blk1->ExMin;
			break;
	}
	return before;
}

GBool TextBlock::isBeforeByRule2(TextBlock *blk1) {
	double cmp = 0;
	int rotLR = rot;
	
	if (!page->primaryLR) {
		rotLR = (rotLR + 2) % 4;
	}
	
	switch (rotLR) {
		case 0:
			cmp = ExMax - blk1->ExMin;
			break;
		case 1:
			cmp = EyMin - blk1->EyMax;
			break;
		case 2:
			cmp = blk1->ExMax - ExMin;
			break;
		case 3:
			cmp = blk1->EyMin - EyMax;
			break;
	}
	return cmp <= 0;
}

// Sort into reading order by performing a topological sort using the rules
// given in "High Performance Document Layout Analysis", T.M. Breuel, 2003.
// See http://pubs.iupr.org/#2003-breuel-sdiut
// Topological sort is done by depth first search, see
// http://en.wikipedia.org/wiki/Topological_sorting
int TextBlock::visitDepthFirst(TextBlock *blkList, int pos1,
							   TextBlock **sorted, int sortPos,
							   GBool *visited) {
	int pos2;
	TextBlock *blk1, *blk2, *blk3;
	GBool before;
	
	if (visited[pos1]) {
		return sortPos;
	}
	
	blk1 = this;
	
	visited[pos1] = gTrue;
	pos2 = -1;
	for (blk2 = blkList; blk2; blk2 = blk2->next) {
		pos2++;
		if (visited[pos2]) {
			// skip visited nodes
			continue;
		}
		before = gFalse;
		
		// is blk2 before blk1? (for table entries)
		if (blk1->tableId >= 0 && blk1->tableId == blk2->tableId) {
			if (page->primaryLR) {
				if (blk2->xMax <= blk1->xMin &&
					blk2->yMin <= blk1->yMax &&
					blk2->yMax >= blk1->yMin)
					before = gTrue;
			} else {
				if (blk2->xMin >= blk1->xMax &&
					blk2->yMin <= blk1->yMax &&
					blk2->yMax >= blk1->yMin)
					before = gTrue;
			}
			
			if (blk2->yMax <= blk1->yMin)
				before = gTrue;
		} else {
			if (blk2->isBeforeByRule1(blk1)) {
				// Rule (1) blk1 and blk2 overlap, and blk2 is above blk1.
				before = gTrue;
			} else if (blk2->isBeforeByRule2(blk1)) {
				// Rule (2) blk2 left of blk1, and no intervening blk3
				//          such that blk1 is before blk3 by rule 1,
				//          and blk3 is before blk2 by rule 1.
				before = gTrue;
				for (blk3 = blkList; blk3; blk3 = blk3->next) {
					if (blk3 == blk2 || blk3 == blk1) {
						continue;
					}
					if (blk1->isBeforeByRule1(blk3) &&
						blk3->isBeforeByRule1(blk2)) {
						before = gFalse;
						break;
					}
				}
			}
		}
		if (before) {
			// blk2 is before blk1, so it needs to be visited
			// before we can add blk1 to the sorted list.
			sortPos = blk2->visitDepthFirst(blkList, pos2, sorted, sortPos, visited);
		}
	}
	sorted[sortPos++] = blk1;
	return sortPos;
}

//------------------------------------------------------------------------
// TextPage
//------------------------------------------------------------------------

TextPage::TextPage(PDFDoc *doc, int pageNum) {
	int rot;
	curWord = NULL;
	charPos = 0;
	curFont = NULL;
	curFontSize = 0;
	nest = 0;
	nTinyChars = 0;
	lastCharOverlap = gFalse;
	for (rot = 0; rot < 4; ++rot)
		pools[rot] = new TextPool();
	blocks = NULL;
	lastBlk = NULL;
	fonts = new GooList();
	actualText = NULL;
	selStart = NULL;
	selEnd = NULL;
	ok = gFalse;
	doc->displayPage(this, pageNum, 72, 72, 0, gTrue, gFalse, gFalse);
	coalesce();
    if (actualText) delete actualText;
	deleteGooList(fonts, TextFontInfo);
	ok = gTrue;
}

TextPage::~TextPage() {
	TextBlock *blk;
	while (blocks) {
		blk = blocks;
		blocks = blocks->next;
		delete blk;
	}
}

void TextPage::startPage(int pageNum, GfxState *state) {
    actualTextBMCLevel = 0;	
	if (state) {
		pageWidth = state->getPageWidth();
		pageHeight = state->getPageHeight();
	} else {
		pageWidth = pageHeight = 0;
	}
}

void TextPage::endPage() {
	if (curWord) endWord();
}

void TextPage::updateFont(GfxState *state) {
	GfxFont *gfxFont;
	double *fm;
	const char *name;
	int code, mCode, letterCode, anyCode;
	double w;
	int i;
	
	// get the font info object
	curFont = NULL;
	for (i = 0; i < fonts->getLength(); ++i) {
		curFont = (TextFontInfo *)fonts->get(i);
		if (curFont->matches(state)) {
			break;
		}
		curFont = NULL;
	}
	if (!curFont) {
		curFont = new TextFontInfo(state);
		fonts->append(curFont);
	}
	
	// adjust the font size
	gfxFont = state->getFont();
	curFontSize = state->getTransformedFontSize();
	if (gfxFont && gfxFont->getType() == fontType3) {
		// This is a hack which makes it possible to deal with some Type 3
		// fonts.  The problem is that it's impossible to know what the
		// base coordinate system used in the font is without actually
		// rendering the font.  This code tries to guess by looking at the
		// width of the character 'm' (which breaks if the font is a
		// subset that doesn't contain 'm').
		mCode = letterCode = anyCode = -1;
		for (code = 0; code < 256; ++code) {
			name = ((Gfx8BitFont *)gfxFont)->getCharName(code);
			if (name && name[0] == 'm' && name[1] == '\0') {
				mCode = code;
			}
			if (letterCode < 0 && name && name[1] == '\0' &&
				((name[0] >= 'A' && name[0] <= 'Z') ||
				 (name[0] >= 'a' && name[0] <= 'z'))) {
					letterCode = code;
				}
			if (anyCode < 0 && name &&
				((Gfx8BitFont *)gfxFont)->getWidth(code) > 0) {
				anyCode = code;
			}
		}
		if (mCode >= 0 &&
			(w = ((Gfx8BitFont *)gfxFont)->getWidth(mCode)) > 0) {
			// 0.6 is a generic average 'm' width -- yes, this is a hack
			curFontSize *= w / 0.6;
		} else if (letterCode >= 0 &&
				   (w = ((Gfx8BitFont *)gfxFont)->getWidth(letterCode)) > 0) {
			// even more of a hack: 0.5 is a generic letter width
			curFontSize *= w / 0.5;
		} else if (anyCode >= 0 &&
				   (w = ((Gfx8BitFont *)gfxFont)->getWidth(anyCode)) > 0) {
			// better than nothing: 0.5 is a generic character width
			curFontSize *= w / 0.5;
		}
		fm = gfxFont->getFontMatrix();
		if (fm[0] != 0) {
			curFontSize *= fabs(fm[3] / fm[0]);
		}
	}
}

void TextPage::drawChar(GfxState *state, double x, double y, double dx, double dy,
						double originX, double originY, CharCode c, int nBytes, 
						Unicode *u, int uLen) {
	if (actualTextBMCLevel == 0) {
		addChar(state, x, y, dx, dy, c, nBytes, u, uLen);
	} else {
		// Inside ActualText span.
		if (newActualTextSpan) {
			actualText_x = x;
			actualText_y = y;
			actualText_dx = dx;
			actualText_dy = dy;
			newActualTextSpan = gFalse;
		} else {
			if (x < actualText_x)
				actualText_x = x;
			if (y < actualText_y)
				actualText_y = y;
			if (x + dx > actualText_x + actualText_dx)
				actualText_dx = x + dx - actualText_x;
			if (y + dy > actualText_y + actualText_dy)
				actualText_dy = y + dy - actualText_y;
		}
	}
}

void TextPage::beginMarkedContent(const char *name, Dict *properties) {
	if (actualTextBMCLevel > 0) {
		// Already inside a ActualText span.
		actualTextBMCLevel++;
		return;
	}
	
	Object obj;
	if (properties && properties->lookup("ActualText", &obj)) {
		if (obj.isString()) {
			actualText = obj.getString();
			actualTextBMCLevel = 1;
			newActualTextSpan = gTrue;
		}
	}
}

void TextPage::endMarkedContent(GfxState *state) {
	if (actualTextBMCLevel > 0) {
		actualTextBMCLevel--;
		if (actualTextBMCLevel == 0) {
			// ActualText span closed. Output the span text and the
			// extents of all the glyphs inside the span
			
			if (newActualTextSpan) {
				// No content inside span.
				actualText_x = state->getCurX();
				actualText_y = state->getCurY();
				actualText_dx = 0;
				actualText_dy = 0;
			}
			
			Unicode *uni;
			int length = actualText->getLength();
			if (!actualText->hasUnicodeMarker()) {
				uni = new Unicode[length];
				for (int i = 0; i < length; ++i)
					uni[i] = pdfDocEncoding[(int)actualText->getChar(i) & 0xff];
			}
			else {
				uni = new Unicode[length - 2];
				for (int i = 2; i < length; ++i) {
					uni[i - 2] = ((actualText->getChar(i) << 8) & 0xffff) | 
								 ((int)actualText->getChar(i + 1) & 0xff);
				}
				length -= 2;
			}
			addChar(state, actualText_x, actualText_y, actualText_dx, actualText_dy, 
					0, 1, uni, length);
			delete [] uni;
			delete actualText;
			actualText = NULL;
		}
	}
}

void TextPage::beginWord(GfxState *state, double x0, double y0) {
	//GfxFont *gfxFont;
	//double *fontm;
	double m[4]/*, m2[4]*/;
	int rot;
	
	// This check is needed because Type 3 characters can contain
	// text-drawing operations (when TextPage is being used via
	// {X,Win}SplashOutputDev rather than TextOutputDev).
	if (curWord) {
		++nest;
		return;
	}
	
	// compute the rotation
	state->getFontTransMat(&m[0], &m[1], &m[2], &m[3]);
	
	// I have seen the font matrix of some Type3 fonts do not represent
	// text rotation. Perhaps they are only about the rotation of drawing
	// pictures (fonts)?      Guangda
	/*gfxFont = state->getFont();
	if (gfxFont && gfxFont->getType() == fontType3) {
		fontm = state->getFont()->getFontMatrix();
		m2[0] = fontm[0] * m[0] + fontm[1] * m[2];
		m2[1] = fontm[0] * m[1] + fontm[1] * m[3];
		m2[2] = fontm[2] * m[0] + fontm[3] * m[2];
		m2[3] = fontm[2] * m[1] + fontm[3] * m[3];
		m[0] = m2[0];
		m[1] = m2[1];
		m[2] = m2[2];
		m[3] = m2[3];
	}*/
	if (fabs(m[0] * m[3]) > fabs(m[1] * m[2])) {
		rot = (m[3] < 0) ? 0 : 2;
	} else {
		rot = (m[2] > 0) ? 1 : 3;
	}
	curWord = new TextWord(state, rot, x0, y0, charPos, curFont, curFontSize);
}

void TextPage::addChar(GfxState *state, double x, double y, double dx, double dy,
					   CharCode c, int nBytes, Unicode *u, int uLen) {
	double x1, y1, w1, h1, dx2, dy2, base, sp, delta;
	GBool overlap;
	int i;
	
	// subtract char and word spacing from the dx,dy values
	sp = state->getCharSpace();
	if (c == (CharCode)0x20) {
		sp += state->getWordSpace();
	}
	state->textTransformDelta(sp * state->getHorizScaling(), 0, &dx2, &dy2);
	dx -= dx2;
	dy -= dy2;
	state->transformDelta(dx, dy, &w1, &h1);
	
	// throw away chars that aren't inside the page bounds
	// (and also do a sanity check on the character size)
	state->transform(x, y, &x1, &y1);
	if (x1 + w1 < 0 || x1 > pageWidth ||
		y1 + h1 < 0 || y1 > pageHeight ||
		w1 > pageWidth || h1 > pageHeight) {
		charPos += nBytes;
		return;
	}
	
	// check the tiny chars limit
	if (!globalParams->getTextKeepTinyChars() &&
		fabs(w1) < 3 && fabs(h1) < 3) {
		if (++nTinyChars > 50000) {
			charPos += nBytes;
			return;
		}
	}
	
	// break words at space character
	if (uLen == 1 && u[0] == (Unicode)0x20) {
		if (curWord) {
			++curWord->charLen;
		}
		charPos += nBytes;
		endWord();
		return;
	}
	
	// start a new word if:
	// (1) this character doesn't fall in the right place relative to
	//     the end of the previous word (this places upper and lower
	//     constraints on the position deltas along both the primary
	//     and secondary axes), or
	// (2) this character overlaps the previous one (duplicated text), or
	// (3) the previous character was an overlap (we want each duplicated
	//     character to be in a word by itself at this stage),
	// (4) the font size has changed
	if (curWord && curWord->len > 0) {
		base = sp = delta = 0; // make gcc happy
		switch (curWord->rot) {
			case 0:
				base = y1;
				sp = x1 - curWord->xMax;
				delta = x1 - curWord->edge[curWord->len - 1];
				break;
			case 1:
				base = x1;
				sp = y1 - curWord->yMax;
				delta = y1 - curWord->edge[curWord->len - 1];
				break;
			case 2:
				base = y1;
				sp = curWord->xMin - x1;
				delta = curWord->edge[curWord->len - 1] - x1;
				break;
			case 3:
				base = x1;
				sp = curWord->yMin - y1;
				delta = curWord->edge[curWord->len - 1] - y1;
				break;
		}
		overlap = fabs(delta) < dupMaxPriDelta * curWord->fontSize &&
		fabs(base - curWord->base) < dupMaxSecDelta * curWord->fontSize;
		if (overlap || lastCharOverlap ||
			sp < -minDupBreakOverlap * curWord->fontSize ||
			sp > minWordBreakSpace * curWord->fontSize ||
			fabs(base - curWord->base) > 0.5 ||
			curFontSize != curWord->fontSize) {
			endWord();
		}
		lastCharOverlap = overlap;
	} else {
		lastCharOverlap = gFalse;
	}
	
	if (uLen != 0) {
		// start a new word if needed
		if (!curWord) {
			beginWord(state, x, y);
		}
		
		// page rotation and/or transform matrices can cause text to be
		// drawn in reverse order -- in this case, swap the begin/end
		// coordinates and break text into individual chars
		if ((curWord->rot == 0 && w1 < 0) ||
			(curWord->rot == 1 && h1 < 0) ||
			(curWord->rot == 2 && w1 > 0) ||
			(curWord->rot == 3 && h1 > 0)) {
			endWord();
			beginWord(state, x + dx, y + dy);
			x1 += w1;
			y1 += h1;
			w1 = -w1;
			h1 = -h1;
		}
		
		// add the characters to the current word
		w1 /= uLen;
		h1 /= uLen;
		for (i = 0; i < uLen; ++i) {
			if (u[i] >= 0xd800 && u[i] < 0xdc00) { /* surrogate pair */
				if (i + 1 < uLen && u[i+1] >= 0xdc00 && u[i+1] < 0xe000) {
					/* next code is a low surrogate */
					Unicode uu = (((u[i] & 0x3ff) << 10) | (u[i+1] & 0x3ff)) + 0x10000;
					i++;
					curWord->addChar(state, x1 + i*w1, y1 + i*h1, w1, h1, c, uu);
				} else {
					/* missing low surrogate
					 replace it with REPLACEMENT CHARACTER (U+FFFD) */
					curWord->addChar(state, x1 + i*w1, y1 + i*h1, w1, h1, c, 0xfffd);
				}
			} else if (u[i] >= 0xdc00 && u[i] < 0xe000) {
				/* invalid low surrogate
				 replace it with REPLACEMENT CHARACTER (U+FFFD) */
				curWord->addChar(state, x1 + i*w1, y1 + i*h1, w1, h1, c, 0xfffd);
			} else {
				curWord->addChar(state, x1 + i*w1, y1 + i*h1, w1, h1, c, u[i]);
			}
		}
	}
	if (curWord) {
		curWord->charLen += nBytes;
	}
	charPos += nBytes;
}

void TextPage::endWord() {
	// This check is needed because Type 3 characters can contain
	// text-drawing operations (when TextPage is being used via
	// {X,Win}SplashOutputDev rather than TextOutputDev).
	if (nest > 0) {
		--nest;
		return;
	}
	
	if (curWord) {
		addWord(curWord);
		curWord = NULL;
	}
}

void TextPage::addWord(TextWord *word) {
	// throw away zero-length words -- they don't have valid xMin/xMax
	// values, and they're useless anyway
	if (word->len == 0) {
		delete word;
		return;
	}
	
	pools[word->rot]->addWord(word);
}

void TextPage::coalesce() {
	TextPool *pool;
	TextWord *word0, *word1, *word2;
	TextLine *line;
	TextBlock *blk, *blk1, *blk2;
	int rot, poolMinBaseIdx, baseIdx, startBaseIdx;
	double minBase, maxBase, newMinBase, newMaxBase;
	double fontSize, colSpace1, colSpace2, lineSpace, intraLineSpace;
	GBool found;
	int nBlocks;
	int count[4];
	int lrCount;
	int i, n;
	
	blocks = NULL;
	lastBlk = NULL;
	nBlocks = 0;
	primaryRot = -1;
	
	//----- assemble the blocks
	
	//~ add an outer loop for writing mode (vertical text)
	
	// build blocks for each rotation value
	for (rot = 0; rot < 4; ++rot) {
		pool = pools[rot];
		poolMinBaseIdx = pool->minBaseIdx;
		count[rot] = 0;
		
		// add blocks until no more words are left
		while (1) {
			
			// find the first non-empty line in the pool
			for (;
				 poolMinBaseIdx <= pool->maxBaseIdx &&
				 !pool->getPool(poolMinBaseIdx);
				 ++poolMinBaseIdx) ;
			if (poolMinBaseIdx > pool->maxBaseIdx) {
				break;
			}
			
			// look for the left-most word in the first four lines of the
			// pool -- this avoids starting with a superscript word
			startBaseIdx = poolMinBaseIdx;
			for (baseIdx = poolMinBaseIdx + 1;
				 baseIdx < poolMinBaseIdx + 4 && baseIdx <= pool->maxBaseIdx;
				 ++baseIdx) {
				if (!pool->getPool(baseIdx)) {
					continue;
				}
				if (pool->getPool(baseIdx)->primaryCmp(pool->getPool(startBaseIdx))
					< 0) {
					startBaseIdx = baseIdx;
				}
			}
			
			// create a new block
			word0 = pool->getPool(startBaseIdx);
			pool->setPool(startBaseIdx, word0->next);
			word0->next = NULL;
			blk = new TextBlock(this, rot);
			blk->addWord(word0);
			
			fontSize = word0->fontSize;
			minBase = maxBase = word0->base;
			colSpace1 = minColSpacing1 * fontSize;
			colSpace2 = minColSpacing2 * fontSize;
			lineSpace = maxLineSpacingDelta * fontSize;
			intraLineSpace = maxIntraLineDelta * fontSize;
			
			// add words to the block
			do {
				found = gFalse;
				
				// look for words on the line above the current top edge of
				// the block
				newMinBase = minBase;
				for (baseIdx = pool->getBaseIdx(minBase);
					 baseIdx >= pool->getBaseIdx(minBase - lineSpace);
					 --baseIdx) {
					word0 = NULL;
					word1 = pool->getPool(baseIdx);
					while (word1) {
						if (word1->base < minBase &&
							word1->base >= minBase - lineSpace &&
							((rot == 0 || rot == 2)
							 ? (word1->xMin < blk->xMax && word1->xMax > blk->xMin)
							 : (word1->yMin < blk->yMax && word1->yMax > blk->yMin)) &&
							fabs(word1->fontSize - fontSize) <
							maxBlockFontSizeDelta1 * fontSize) {
							word2 = word1;
							if (word0) {
								word0->next = word1->next;
							} else {
								pool->setPool(baseIdx, word1->next);
							}
							word1 = word1->next;
							word2->next = NULL;
							blk->addWord(word2);
							found = gTrue;
							newMinBase = word2->base;
						} else {
							word0 = word1;
							word1 = word1->next;
						}
					}
				}
				minBase = newMinBase;
				
				// look for words on the line below the current bottom edge of
				// the block
				newMaxBase = maxBase;
				for (baseIdx = pool->getBaseIdx(maxBase);
					 baseIdx <= pool->getBaseIdx(maxBase + lineSpace);
					 ++baseIdx) {
					word0 = NULL;
					word1 = pool->getPool(baseIdx);
					while (word1) {
						if (word1->base > maxBase &&
							word1->base <= maxBase + lineSpace &&
							((rot == 0 || rot == 2)
							 ? (word1->xMin < blk->xMax && word1->xMax > blk->xMin)
							 : (word1->yMin < blk->yMax && word1->yMax > blk->yMin)) &&
							fabs(word1->fontSize - fontSize) <
							maxBlockFontSizeDelta1 * fontSize) {
							word2 = word1;
							if (word0) {
								word0->next = word1->next;
							} else {
								pool->setPool(baseIdx, word1->next);
							}
							word1 = word1->next;
							word2->next = NULL;
							blk->addWord(word2);
							found = gTrue;
							newMaxBase = word2->base;
						} else {
							word0 = word1;
							word1 = word1->next;
						}
					}
				}
				maxBase = newMaxBase;
				
				// look for words that are on lines already in the block, and
				// that overlap the block horizontally
				for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
					 baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
					 ++baseIdx) {
					word0 = NULL;
					word1 = pool->getPool(baseIdx);
					while (word1) {
						if (word1->base >= minBase - intraLineSpace &&
							word1->base <= maxBase + intraLineSpace &&
							((rot == 0 || rot == 2)
							 ? (word1->xMin < blk->xMax + colSpace1 &&
								word1->xMax > blk->xMin - colSpace1)
							 : (word1->yMin < blk->yMax + colSpace1 &&
								word1->yMax > blk->yMin - colSpace1)) &&
							fabs(word1->fontSize - fontSize) <
							maxBlockFontSizeDelta2 * fontSize) {
							word2 = word1;
							if (word0) {
								word0->next = word1->next;
							} else {
								pool->setPool(baseIdx, word1->next);
							}
							word1 = word1->next;
							word2->next = NULL;
							blk->addWord(word2);
							found = gTrue;
						} else {
							word0 = word1;
							word1 = word1->next;
						}
					}
				}
				
				// only check for outlying words (the next two chunks of code)
				// if we didn't find anything else
				if (found) {
					continue;
				}
				
				// scan down the left side of the block, looking for words
				// that are near (but not overlapping) the block; if there are
				// three or fewer, add them to the block
				n = 0;
				for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
					 baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
					 ++baseIdx) {
					word1 = pool->getPool(baseIdx);
					while (word1) {
						if (word1->base >= minBase - intraLineSpace &&
							word1->base <= maxBase + intraLineSpace &&
							((rot == 0 || rot == 2)
							 ? (word1->xMax <= blk->xMin &&
								word1->xMax > blk->xMin - colSpace2)
							 : (word1->yMax <= blk->yMin &&
								word1->yMax > blk->yMin - colSpace2)) &&
							fabs(word1->fontSize - fontSize) <
							maxBlockFontSizeDelta3 * fontSize) {
							++n;
							break;
						}
						word1 = word1->next;
					}
				}
				if (n > 0 && n <= 3) {
					for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
						 baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
						 ++baseIdx) {
						word0 = NULL;
						word1 = pool->getPool(baseIdx);
						while (word1) {
							if (word1->base >= minBase - intraLineSpace &&
								word1->base <= maxBase + intraLineSpace &&
								((rot == 0 || rot == 2)
								 ? (word1->xMax <= blk->xMin &&
									word1->xMax > blk->xMin - colSpace2)
								 : (word1->yMax <= blk->yMin &&
									word1->yMax > blk->yMin - colSpace2)) &&
								fabs(word1->fontSize - fontSize) <
								maxBlockFontSizeDelta3 * fontSize) {
								word2 = word1;
								if (word0) {
									word0->next = word1->next;
								} else {
									pool->setPool(baseIdx, word1->next);
								}
								word1 = word1->next;
								word2->next = NULL;
								blk->addWord(word2);
								if (word2->base < minBase) {
									minBase = word2->base;
								} else if (word2->base > maxBase) {
									maxBase = word2->base;
								}
								found = gTrue;
								break;
							} else {
								word0 = word1;
								word1 = word1->next;
							}
						}
					}
				}
				
				// scan down the right side of the block, looking for words
				// that are near (but not overlapping) the block; if there are
				// three or fewer, add them to the block
				n = 0;
				for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
					 baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
					 ++baseIdx) {
					word1 = pool->getPool(baseIdx);
					while (word1) {
						if (word1->base >= minBase - intraLineSpace &&
							word1->base <= maxBase + intraLineSpace &&
							((rot == 0 || rot == 2)
							 ? (word1->xMin >= blk->xMax &&
								word1->xMin < blk->xMax + colSpace2)
							 : (word1->yMin >= blk->yMax &&
								word1->yMin < blk->yMax + colSpace2)) &&
							fabs(word1->fontSize - fontSize) <
							maxBlockFontSizeDelta3 * fontSize) {
							++n;
							break;
						}
						word1 = word1->next;
					}
				}
				if (n > 0 && n <= 3) {
					for (baseIdx = pool->getBaseIdx(minBase - intraLineSpace);
						 baseIdx <= pool->getBaseIdx(maxBase + intraLineSpace);
						 ++baseIdx) {
						word0 = NULL;
						word1 = pool->getPool(baseIdx);
						while (word1) {
							if (word1->base >= minBase - intraLineSpace &&
								word1->base <= maxBase + intraLineSpace &&
								((rot == 0 || rot == 2)
								 ? (word1->xMin >= blk->xMax &&
									word1->xMin < blk->xMax + colSpace2)
								 : (word1->yMin >= blk->yMax &&
									word1->yMin < blk->yMax + colSpace2)) &&
								fabs(word1->fontSize - fontSize) <
								maxBlockFontSizeDelta3 * fontSize) {
								word2 = word1;
								if (word0) {
									word0->next = word1->next;
								} else {
									pool->setPool(baseIdx, word1->next);
								}
								word1 = word1->next;
								word2->next = NULL;
								blk->addWord(word2);
								if (word2->base < minBase) {
									minBase = word2->base;
								} else if (word2->base > maxBase) {
									maxBase = word2->base;
								}
								found = gTrue;
								break;
							} else {
								word0 = word1;
								word1 = word1->next;
							}
						}
					}
				}
				
			} while (found);
			
			//~ need to compute the primary writing mode (horiz/vert) in
			//~ addition to primary rotation
			
			// coalesce the block, and add it to the list
			blk->coalesce();
			if (lastBlk) {
				lastBlk->next = blk;
			} else {
				blocks = blk;
			}
			lastBlk = blk;
			count[rot] += blk->charCount;
			if (primaryRot < 0 || count[rot] > count[primaryRot]) {
				primaryRot = rot;
			}
			++nBlocks;
		}
	}
	
	for (rot = 0; rot < 4; ++rot) delete pools[rot];
	
	// determine the primary direction
	lrCount = 0;
	for (blk = blocks; blk; blk = blk->next) {
		for (line = blk->lines; line; line = line->next) {
			for (word0 = line->words; word0; word0 = word0->next) {
				for (i = 0; i < word0->len; ++i) {
					if (unicodeTypeL(word0->text[i])) {
						++lrCount;
					} else if (unicodeTypeR(word0->text[i])) {
						--lrCount;
					}
				}
			}
		}
	}
	primaryLR = lrCount >= 0;
	
	//----- reading order sort
	
	// compute space on left and right sides of each block
	for (blk1 = blocks; blk1; blk1 = blk1->next)
		for (blk2 = blocks; blk2; blk2 = blk2->next)
			if (blk1 != blk2) blk1->updatePriMinMax(blk2);
	
	double bxMin0, byMin0, bxMin1, byMin1;
	int numTables = 0;
	int tableId = -1;
	int correspondenceX, correspondenceY;
	double xCentre1, yCentre1, xCentre2, yCentre2;
	double xCentre3, yCentre3, xCentre4, yCentre4;
	double deltaX, deltaY;
	TextBlock *fblk2 = NULL, *fblk3 = NULL, *fblk4 = NULL;
	
	for (blk1 = blocks; blk1; blk1 = blk1->next) {
		blk1->ExMin = blk1->xMin;
		blk1->ExMax = blk1->xMax;
		blk1->EyMin = blk1->yMin;
		blk1->EyMax = blk1->yMax;
		
		bxMin0 = DBL_MAX;
		byMin0 = DBL_MAX;
		bxMin1 = DBL_MAX;
		byMin1 = DBL_MAX;
		
		fblk2 = NULL;
		fblk3 = NULL;
		fblk4 = NULL;
		
		/*  find fblk2, fblk3 and fblk4 so that
		 *  fblk2 is on the right of blk1 and overlap with blk1 in y axis
		 *  fblk3 is under blk1 and overlap with blk1 in x axis
		 *  fblk4 is under blk1 and on the right of blk1
		 *  and they are closest to blk1
		 */
		for (blk2 = blocks; blk2; blk2 = blk2->next) {
			if (blk2 != blk1) {
				if (blk2->yMin <= blk1->yMax &&
					blk2->yMax >= blk1->yMin &&
					blk2->xMin > blk1->xMax &&
					blk2->xMin < bxMin0) {
					bxMin0 = blk2->xMin;
					fblk2 = blk2;
				} else if (blk2->xMin <= blk1->xMax &&
						   blk2->xMax >= blk1->xMin &&
						   blk2->yMin > blk1->yMax &&
						   blk2->yMin < byMin0) {
					byMin0 = blk2->yMin;
					fblk3 = blk2;
				} else if (blk2->xMin > blk1->xMax &&
						   blk2->xMin < bxMin1 &&
						   blk2->yMin > blk1->yMax &&
						   blk2->yMin < byMin1) {
					bxMin1 = blk2->xMin;
					byMin1 = blk2->yMin;
					fblk4 = blk2;
				}
			}
		}
		
		/*  fblk4 can not overlap with fblk3 in x and with fblk2 in y
		 *  fblk2 can not overlap with fblk3 in x and y
		 *  fblk4 has to overlap with fblk3 in y and with fblk2 in x
		 */
		if (fblk2 != NULL &&
			fblk3 != NULL &&
			fblk4 != NULL) {
			if (((fblk3->xMin <= fblk4->xMax && fblk3->xMax >= fblk4->xMin) ||
				 (fblk2->yMin <= fblk4->yMax && fblk2->yMax >= fblk4->yMin) ||
				 (fblk2->xMin <= fblk3->xMax && fblk2->xMax >= fblk3->xMin) ||
				 (fblk2->yMin <= fblk3->yMax && fblk2->yMax >= fblk3->yMin)) ||
				!(fblk4->xMin <= fblk2->xMax && fblk4->xMax >= fblk2->xMin &&
				  fblk4->yMin <= fblk3->yMax && fblk4->yMax >= fblk3->yMin)) {
					fblk2 = NULL;
					fblk3 = NULL;
					fblk4 = NULL;
				}
		}
		
		// if we found any then look whether they form a table
		if (fblk2 != NULL &&
			fblk3 != NULL &&
			fblk4 != NULL) {
			tableId = -1;
			correspondenceX = 0;
			correspondenceY = 0;
			deltaX = 0.0;
			deltaY = 0.0;
			
			if (blk1->lines && blk1->lines->words)
				deltaX = blk1->lines->words->fontSize;
			if (fblk2->lines && fblk2->lines->words)
				deltaX = deltaX < fblk2->lines->words->fontSize ?
				deltaX : fblk2->lines->words->fontSize;
			if (fblk3->lines && fblk3->lines->words)
				deltaX = deltaX < fblk3->lines->words->fontSize ?
				deltaX : fblk3->lines->words->fontSize;
			if (fblk4->lines && fblk4->lines->words)
				deltaX = deltaX < fblk4->lines->words->fontSize ?
				deltaX : fblk4->lines->words->fontSize;
			
			deltaY = deltaX;
			
			deltaX *= minColSpacing1;
			deltaY *= maxIntraLineDelta;
			
			xCentre1 = (blk1->xMax + blk1->xMin) / 2.0;
			yCentre1 = (blk1->yMax + blk1->yMin) / 2.0;
			xCentre2 = (fblk2->xMax + fblk2->xMin) / 2.0;
			yCentre2 = (fblk2->yMax + fblk2->yMin) / 2.0;
			xCentre3 = (fblk3->xMax + fblk3->xMin) / 2.0;
			yCentre3 = (fblk3->yMax + fblk3->yMin) / 2.0;
			xCentre4 = (fblk4->xMax + fblk4->xMin) / 2.0;
			yCentre4 = (fblk4->yMax + fblk4->yMin) / 2.0;
			
			// are blocks centrally aligned in x ?
			if (fabs (xCentre1 - xCentre3) <= deltaX &&
				fabs (xCentre2 - xCentre4) <= deltaX)
				correspondenceX++;
			
			// are blocks centrally aligned in y ?
			if (fabs (yCentre1 - yCentre2) <= deltaY &&
				fabs (yCentre3 - yCentre4) <= deltaY)
				correspondenceY++;
			
			// are blocks aligned to the left ?
			if (fabs (blk1->xMin - fblk3->xMin) <= deltaX &&
				fabs (fblk2->xMin - fblk4->xMin) <= deltaX)
				correspondenceX++;
			
			// are blocks aligned to the right ?
			if (fabs (blk1->xMax - fblk3->xMax) <= deltaX &&
				fabs (fblk2->xMax - fblk4->xMax) <= deltaX)
				correspondenceX++;
			
			// are blocks aligned to the top ?
			if (fabs (blk1->yMin - fblk2->yMin) <= deltaY &&
				fabs (fblk3->yMin - fblk4->yMin) <= deltaY)
				correspondenceY++;
			
			// are blocks aligned to the bottom ?
			if (fabs (blk1->yMax - fblk2->yMax) <= deltaY &&
				fabs (fblk3->yMax - fblk4->yMax) <= deltaY)
				correspondenceY++;
			
			// are blocks aligned in x and y ?
			if (correspondenceX > 0 &&
				correspondenceY > 0) {
				
				// find maximal tableId
				tableId = tableId < fblk4->tableId ? fblk4->tableId : tableId;
				tableId = tableId < fblk3->tableId ? fblk3->tableId : tableId;
				tableId = tableId < fblk2->tableId ? fblk2->tableId : tableId;
				tableId = tableId < blk1->tableId ? blk1->tableId : tableId;
				
				// if the tableId is -1, then we found new table
				if (tableId < 0) {
					tableId = numTables;
					numTables++;
				}
				
				blk1->tableId = tableId;
				fblk2->tableId = tableId;
				fblk3->tableId = tableId;
				fblk4->tableId = tableId;
			}
		}
	}
	
	/*  set extended bounding boxes of all table entries
	 *  so that they contain whole table
	 *  (we need to process whole table size when comparing it
	 *   with regular text blocks)
	 */
	PDFRectangle *envelopes = new PDFRectangle [numTables];
	TextBlock **ending_blocks = new TextBlock *[numTables];
	
	for (i = 0; i < numTables; i++) {
		envelopes[i].x1 = DBL_MAX;
		envelopes[i].x2 = DBL_MIN;
		envelopes[i].y1 = DBL_MAX;
		envelopes[i].y2 = DBL_MIN;
	}
	
	for (blk1 = blocks; blk1; blk1 = blk1->next) {
		if (blk1->tableId >= 0) {
			if (blk1->ExMin < envelopes[blk1->tableId].x1) {
				envelopes[blk1->tableId].x1 = blk1->ExMin;
				if (!blk1->page->primaryLR)
					ending_blocks[blk1->tableId] = blk1;
			}
			
			if (blk1->ExMax > envelopes[blk1->tableId].x2) {
				envelopes[blk1->tableId].x2 = blk1->ExMax;
				if (blk1->page->primaryLR)
					ending_blocks[blk1->tableId] = blk1;
			}
			
			envelopes[blk1->tableId].y1 = blk1->EyMin < envelopes[blk1->tableId].y1 ?
			blk1->EyMin : envelopes[blk1->tableId].y1;
			envelopes[blk1->tableId].y2 = blk1->EyMax > envelopes[blk1->tableId].y2 ?
			blk1->EyMax : envelopes[blk1->tableId].y2;
		}
	}
	
	for (blk1 = blocks; blk1; blk1 = blk1->next) {
		if (blk1->tableId >= 0 &&
			blk1->xMin <= ending_blocks[blk1->tableId]->xMax &&
			blk1->xMax >= ending_blocks[blk1->tableId]->xMin) {
			blk1->tableEnd = gTrue;
		}
	}
	
	for (blk1 = blocks; blk1; blk1 = blk1->next) {
		if (blk1->tableId >= 0) {
			blk1->ExMin = envelopes[blk1->tableId].x1;
			blk1->ExMax = envelopes[blk1->tableId].x2;
			blk1->EyMin = envelopes[blk1->tableId].y1;
			blk1->EyMax = envelopes[blk1->tableId].y2;
		}
	}
	delete[] envelopes;
	delete[] ending_blocks;
	
	
	/*  set extended bounding boxes of all other blocks
	 *  so that they extend in x without hitting neighbours
	 */
	for (blk1 = blocks; blk1; blk1 = blk1->next) {
		if (!blk1->tableId >= 0) {
			double xMax = DBL_MAX;
			double xMin = DBL_MIN;
			
			for (blk2 = blocks; blk2; blk2 = blk2->next) {
				if (blk2 == blk1)
					continue;
				
				if (blk1->yMin <= blk2->yMax && blk1->yMax >= blk2->yMin) {
					if (blk2->xMin < xMax && blk2->xMin > blk1->xMax)
						xMax = blk2->xMin;
					
					if (blk2->xMax > xMin && blk2->xMax < blk1->xMin)
						xMin = blk2->xMax;
				}
			}
			
			for (blk2 = blocks; blk2; blk2 = blk2->next) {
				if (blk2 == blk1)
					continue;
				
				if (blk2->xMax > blk1->ExMax &&
					blk2->xMax <= xMax &&
					blk2->yMin >= blk1->yMax) {
					blk1->ExMax = blk2->xMax;
				}
				
				if (blk2->xMin < blk1->ExMin &&
					blk2->xMin >= xMin &&
					blk2->yMin >= blk1->yMax)
					blk1->ExMin = blk2->xMin;
			}
		}
	}
	
	TextBlock **blkarray = (TextBlock **)gmallocn(nBlocks, sizeof(TextBlock *));
	for (blk = blocks, i = 0; blk; blk = blk->next, ++i) blkarray[i] = blk;
	qsort(blkarray, nBlocks, sizeof(TextBlock *), &TextBlock::cmpXYPrimaryRot);
	int sortPos = 0;
	GBool *visited = (GBool *)gmallocn(nBlocks, sizeof(GBool));
	for (i = 0; i < nBlocks; ++i) visited[i] = gFalse;
	for (blk = blocks, i = 0; blk; blk = blk->next, ++i)
		sortPos = blk->visitDepthFirst(blocks, i, blkarray, sortPos, visited);
	if (visited) gfree(visited);
	if (nBlocks > 0) {
		blocks = blkarray[0];
		blocks->prev = NULL;
		for (i = 0; i < nBlocks - 1; ++i) {
			blkarray[i]->next = blkarray[i + 1];
			blkarray[i + 1]->prev = blkarray[i];
		}
		lastBlk = blkarray[i];
		lastBlk->next = NULL;
	}
	else {
		blocks = NULL;
		lastBlk = NULL;
	}
	if (blkarray) gfree(blkarray);
	
	blk1 = blocks;
	blk1->xMinPre = blk1->xMin;
	blk1->xMaxPre = blk1->xMax;
	blk1->yMinPre = blk1->yMin;
	blk1->yMaxPre = blk1->yMax;
	for (blk2 = blk1->next; blk2; blk2 = blk2->next) {
		blk2->xMinPre = fmin(blk2->xMin, blk1->xMinPre);
		blk2->xMaxPre = fmax(blk2->xMax, blk1->xMaxPre);
		blk2->yMinPre = fmin(blk2->yMin, blk1->yMinPre);
		blk2->yMaxPre = fmax(blk2->yMax, blk1->yMaxPre);
		blk1 = blk2;
	}
	
	blk1 = lastBlk;
	blk1->xMinPost = blk1->xMin;
	blk1->xMaxPost = blk1->xMax;
	blk1->yMinPost = blk1->yMin;
	blk1->yMaxPost = blk1->yMax;
	for (blk2 = blk1->prev; blk2; blk2 = blk2->prev) {
		blk2->xMinPost = fmin(blk2->xMin, blk1->xMinPost);
		blk2->xMaxPost = fmax(blk2->xMax, blk1->xMaxPost);
		blk2->yMinPost = fmin(blk2->yMin, blk1->yMinPost);
		blk2->yMaxPost = fmax(blk2->yMax, blk1->yMaxPost);
		blk1 = blk2;
	}
	
	i = 0;
	for (blk = blocks; blk; blk = blk->next)
		for (line = blk->lines; line; line = line->next)
			for (word0 = line->words; word0; word0 = word0->next) {
				word0->index = i;
				i += word0->len + (word0->spaceAfter ? 1 : 0);
			}
}

//------------------------------------------------------------------------
// Search Text
//------------------------------------------------------------------------

GBool TextWord::startWith(Unicode *str, int length, GBool caseSen) {
	if (norm == NULL)
		norm = unicodeNormalizeNFKC(text, len, &normLen, NULL);
	if (normLen < length)
		return gFalse;
	for (int i = 0; i < length; ++i)
		if ((caseSen && norm[i] != str[i]) || (!caseSen && unicodeToUpper(norm[i]) != str[i]))
			return gFalse;
	return gTrue;
}

GBool TextWord::endWith(Unicode *str, int length, GBool caseSen) {
	if (norm == NULL)
		norm = unicodeNormalizeNFKC(text, len, &normLen, NULL);
	if (normLen < length)
		return gFalse;
	for (int i = 0; i < length; ++i)
		if ((caseSen && norm[normLen - i - 1] != str[length - i - 1]) || 
			(!caseSen && unicodeToUpper(norm[normLen - i - 1]) != str[length - i - 1]))
			return gFalse;
	return gTrue;
}

GBool TextWord::strEq(Unicode *str, int length, GBool caseSen) {
	if (norm == NULL)
		norm = unicodeNormalizeNFKC(text, len, &normLen, NULL);
	if (normLen != length)
		return gFalse;
	for (int i = 0; i < length; ++i)
		if ((caseSen && norm[i] != str[i]) || (!caseSen && unicodeToUpper(norm[i]) != str[i]))
			return gFalse;
	return gTrue;
}

GBool TextWord::contain(Unicode *str, int length, GBool caseSen) {
	if (norm == NULL)
		norm = unicodeNormalizeNFKC(text, len, &normLen, NULL);
	for (int i = 0; i + length <= normLen; ++i) {
		int j;
		for (j = 0; j < length; ++j)
			if ((caseSen && norm[i + j] != str[j]) || (!caseSen && unicodeToUpper(norm[i + j]) != str[j]))
				break;
		if (j == length) return gTrue;
	}
	return gFalse;
}

GooList *TextPage::searchText(Unicode *str, int length, GBool caseSen) {
	Unicode *strNorm = NULL;
	int strNormLen = 0, nWords = 0;
	GooList *result = new GooList();
	
	if (blocks == NULL) return result;
	strNorm = unicodeNormalizeNFKC(str, length, &strNormLen, NULL);
	GBool inWord = gFalse;
	for (int i = 0; i < strNormLen; ++i) {
		if (isspace(strNorm[i])) inWord = gFalse;
		else if (!inWord) {
				++nWords;
				inWord = gTrue;
			}
		if (!caseSen) strNorm[i] = unicodeToUpper(strNorm[i]);
	}
	if (nWords == 1) {
		for (TextWord *word = blocks->lines->words; word; word = word->nextWord())
			if (word->contain(str, length, caseSen))
				result->append(new PDFRectangle(word->xMin, word->yMin, word->xMax, word->xMax));
	}
	else if (nWords > 1) {
		int *startPos = (int *)gmallocn(nWords, sizeof(int));
		int *lens = (int *)gmallocn(nWords, sizeof(int));
		inWord = gFalse;
		int i, k;
		for (i = 0, k = -1; i < strNormLen; ++i) {
			if (isspace(strNorm[i])) {
				if (inWord) {
					lens[k] = i - startPos[k];
					inWord = gFalse;
				}
			}
			else if (!inWord) {
				startPos[++k] = i;
				inWord = gTrue;
			}
		}
		if (inWord) lens[k] = i - startPos[k];
		for (TextWord *word0 = blocks->lines->words; word0; word0 = word0->nextWord()) {
			if (!word0->endWith(strNorm + startPos[0], lens[0], caseSen))
				continue;
			TextWord *word;
			int i;
			for (i = 1, word = word0->nextWord(); i < nWords - 1 && word; ++i, word = word->nextWord())
				if (!word->strEq(strNorm + startPos[i], lens[i], caseSen))
					break;
			if (i < nWords - 1) {
				if (word) continue;
				else break;
			}
			if (!word->startWith(strNorm + startPos[i], lens[i], caseSen))
				continue;
			
			PDFRectangle *lastRect = new PDFRectangle(word0->xMin, word0->yMin, word0->xMax, word0->yMax);
			TextWord *lastWord = word0;
			result->append(lastRect);
			for (word0 = word0->nextWord(); word0 != word; word0 = word0->nextWord()) {
				if (lastWord->line == word0->line) {
					if (lastRect->x1 > word0->xMin) lastRect->x1 = word0->xMin;
					if (lastRect->x2 < word0->xMax) lastRect->x2 = word0->xMax;
					if (lastRect->y1 > word0->xMin) lastRect->y1 = word0->yMin;
					if (lastRect->y2 < word0->yMax) lastRect->y2 = word0->yMax;
				}
				else {
					lastRect = new PDFRectangle(word0->xMin, word0->yMin, word0->xMax, word0->yMax);
					result->append(lastRect);
				}
				lastWord = word0;
			}
			if (lastWord->line == word->line) {
				if (lastRect->x1 > word->xMin) lastRect->x1 = word->xMin;
				if (lastRect->x2 < word->xMax) lastRect->x2 = word->xMax;
				if (lastRect->y1 > word->xMin) lastRect->y1 = word->yMin;
				if (lastRect->y2 < word->yMax) lastRect->y2 = word->yMax;
			}
			else
				result->append(new PDFRectangle(word->xMin, word->yMin, word->xMax, word->yMax));
		}
		gfree(startPos);
		gfree(lens);
	}
	gfree(strNorm);
	for (int i = 0; i < result->getLength(); ++i) {
		PDFRectangle *rect = (PDFRectangle *)result->get(i);
		rect->x1 /= pageWidth;
		rect->y1 /= pageHeight;
		rect->x2 /= pageWidth;
		rect->y2 /= pageHeight;
	}
	return result;
}

//------------------------------------------------------------------------
// Text Selection
//------------------------------------------------------------------------

#define dist(obj, px, py)		(fmax((obj)->xMin - (px), 0.0) + fmax((px) - (obj)->xMax, 0.0) + \
								fmax((obj)->yMin - (py), 0.0) + fmax((py) - (obj)->yMax, 0.0))

#define distPre(obj, px, py)	(fmax((obj)->xMinPre - (px), 0.0) + fmax((px) - (obj)->xMaxPre, 0.0) + \
								fmax((obj)->yMinPre - (py), 0.0) + fmax((py) - (obj)->yMaxPre, 0.0))

#define distPost(obj, px, py)	(fmax((obj)->xMinPost - (px), 0.0) + fmax((px) - (obj)->xMaxPost, 0.0) + \
								fmax((obj)->yMinPost - (py), 0.0) + fmax((py) - (obj)->yMaxPost, 0.0))


TextWord *TextPage::findNearest(double x, double y, TextWord *start) {
	if (blocks == NULL) return NULL;
	double mindist;
	if (start == NULL) {
		mindist = DBL_MAX;
		TextBlock *bestblk;
		for (TextBlock *blk = blocks; blk && mindist > 0; blk = blk->next) {
			double d = dist(blk, x, y);
			if (d < mindist) {
				mindist = d;
				bestblk = blk;
			}
		}
		mindist = DBL_MAX;
		TextLine *bestline;
		for (TextLine *line = bestblk->lines; line && mindist > 0; line = line->next) {
			double d= dist(line, x, y);
			if (d < mindist) {
				mindist = d;
				bestline = line;
			}
		}
		mindist = DBL_MAX;
		for (TextWord *word = bestline->words; word && mindist > 0; word = word->next) {
			double d = dist(word, x, y);
			if (d < mindist) {
				mindist = d;
				start = word;
			}
		}
	}
	else mindist = dist(start, x, y);
	
	TextWord *word = start, *bestword = start;
	TextLine *line = word->line;
	TextBlock *blk = line->blk;
	word = word->next;
	while (true) {
		if (word == NULL || mindist < distPost(word, x, y)) {
			line = line->next;
			if (line == NULL || mindist < distPost(line, x, y)) {
				blk = blk->next;
				if (blk == NULL || mindist < distPost(blk, x, y)) break;
				line = blk->lines;
			}
			word = line->words;
			continue;
		}
		double d = dist(word, x, y);
		if (d < mindist) {
			mindist = d;
			bestword = word;
			if (mindist == 0) break;
		}
		word = word->next;
	}
	word = start;
	line = word->line;
	blk = line->blk;
	word = word->prev;
	while (true) {
		if (word == NULL || mindist < distPre(word, x, y)) {
			line = line->prev;
			if (line == NULL || mindist < distPre(line, x, y)) {
				blk = blk->prev;
				if (blk == NULL || mindist < distPre(blk, x, y)) break;
				line = blk->lastLine;
			}
			word = line->lastWord;
			continue;
		}
		double d = dist(word, x, y);
		if (d < mindist) {
			mindist = d;
			bestword = word;
			if (mindist == 0) break;
		}
		word = word->prev;
	}
	return bestword;
}

int TextPage::calIdx(double x, double y, TextWord *&word) {
	double pos, offset;
	switch (word->rot) {
		case 0:
			pos = x;
			offset = (x - word->xMin) / (word->xMax - word->xMin);
			break;
		case 1:
			pos = y;
			offset = (y - word->yMin) / (word->yMax - word->yMin);
			break;
		case 2:
			pos = x;
			offset = (word->xMax - x) / (word->xMax - word->xMin);
			break;
		case 3:
			pos = y;
			offset = (word->yMax - y) / (word->yMax - word->yMin);
			break;
		default:  // impossible
			return 0;
	}
	int rtn = (int)floor(offset * word->len);
	if (rtn >= 0 && rtn < word->len) {
		if (word->rot == 0 || word->rot == 1) {
			while (rtn < word->len && word->edge[rtn + 1] < pos) ++rtn;
			while (rtn >= 0 && word->edge[rtn] > pos) --rtn;
		}
		else {
			while (rtn < word->len && word->edge[rtn + 1] > pos) ++rtn;
			while (rtn >= 0 && word->edge[rtn] < pos) --rtn;
		}
	}
	if (rtn < 0) {
		if (word->prev && word->prev->spaceAfter) {
			word = word->prev;
			return word->len;
		}
		return 0;
	}
	if (rtn >= word->len) {
		if (word->spaceAfter) return word->len;
		return word->len - 1;
	}
	return rtn;
}

void TextPage::startSelection(double x, double y) {
	x *= pageWidth;
	y *= pageHeight;
	selStart = findNearest(x, y);
	selIdx1 = calIdx(x, y, selStart);
	selEnd = selStart;
	selIdx2 = selIdx1;
	selIdxSave = selIdx1;
}

GBool TextPage::moveSelEndTo(double x, double y) {
	if (selStart == NULL) return gFalse;
	int oldIdx = selIdx2 + selEnd->index;
	x *= pageWidth;
	y *= pageHeight;
	selEnd = findNearest(x, y, selEnd);
	selIdx2 = calIdx(x, y, selEnd);
	if (selStart == selEnd ||
		(selStart->next == selEnd && selIdxSave == selStart->len) ||
		(selEnd->next == selStart && selIdx2 == selEnd->len))
		selIdx1 = selIdxSave;
	else {
		if (selStart->index < selEnd->index) {
			if (selIdx1 < selStart->len) selIdx1 = 0;
			if (selIdx2 < selEnd->len) selIdx2 = selEnd->len - 1;
		}
		else {
			if (selIdx2 < selEnd->len) selIdx2 = 0;
			if (selIdx1 < selStart->len) selIdx1 = selStart->len - 1;
		}
	}
	return oldIdx != selIdx2 + selEnd->index;
}

GooList *TextPage::getSelectedRegion() {
	GooList *result = new GooList();
	if (selStart == NULL) return result;
	TextWord *begin, *end;
	int bIdx, eIdx;
	if (selStart->index + selIdx1 < selEnd->index + selIdx2) {
		begin = selStart;
		bIdx = selIdx1;
		end = selEnd;
		eIdx = selIdx2;
	}
	else {
		begin = selEnd;
		bIdx = selIdx2;
		end = selStart;
		eIdx = selIdx1;
	}
	if (eIdx == end->len) {
		end = end->next;
		eIdx = -1;
	}
	TextLine *line = begin->line;
	switch (line->rot) {
		case 0:
			result->append(new PDFRectangle(begin->edge[bIdx], line->yMin, line->xMax, line->yMax));
			break;
		case 1:
			result->append(new PDFRectangle(line->xMin, begin->edge[bIdx], line->xMax, line->yMax));
			break;
		case 2:
			result->append(new PDFRectangle(line->xMin, line->yMin, begin->edge[bIdx], line->yMax));
			break;
		case 3:
			result->append(new PDFRectangle(line->xMin, line->yMin, line->xMax, begin->edge[bIdx]));
			break;
	}
	TextLine *stop = end->line->next;
	for (line = line->next; line != stop; line = line->next)
		result->append(new PDFRectangle(line->xMin, line->yMin, line->xMax, line->yMax));
	line = end->line;
	PDFRectangle *last = (PDFRectangle *)result->get(result->getLength() - 1);
	switch (line->rot) {
		case 0:
			last->x2 = end->edge[eIdx + 1];
			break;
		case 1:
			last->y2 = end->edge[eIdx + 1];
			break;
		case 2:
			last->x1 = end->edge[eIdx + 1];
			break;
		case 3:
			last->y1 = end->edge[eIdx + 1];
			break;
	}
	for (int i = 0; i < result->getLength(); ++i) {
		PDFRectangle *rect = (PDFRectangle *)result->get(i);
		rect->x1 /= pageWidth;
		rect->y1 /= pageHeight;
		rect->x2 /= pageWidth;
		rect->y2 /= pageHeight;
	}
	return result;
}

static inline void appendUni(Unicode *&buf, int &pos, int &size, Unicode *str, int len) {
	if (pos + len > size) {
		size = ((pos + len + 127) & ~0x7f);
		buf = (Unicode *)greallocn(buf, size, sizeof(Unicode));
	}
	for (int i = 0; i < len; ++i)
		buf[pos++] = str[i];
}

static inline void appendUniCh(Unicode *&buf, int &pos, int &size, Unicode ch) {
	if (pos == size) {
		size += 128;
		buf = (Unicode *)greallocn(buf, size, sizeof(Unicode));
	}
	buf[pos++] = ch;
}

Unicode *TextPage::getSelectedText(GBool normalize, int *length) {
	if (selStart == NULL) return NULL;
	TextWord *begin, *end;
	int bIdx, eIdx;
	if (selStart->index + selIdx1 < selEnd->index + selIdx2) {
		begin = selStart;
		bIdx = selIdx1;
		end = selEnd;
		eIdx = selIdx2;
	}
	else {
		begin = selEnd;
		bIdx = selIdx2;
		end = selStart;
		eIdx = selIdx1;
	}
	int size = ((end->index + eIdx - begin->index - bIdx + 255) & ~0x7f) ;
	*length = 0;
	Unicode *result = (Unicode *)gmallocn(size, sizeof(Unicode));
	GBool appendSpace = gFalse;
	if (bIdx == begin->len) {
		result[(*length)++] = (Unicode)' ';
		if (begin == end && bIdx == eIdx) return result;
		begin = begin->next;
		bIdx = 0;
	}
	if (eIdx == end->len) {
		appendSpace = gTrue;
		--eIdx;
	}
	if (begin == end) {
		if (normalize) {
			int nlen;
			Unicode *tmp = unicodeNormalizeNFKC(begin->text + bIdx, eIdx - bIdx + 1, &nlen, NULL);
			appendUni(result, *length, size, tmp, nlen);
			gfree(tmp);
		}
		else appendUni(result, *length, size, begin->text + bIdx, eIdx - bIdx + 1);
	}
	else {
		if (normalize) {
			int nlen;
			Unicode *tmp = unicodeNormalizeNFKC(begin->text + bIdx, begin->len - bIdx, &nlen, NULL);
			appendUni(result, *length, size, tmp, nlen);
			gfree(tmp);
		}
		else appendUni(result, *length, size, begin->text + bIdx, begin->len - bIdx);
		if (begin->spaceAfter) appendUniCh(result, *length, size, (Unicode)' ');
		TextLine *line = begin->line;
		TextBlock *blk = line->blk;
		for (begin = begin->next; ; begin = begin->next) {
			if (begin == NULL) {
				appendUniCh(result, *length, size, (Unicode)'\n');
				line = line->next;
				if (line == NULL) {
					blk = blk->next;
					if (blk == NULL) break;
					line = blk->lines;
				}
				begin = line->words;
			}
			if (begin == end) break;
			if (normalize) {
				if (begin->norm == NULL)
					begin->norm = unicodeNormalizeNFKC(begin->text, begin->len, &begin->normLen, NULL);
				appendUni(result, *length, size, begin->norm, begin->normLen);
			}
			else appendUni(result, *length, size, begin->text, begin->len);
			if (begin->spaceAfter) appendUniCh(result, *length, size, (Unicode)' ');
		}
		if (normalize) {
			int nlen;
			Unicode *tmp = unicodeNormalizeNFKC(end->text, eIdx + 1, &nlen, NULL);
			appendUni(result, *length, size, tmp, nlen);
			gfree(tmp);
		}
		else appendUni(result, *length, size, end->text, eIdx);
	}
	if (appendSpace)
		appendUniCh(result, *length, size, (Unicode)' ');
	return result;
}
