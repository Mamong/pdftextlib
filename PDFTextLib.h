//
//  PDFTextLib.h
//
//  Created by tarlou on 11-5-25.
//

#import <Foundation/Foundation.h>

// declare C classes, the following idea is from 
// http://www.zedkep.com/blog/index.php?/archives/247-Forward-declaring-C++-classes-in-Objective-C.html
struct PDFDoc;
typedef struct PDFDoc PDFDoc;
struct TextPage;
typedef struct TextPage TextPage;

@interface PDFTextLib : NSObject {
	PDFDoc *doc;
	TextPage **pages;
	int numPages;
	
	CGMutablePathRef selectPath;
	CGMutablePathRef searchPath;
}

// Please call this before any [PDFTextLib alloc]
// Modify this for poppler-data
+ (void)globalInit;

// Please call this after all [PDFTextLib release]
+ (void)globalRelease;

- (id)initWithFilename:(NSString *)filename;

- (id)initWithFilenameAndPassword:(NSString *)filename userPW:(NSString *)userPW ownerPW:(NSString *)ownerPW;


////////////////////////////////////////////////////////////////////////////////
// Selection Functions                                                        //
// Selections are done independently on different pages.                      //
////////////////////////////////////////////////////////////////////////////////

// Set the beginning point of a text selection. This will automatically set the end point to be the same as begin,
// i.e. select one character.
- (void)setBeginCoordinate:(double)x andY:(double)y onPage:(NSInteger)pageNum;

// Move the end point of selection. Return nil if no change needed or error.
// Please call setBeginCoordinate first.
// The CGPath object will be overwritten for the next call.
// Don't release it, it will be released at [PDFTextLib release]
- (CGPathRef)fromBeginToCoordinate:(double)x andY:(double)y onPage:(NSInteger)pageNum;

// Get the text of selection.
// Please call setBeginCoordinate first.
// Set normalize=YES to do NFKC normalization on the text.
// @"" is returned if nothing.
// Autorelease.
- (NSString *)getSelectedText:(BOOL)normalize onPage:(NSInteger)pageNum;


////////////////////////////////////////////////////////////////////////////////
// Searching Function                                                         //
// Searchings are done independently on different pages.                      //
////////////////////////////////////////////////////////////////////////////////

// Search on a page. Behaves like Adobe Reader.
// Return all words matches keyWord on the page. Return bounding boxes of *WHOLE* words.
// If keyWord contains multiple words w1,..., wk, we look for a string that the first word
// ends with w1, the last word starts with wk, all words in between equal to the queries.
- (CGPathRef)searchResultForKeyWord:(NSString *)keyWord caseSensitive:(BOOL)caseSen onPage:(NSInteger)pageNum;

@end
