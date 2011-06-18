#import <Foundation/Foundation.h>
#import "PDFTextLib.h"

int main (int argc, const char * argv[]) {
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
	[PDFTextLib globalInit];
	PDFTextLib *pdfTextLib = [[PDFTextLib alloc] initWithFilename:@"/Users/tarlou/test.pdf"];
	// Do somthing.
	[pdfTextLib release];
	[PDFTextLib globalRelease];
    [pool drain];
    return 0;
}
