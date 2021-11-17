//idoc(parent,examples)
//idoc(tutorial,example_lz_simple,example_lz_simple : Example demonstrating very simple LZ memory->memory compression using only Oodle Core)

/*

Oodle example_lz_simple

Very simple example of OodleLZ memory -> memory compression & decompression.

Uses stdio for file IO to load an input file.

See $example_lz for more advanced OodleLZ usage.

example_lz_simple only uses Oodle Core, no Oodle Ext

include oodle2.h and not oodle2x.h

*/

#include "../include/oodle2.h"

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef BUILDING_EXAMPLE_CALLER
#define main example_lz_simple
#endif

#include "read_whole_file.h"

extern "C" int main(int argc,char *argv[])
{
/* No initialization is needed for Oodle2 Core

    we let Oodle Core use the default system plugins (the C stdlib)
    To change them, use $OodleAPI_OodleCore_Plugins
*/

    // optional check to make sure header matches lib :
    if ( ! Oodle_CheckVersion(OODLE_HEADER_VERSION) )
    {
        fprintf(stderr,"Oodle header version mismatch\n");
        return 10;
    }

    // get args :
    const char * in_name;
    if ( argc < 2 )
    {
        in_name = "r:\\testsets\\lztestset\\lzt02";
    }
    else
    {
        in_name = argv[1];
    }
    
/* read input file using stdio

*/
    OO_SINTa length;
    void * buf = read_whole_file(in_name,&length);
    if ( ! buf )
    {
        fprintf(stderr,"couldn't open : %s\n",in_name);
        return 10;
    }
    

/*

Run OodleLZ_Compress from memory (buf) to memory (compbuf)

Use the OodleLZ_Compressor_Kraken compressor.  Kraken is an amazing balance of good compression and fast decode
speed.  It should generally be your first choice, then try Mermaid or Leviathan if you want faster decodes or more
compression.

Use OodleLZ_CompressionLevel_Normal level of effort in the encoder.  Normal is a balance of encode speed and compression
ratio.  Different levels trade off faster or slower encoding for compressed size.  See $OodleLZ_CompressionLevel.

See $OodleLZ_About for information on selection of the compression options.

This call is synchronous and not threaded; see $example_lz for an example using the async compression APIs.

*/

    OodleLZ_Compressor compressor = OodleLZ_Compressor_Kraken;
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Normal;
    //OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Optimal; // for high compression, slower encode

    // allocate memory big enough for compressed data :
    void * compbuf = malloc( OodleLZ_GetCompressedBufferSizeNeeded(compressor,length) + sizeof(length) );
    if ( compbuf == NULL )
        return 10;
        
    char * compptr = (char *)compbuf;
    memcpy(compptr,&length,sizeof(length));
    compptr += sizeof(length);

    // compress :
    OO_SINTa complen = OodleLZ_Compress(compressor,buf,length,compptr,level);
    compptr += complen;

    // log about it :
    //  full compressed size also includes the header +(int)sizeof(length)
    printf("%s compressed %d -> %d\n",in_name,(int)length,(int)complen);

/*

Run OodleLZ_Decompress from memory (compbuf) to memory (decbuf)

Note that you must provide the exact decompressed size.  OodleLZ data is headerless; store the size in
your own header.

We will allocate the needed decoder scratch mem to pass in, then OodleLZ_Decompress will do no allocations
internally.  In real use you might want to keep the scratch mem allocated across calls so it doesn't need to
be allocated and freed each time.  The scratch mem can be reused, but cannot be used by multiple threads at the
same time.  See also example_lz_noallocs

*/

    OO_SINTa declength;
    compptr = (char *)compbuf;
    memcpy(&declength,compptr,sizeof(declength));
    compptr += sizeof(declength);
    assert( length == declength );

    // malloc for decompressed buffer  :
    void * decbuf = malloc( declength );

    // allocate the decoder scratch memory needed
    //  if we pass NULL for these (the default argument)
    //  then OodleLZ_Decompress will allocate them internally
    OO_SINTa decoderScratchMemSize = OodleLZDecoder_MemorySizeNeeded(compressor,-1);
    void * decoderScratchMem = malloc( decoderScratchMemSize );

    // do the decompress :
    OO_SINTa decompress_return = OodleLZ_Decompress(compptr,complen,decbuf,declength,
        OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,NULL,0,NULL,NULL,
        decoderScratchMem,decoderScratchMemSize);

    // check it was successful :
    assert( decompress_return == length );
    assert( memcmp(buf,decbuf,length) == 0 );

    if ( decompress_return != length )
        return 10;

    printf("decompressed successfully.\n");
    
/*

And finish up.  No shutdown is needed for Oodle2 Core.

*/

    // free all the memory :
    free(buf);
    free(compbuf);
    free(decbuf);
    free(decoderScratchMem);

    return 0;
}

