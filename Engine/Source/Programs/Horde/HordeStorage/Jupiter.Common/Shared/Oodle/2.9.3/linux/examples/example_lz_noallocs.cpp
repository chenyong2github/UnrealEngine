//idoc(parent,examples)
//idoc(tutorial,example_lz_noallocs,example_lz_noallocs : Example demonstrating Oodle compression with no allocations)

/*

Oodle example_lz_noallocs

Very simple example of OodleLZ memory -> memory compression & decompression.

Shows how to use Oodle without any allocations done by Oodle.  All memory needed is passed in by the client.

Uses stdio for file IO to load an input file.

See $example_lz for more advanced OodleLZ usage.

example_lz_noallocs only uses Oodle Core, no Oodle Ext

include oodle2.h and not oodle2x.h

If you want to use Oodle with no allocations, you cannot use OodleX.  OodleX installs its own allocator into Oodle Core.
Do not use OodlePlugins_SetAllocators with OodleX.

See $Oodle_FAQ_UseOodleWithNoAllocator and $OodleCore_Plugins_SetAllocators

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
#define main example_lz_noallocs
#endif

#include "read_whole_file.h"


void * OODLE_CALLBACK example_noallocs_MallocAligned_Logging(OO_SINTa size,OO_S32 alignment)
{
    // malloc should not be called, log an error :
    printf("ERROR : example_noallocs_MallocAligned_Logging called (size %d)\n",(int)size);

    // use default Oodle MallocAligned as an easy way to get a cross-platform aligned malloc :
    return OodleCore_Plugin_MallocAligned_Default(size,alignment);
}

void OODLE_CALLBACK example_noallocs_Free_Logging(void * ptr)
{
    // malloc should not be called, log an error :
    printf("ERROR : example_noallocs_Free_Logging called.\n");
    
    OodleCore_Plugin_Free_Default(ptr);
}


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

/* Install our own allocator plugins that log an error if called.

    These should never be called.  You could also disable them :
    OodleCore_Plugins_SetAllocators(NULL,NULL);
    
    but that is not recommended because it will cause a hard failure if Oodle ever needs memory.
*/
    OodleCore_Plugins_SetAllocators(example_noallocs_MallocAligned_Logging,example_noallocs_Free_Logging);

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
speed.  It should generally be your first choice.

Use OodleLZ_CompressionLevel_Normal level of effort in the encoder.  Normal is a balance of encode speed and compression
ratio.  Different levels trade off faster or slower encoding for compressed size.  See $OodleLZ_CompressionLevel.

See $OodleLZ_About for information on selection of the compression options.

This call is synchronous and not threaded; see $example_lz for an example using the async compression APIs.

*/

    OodleLZ_Compressor compressor = OodleLZ_Compressor_Kraken;
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Normal;
    //OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Optimal2; // optimals are OODLELZ_SCRATCH_MEM_NO_BOUND

    // allocate memory big enough for compressed data :
    void * compbuf = malloc( OodleLZ_GetCompressedBufferSizeNeeded(compressor,length) + sizeof(length) );
    if ( compbuf == NULL )
        return 10;

    // allocate memory for encoder scratch :
    OO_SINTa enc_scratch_size = OodleLZ_GetCompressScratchMemBound(compressor,level,length,NULL);
    if ( enc_scratch_size == OODLELZ_SCRATCH_MEM_NO_BOUND )
    {
        // scratch cannot be bounded for this choice of compressor/level
        // the allocator may be used!
        // go ahead and give it 4 MB of scratch
        enc_scratch_size = 4*1024*1024;
    }
    
    void * enc_scratch = malloc(enc_scratch_size);
    if ( enc_scratch == NULL )
        return 10;  
        
    char * compptr = (char *)compbuf;
    memcpy(compptr,&length,sizeof(length));
    compptr += sizeof(length);

    // compress :
    OO_SINTa complen = OodleLZ_Compress(compressor,buf,length,compptr,level,NULL,NULL,NULL,enc_scratch,enc_scratch_size);
    compptr += complen;

    // log about it :
    //  full compressed size also includes the header
    printf("%s compressed %d -> %d (+%d)\n",in_name,(int)length,(int)complen,(int)sizeof(length));

    // can free enc_scratch now
    // enc_scratch can be reused for further compression
    //  but must be used by only one thread at a time
    free(enc_scratch); enc_scratch = NULL;

/*

Run OodleLZ_Decompress from memory (compbuf) to memory (decbuf)

Note that you must provide the exact decompressed size.  OodleLZ data is headerless; store the size in
your own header.

*/

    OO_SINTa declength;
    compptr = (char *)compbuf;
    memcpy(&declength,compptr,sizeof(declength));
    compptr += sizeof(declength);
    assert( length == declength );

    // malloc for decompressed buffer  :
    void * decbuf = malloc( declength );
    if ( decbuf == NULL )
        return 10;      

    OO_SINTa decoder_mem_size = OodleLZDecoder_MemorySizeNeeded(compressor,declength);
    void * decoder_mem = malloc(decoder_mem_size);
    if ( decoder_mem == NULL )
        return 10;      

    // do the decompress :
    OO_SINTa decompress_return = OodleLZ_Decompress(compptr,complen,decbuf,declength,
            OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_Yes,OodleLZ_Verbosity_None,NULL,0,NULL,NULL,
            decoder_mem,decoder_mem_size,
            OodleLZ_Decode_Unthreaded);
    
    // check it was successful :
    assert( decompress_return == length );
    assert( memcmp(buf,decbuf,length) == 0 );

    if ( decompress_return != length )
        return 10;

    printf("decompessed successfully.\n");
        
/*

And finish up.  No shutdown is needed for Oodle2 Core.

*/

    // free all the memory :
    free(buf);
    free(compbuf);
    free(decbuf);
    free(decoder_mem);

    return 0;
}
