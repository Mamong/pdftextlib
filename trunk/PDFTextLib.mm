//
//  PDFTextLib.mm
//
//  Created by tarlou on 11-5-25.
//

#include "GooList.h"
#include "Page.h"
#include "PDFDoc.h"
#include "TextOutputDev.h"
#include "GlobalParams.h"
#include "gmem.h"
#include "Object.h"

#import "PDFTextLib.h"

@implementation PDFTextLib

+ (void)globalInit
{
	// Please set resPath to be the parent of "poppler-data"
	NSString *resPath = [[NSBundle mainBundle] resourcePath];
	globalParams = new GlobalParams([resPath cStringUsingEncoding:NSUTF8StringEncoding]);
}

+ (void)globalRelease
{
	if (globalParams) delete globalParams;
#ifdef DEBUG_MEM
	Object::memCheck(stderr);
	gMemReport(stderr);
#endif
}

- (id)initWithFilename:(NSString *)filename
{
	return [self initWithFilenameAndPassword:filename userPW:nil ownerPW:nil];
}

- (id)initWithFilenameAndPassword:(NSString *)filename userPW:(NSString *)userPW ownerPW:(NSString *)ownerPW
{
	if (self != [super init]) return nil;
	doc = new PDFDoc([filename cStringUsingEncoding:NSUTF8StringEncoding],
					 [userPW cStringUsingEncoding:NSUTF8StringEncoding],
					 [ownerPW cStringUsingEncoding:NSUTF8StringEncoding]);
	if (!doc->isOk()) {
		[self release];
		return nil;
	}
	numPages = doc->getNumPages();
	pages = new TextPage *[numPages];
	for (int i = 0; i < numPages; ++i)
		pages[i] = NULL;
	return self;
}

- (void)dealloc
{
	if (pages) {
		for (int i = 0; i < numPages; ++i)
			if (pages[i]) delete pages[i];
		delete [] pages;
	}
	if (doc) delete doc;
	
	CGPathRelease(selectPath);
	CGPathRelease(searchPath);
	[super dealloc];
}

- (TextPage *)touchPage:(NSInteger)pageNum
{
	if (pageNum <= 0 || pageNum > numPages)
		return NULL;
	if (pages[pageNum - 1] == NULL) {
		pages[pageNum - 1] = new TextPage(doc, pageNum);
		if (!pages[pageNum - 1]->isOk()) {
			printf("Error on page %d.\n", (int)pageNum);
			delete pages[pageNum - 1];
			pages[pageNum - 1] = NULL;
			return NULL;
		}
	}
	return pages[pageNum - 1];
}

- (void)setBeginCoordinate:(double)x andY:(double)y onPage:(NSInteger)pageNum
{
	TextPage *page;
	if (!(page = [self touchPage:pageNum])) return;
	page->startSelection(x, y);
}

- (CGPathRef)fromBeginToCoordinate:(double)x andY:(double)y onPage:(NSInteger)pageNum
{
	TextPage *page;
	if (!(page = [self touchPage:pageNum])) return nil;
	if (!page->moveSelEndTo(x, y)) return nil;
	
	GooList *rects = page->getSelectedRegion();
	
	CGPathRelease(selectPath);
	selectPath = CGPathCreateMutable();
	for (int i = 0; i < rects->getLength(); ++i) {
		PDFRectangle *rect = (PDFRectangle *)rects->get(i);
		CGPathMoveToPoint(selectPath, NULL, rect->x1, rect->y1);
		CGPathAddLineToPoint(selectPath, NULL, rect->x2, rect->y1);
		CGPathAddLineToPoint(selectPath, NULL, rect->x2, rect->y2);
		CGPathAddLineToPoint(selectPath, NULL, rect->x1, rect->y2);
		CGPathCloseSubpath(selectPath);
	}
	deleteGooList(rects, PDFRectangle);
	return selectPath;
}

+ (int)toUTF16String:(Unicode *)buf Length:(int)len
{
	unichar *p = (unichar *)buf;
	int utf16Length = 0;
	for (int i = 0; i < len; ++i) {
		if (buf[i] > 0xffff && buf[i] < 0x110000) {
			Unicode tmp = buf[i] - 0x10000;
			*p++ = (unichar)((tmp >> 10) + 0xd800);
			*p++ = (unichar)((tmp & 0x3ff) + 0xdc00);
			utf16Length += 2;
		} else {
			*p++ = (unichar)buf[i];
			++utf16Length;
		}
	}
	return utf16Length;
}

- (NSString *)getSelectedText:(BOOL)normalize onPage:(NSInteger)pageNum
{
	TextPage *page;
	if (!(page = [self touchPage:pageNum])) return @"";
	
	int len;
	Unicode *buf = page->getSelectedText(normalize ? gTrue : gFalse, &len);
	if (buf == NULL) return @"";
	
	int utf16Length = [PDFTextLib toUTF16String:buf Length:len];
	NSString *rtn = [[NSString alloc] initWithCharacters:(unichar *)buf length:utf16Length];
	gfree(buf);
	[rtn autorelease];
	return rtn;
}

+ (Unicode *)toUTF32String:(NSString *)str Length:(int *)len
{
	Unicode *buf = new Unicode[[str length]];
	*len = 0;
	Unicode save = 0;
	for (int i = 0; i < [str length]; ++i) {
		Unicode tmp = [str characterAtIndex:i];
		if (tmp >= 0xd800 && tmp < 0xdc00) {
			if (save) buf[(*len)++] = 0xfffd;
			save = tmp;
		}
		else if (tmp >= 0xdc00 && tmp < 0xe000) {
			if (save) {
				buf[(*len)++] = (((save & 0x3ff) << 10) | (tmp & 0x3ff)) + 0x10000;
				save = 0;
			}
			else buf[(*len)++] = 0xfffd;
		}
		else {
			if (save) {
				buf[(*len)++] = 0xfffd;
				save = 0;
			}
			buf[(*len)++] = tmp;
		}
	}
	return buf;
}

- (CGPathRef)searchResultForKeyWord:(NSString *)keyWord caseSensitive:(BOOL)caseSen onPage:(NSInteger)pageNum
{
	TextPage *page;
	if (!(page = [self touchPage:pageNum])) return nil;
	
	int len;
	Unicode *buf = [PDFTextLib toUTF32String:keyWord Length:&len];
	GooList *rects = page->searchText(buf, len, caseSen);
	delete [] buf;
	
	CGPathRelease(searchPath);
	searchPath = CGPathCreateMutable();
	for (int i = 0; i < rects->getLength(); ++i) {
		PDFRectangle *rect = (PDFRectangle *)rects->get(i);
		CGPathMoveToPoint(searchPath, NULL, rect->x1, rect->y1);
		CGPathAddLineToPoint(searchPath, NULL, rect->x2, rect->y1);
		CGPathAddLineToPoint(searchPath, NULL, rect->x2, rect->y2);
		CGPathAddLineToPoint(searchPath, NULL, rect->x1, rect->y2);
		CGPathCloseSubpath(searchPath);
	}
	deleteGooList(rects, PDFRectangle);
	return searchPath;
}

@end
