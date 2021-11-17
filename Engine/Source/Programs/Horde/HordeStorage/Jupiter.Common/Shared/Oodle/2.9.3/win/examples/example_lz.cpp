//idoc(parent,examples)
//idoc(tutorial,example_lz,example_lz : Example demonstrating LZ compression and decompression)

/*

Oodle example_lz

Use the various LZ compress/decompress API's

API's demonstrated here :

OodleLZ : low level buffer compress/decompress

OodleLZDecoder : streaming decoder

OodleLZ_Async : high level async helpers

*/

#include "../include/oodle2x.h"
#include "ooex.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "make_example_input.h"

#ifdef BUILDING_EXAMPLE_CALLER
#define main example_lz
#endif

//===========================================================
// file names :

static const char * in_name = "oodle_example_input_file";
static const char * out_name = "oodle_example_output_file";

// just make the input buffer global here so all the tests can use it :
static void * in_buffer = NULL;
static OO_SINTa in_size = 0;

//===========================================================

// protos :

void lz_test_1();
void lz_test_2();
void lz_test_4();
void lz_test_9();
void lz_test_10();
void lz_test_11();
void lz_test_12();
void lz_test_13();

//===========================================================

extern "C" int main(int argc,char *argv[])
{
    OodleXLog_Printf_v1("usage: example_lz [in] [out]\n");

    // Init Oodle systems with default options :
    OodleXInitOptions opts;
    if ( ! OodleX_Init_GetDefaults(OODLE_HEADER_VERSION,&opts) )
    {
        fprintf(stderr,"Oodle header version mismatch.\n");
        return 10;
    }
    // change opts here if you like
    if ( ! OodleX_Init(OODLE_HEADER_VERSION,&opts) )
    {
        fprintf(stderr,"OodleX_Init failed.\n");
        return 10;
    }

    if ( argc >= 2 ) in_name = argv[1];
    else make_example_input(in_name);
    
    if ( argc >= 3 ) out_name = argv[2];

    OodleXLog_Printf_v1("lz test %s to %s\n",in_name,out_name);

    // read the input file to the global buffer :
    OO_S64 in_size_64;
    in_buffer = OodleXIOQ_ReadMallocWholeFile_AsyncAndWait(in_name,&in_size_64);
    
    if ( ! in_buffer)
    {
        OodleXLog_Printf_v0("failed to read %s\n",in_name); 
        return 10;
    }
    
    in_size = OodleX_S64_to_SINTa_check( in_size_64 );
    
    lz_test_1();
    lz_test_2();
    lz_test_4();
    lz_test_9();
    lz_test_10();
    lz_test_11();
    lz_test_12();
    lz_test_13();

    OodleXLog_Printf_v1("\ndone.\n");

    OodleXFree_IOAligned(in_buffer);

    //OodleX_Shutdown();
    OodleX_Shutdown(NULL,OodleX_Shutdown_LogLeaks_Yes,0);
    
    //OodleXLog_Printf_v1("press a key\n");
    //fgetc(stdin);
    
    return 0;
}

//=================================================

/*

lz_test_1 :

example of directly calling the simple buffer->buffer compression API's

OodleLZ_Compress
OodleLZ_Decompress

*/

void lz_test_1()
{
    OodleXLog_Printf_v0("lz_test_1\n");
    // allocate compressed buffer & decoded buffer of the correct sizes :

    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken,in_size);
    void * comp_buf = OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );

    void * dec_buf = OodleXMalloc( in_size );
    OOEX_ASSERT( dec_buf != NULL );
    
    //---------------------------------------------------
    
    // compress buffer -> buffer :
    
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_VeryFast;
    
    OO_SINTa comp_len = OodleLZ_Compress(OodleLZ_Compressor_Kraken,in_buffer,in_size,comp_buf,level);
    
    OodleXLog_Printf_v1("Kraken compress %d -> %d\n",(int)in_size,(int)comp_len);

    // decompress :

    OO_SINTa dec_len = OodleLZ_Decompress(comp_buf,comp_len,dec_buf,in_size,OodleLZ_FuzzSafe_Yes);

    OOEX_ASSERT_ALWAYS( dec_len == in_size );
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

    //---------------------------------------------------
    // do it again with another compressor, and custom options :
    
    // compress buffer -> buffer :
    
    OodleLZ_CompressOptions options = * OodleLZ_CompressOptions_GetDefault(OodleLZ_Compressor_Leviathan,level);
    options.spaceSpeedTradeoffBytes = OODLELZ_SPACESPEEDTRADEOFFBYTES_DEFAULT/2; // favor size over decode speed
    
    comp_len = OodleLZ_Compress(OodleLZ_Compressor_Leviathan,in_buffer,in_size,comp_buf,level,&options);
    
    OodleXLog_Printf_v1("Leviathan compress %d -> %d\n",(int)in_size,(int)comp_len);

    // decompress :

    dec_len = OodleLZ_Decompress(comp_buf,comp_len,dec_buf,in_size,OodleLZ_FuzzSafe_Yes);

    OOEX_ASSERT( dec_len == in_size );
    OOEX_ASSERT( memcmp(in_buffer,dec_buf,in_size) == 0 );

    //-------------------------------------
    // free buffers :

    OodleXFree(comp_buf);
    OodleXFree(dec_buf);
}

/*

 lz_test_2
    
example of using the OodleLZ_Async_ async helper functions (eg OodleXLZ_Decompress_Wide_Async)
this is the simple way to get the best performance

Use the seekChunkReset option on to get a seekable packed stream.

*/

void lz_test_2()
{
    OodleXLog_Printf_v0("lz_test_2\n");

    // allocate compressed buffer & decoded buffer of the correct sizes :

    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken,in_size);
    void * comp_buf = OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );

    void * dec_buf = OodleXMalloc( in_size );
    OOEX_ASSERT( dec_buf != NULL );
    
    //---------------------------------------------------
    
    OodleLZ_CompressOptions options = * OodleLZ_CompressOptions_GetDefault(OodleLZ_Compressor_Kraken,OodleLZ_CompressionLevel_Normal);
    // turn on seekChunkReset
    //  this makes chunks independent so they can be decompressed in any order (not just linear)
    options.seekChunkReset = true;
    options.seekChunkLen = OodleLZ_MakeSeekChunkLen(in_size,8);

    // with seekChunkReset on, compression will also go in parallel
    //  (actually compression can *always* run in parallel, but seekChunkReset makes it scale more linearly,
    //    and parallelize on a smaller granularity) 
    // use the OodleLZ_Compressor_Kraken compressor
    
    OodleXHandle h = OodleXLZ_Compress_Async(OodleXAsyncSelect_Full,OodleLZ_Compressor_Kraken,in_buffer,in_size,comp_buf,OodleLZ_CompressionLevel_Normal,&options);

    // ... do other game work while compression runs ...
    
    OO_SINTa comp_len = -1; 
    OodleXLZ_Compress_Wait_GetResult(h,&comp_len);

    OodleXLog_Printf_v1("LZ compress %d -> %d\n",(int)in_size,(int)comp_len);

    //-----------------------------------------------------
    // make seek entries :
    //  seek entries allow parallel decompression
    
    OodleLZ_SeekTable * seekTable = OodleLZ_CreateSeekTable(OodleLZSeekTable_Flags_None,options.seekChunkLen,in_buffer,in_size,comp_buf,comp_len);
    OOEX_ASSERT( seekTable != NULL );

    //-----------------------------------------------------

    OodleXHandle dh = OodleXLZ_Decompress_Wide_Async(OodleXAsyncSelect_Full,seekTable,comp_buf,comp_len,dec_buf,in_size,
                            OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,0,0,OodleLZ_PackedRawOverlap_No,0,0);
    
    // ... do other game work while decompression runs ...
    
    OodleXStatus st = OodleX_Wait(dh,OodleXHandleDeleteIfDone_Yes);
    OOEX_ASSERT_ALWAYS( st == OodleXStatus_Done );
    
    //-----------------------------------------------------
    // check :
        
    OOEX_ASSERT( memcmp(in_buffer,dec_buf,in_size) == 0 );

    //-------------------------------------
    // free buffers :

    OodleLZ_FreeSeekTable(seekTable);
    OodleXFree(comp_buf);
    OodleXFree(dec_buf);
}


/*

 test_4 :

example of seeking in packed stream
and firing per-chunk decompression tasks

Sort of like what OodleXLZ_Decompress_Wide_Async does internally.

*/

void lz_test_4()
{
    OodleXLog_Printf_v0("lz_test_4\n");
    // allocate compressed buffer & decoded buffer of the correct sizes :

    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken,in_size);
    void * comp_buf = OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );

    void * dec_buf = OodleXMalloc( in_size );
    OOEX_ASSERT( dec_buf != NULL );
    
    // set up compress options for independent seek chunks of the smallest allowed size :
    OodleLZ_CompressOptions lzOptions = * OodleLZ_CompressOptions_GetDefault(OodleLZ_Compressor_Kraken,OodleLZ_CompressionLevel_VeryFast);
    lzOptions.seekChunkReset = true;
    //lzOptions.seekChunkLen = OODLELZ_BLOCK_LEN;
    // make a seek chunk len to target 32 chunks :
    lzOptions.seekChunkLen = OodleLZ_MakeSeekChunkLen(in_size,32);
        
    //---------------------------------------------------
    
    // compress buffer -> buffer :
        
    OO_SINTa comp_len = OodleLZ_Compress(OodleLZ_Compressor_Kraken,in_buffer,in_size,comp_buf,OodleLZ_CompressionLevel_VeryFast,&lzOptions);
    
    OodleXLog_Printf_v1("LZ compress %d -> %d\n",(int)in_size,(int)comp_len);
    
    //---------------------------------------------------
    // decompress by seeking and firing async decodes
    
    OO_SINTa maxNumSeeks = (in_size + lzOptions.seekChunkLen-1)/lzOptions.seekChunkLen;
    
    OodleXHandle * handles = (OodleXHandle *) OodleXMalloc( sizeof(OodleXHandle) * maxNumSeeks );
    OOEX_ASSERT( handles != NULL );

    OO_S32 numHandles = 0;
    
    {   
    OO_SINTa dec_pos = 0;
    OO_U8 * comp_ptr = (OO_U8 *)comp_buf;
    OO_SINTa comp_avail = comp_len;

    while(dec_pos < in_size)    
    {
        OO_SINTa dec_chunk_len = OOEX_MIN(lzOptions.seekChunkLen, (in_size-dec_pos) );

        OodleXHandle h = OodleXLZ_Decompress_Narrow_Async(OodleXAsyncSelect_Full,comp_ptr,comp_avail,(OO_U8 *)dec_buf+dec_pos,dec_chunk_len);
        //  OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None, etc
        handles[numHandles++] = h;
        
        OO_SINTa seek_step = OodleLZ_GetCompressedStepForRawStep(comp_ptr,comp_avail,dec_pos,dec_chunk_len);
        
        comp_ptr += seek_step;
        dec_pos += dec_chunk_len;
        
        // wait on handles[numHandles-128] to prevent handle count exceeding handle table size :
        if ( numHandles >= 128 )
        {
            OodleXStatus st = OodleX_WaitAndDelete(handles[numHandles-128]);
            OOEX_ASSERT_ALWAYS( st == OodleXStatus_Done );
        }
    }
    
    }
    
    // ... do other game work while async decomps run ...
    
    OodleXStatus st = OodleX_WaitAll(handles,numHandles,OodleXHandleDeleteIfDone_Yes);
    OOEX_ASSERT_ALWAYS( st == OodleXStatus_Done );

    OodleXFree(handles);


    // check it's okay :
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

    //-------------------------------------
    // free buffers :

    OodleXFree(comp_buf);
    OodleXFree(dec_buf);
}


/*

test_9 :

Demonstrate separate block compression & decompression

OodleLZ blocks can be concatenated to form a single valid LZ data stream
That means you can just call OodleLZ_Compress on separate blocks and append the output,
then decode in one call.

OodleLZ blocks that were made from separate Compress calls will be independent
unless you specified dictionary backup in the encode, which makes them depend on previous
data.


The rules are :

1. OodleLZ Decompress can be called on invidual blocks (OODLELZ_BLOCK_LEN) if :

    they are seek-chunk-reset points,
    OR if they were made by separate OodleLZ Compress calls 
    OR if the compressor does not carry state across blocks (OodleLZ_Compressor_MustDecodeWithoutResets)

2. OodleLZ Decompress must get the same dictionary as OodleLZ Compress saw
    no previous dictionary is needed if it's a seek-chunk-reset point
    (the start of an OodleLZ_Compress call is always a seek reset point,
    if no dictionary backup is provided to the encoder) 

*/

void lz_test_9()
{
    OodleXLog_Printf_v0("lz_test_9\n");

    //---------------------------------------------------
    // split the buffer into two pieces
    //  such that the split point is a valid seek chunk point :
    OO_SINTa block_size = OodleLZ_MakeSeekChunkLen(in_size,2);

    if ( block_size >= in_size )
    {
        // too small to split at seek chunk
        return;
    }
    
    char * in1 = (char *)in_buffer;
    char * in2 = in1 + block_size;
    OO_SINTa len1 = block_size;
    OO_SINTa len2 = in_size - len1;
    
    OodleXLog_Printf_v1("Chunks : %d + %d\n",(int)len1,(int)len2);

    //---------------------------------------------------
    
    // allocate compressed buffer & decoded buffer of the correct sizes :

    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken,in_size);
    void * comp_buf = OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );

    void * dec_buf = OodleXMalloc( in_size );
    OOEX_ASSERT( dec_buf != NULL );
    
    //---------------------------------------------------

    OodleLZ_Compressor compressor = OodleLZ_Compressor_Kraken;
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Fast; 
    OodleLZ_CompressOptions options = * OodleLZ_CompressOptions_GetDefault(compressor,level);
    
    // options does NOT have seek resets by default

    //-----------------------------
    // compress as one part :
    {

    OO_SINTa comp_len = OodleLZ_Compress(compressor,in_buffer,in_size,comp_buf,level,&options);

    OodleXLog_Printf_v1("Whole buffer compress : %d -> %d\n",(int)in_size,(int)comp_len);   

    // normal one part decompression :
    memset(dec_buf,0xEE,in_size);
    OodleLZ_Decompress(comp_buf,comp_len,dec_buf,in_size,OodleLZ_FuzzSafe_Yes);
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

    OO_U8 * comp_end = (OO_U8 *)comp_buf + comp_len;

    //===================================================================

    // can decode in two calls with the full dictionary, but only for compressors that don't carry state :
    if ( ! OodleLZ_Compressor_MustDecodeWithoutResets(compressor) )
    {
        //-------------------------------------------------------------
        // decode as two parts : (len1,len2) :
        memset(dec_buf,0xEE,in_size);

        OO_SINTa dec_comp_len1 = OodleLZ_GetCompressedStepForRawStep(comp_buf,comp_len,0,len1);
                
        OodleLZ_Decompress(comp_buf,comp_len,dec_buf,len1,OodleLZ_FuzzSafe_Yes);

        // decompress second part with dictionary base :

        OodleLZ_Decompress((char *)comp_buf+dec_comp_len1,comp_len-dec_comp_len1,(char *)dec_buf+len1,len2,
                                    OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,dec_buf,in_size);

        OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

        //-------------------------------------------------------------
        // can also decode block by block :
        memset(dec_buf,0xEE,in_size);
        
        // scan comp_ptr through blocks :
        OO_U8 * comp_ptr = (OO_U8 *)comp_buf;
        for(OO_SINTa block_pos=0;block_pos < in_size;block_pos += OODLELZ_BLOCK_LEN)
        {
            OO_SINTa block_len = OOEX_MIN(OODLELZ_BLOCK_LEN, (in_size - block_pos) );
            
            OO_SINTa block_comp_len = OodleLZ_GetCompressedStepForRawStep(comp_ptr,comp_end-comp_ptr,0,block_len);
            
            // decode current block, with window set to whole buffer :
            OO_SINTa got_pos = OodleLZ_Decompress(comp_ptr,block_comp_len,(char *)dec_buf+block_pos,block_len,
                                    OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,dec_buf,in_size);
            
            OOEX_ASSERT_ALWAYS( got_pos == block_pos+block_len );
            
            comp_ptr += block_comp_len;
        }

        OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );
        //-------------------------------------------------------------
    }
    
    }
    //-----------------------------
    // two part compression with overlap :
    // two compress calls, but using the full window, so decompression must use full window as well
    {
    
    OO_SINTa comp_len1 = OodleLZ_Compress(compressor,in1,len1,comp_buf,level,&options,in_buffer);
    OO_SINTa comp_len2 = OodleLZ_Compress(compressor,in2,len2,(char *)comp_buf + comp_len1,level,&options,in_buffer);
    OO_SINTa comp_len = comp_len1 + comp_len2;

    OodleXLog_Printf_v1("Two part compress with overlap : %d -> %d\n",(int)in_size,(int)comp_len);  

    // must decode whole buffer, but can do it in two calls :

    // you can always just do a whole buffer decode here :
    memset(dec_buf,0xEE,in_size);
    OodleLZ_Decompress(comp_buf,comp_len,dec_buf,in_size,OodleLZ_FuzzSafe_Yes);
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

    // or incremental, but with the whole dictionary :
    memset(dec_buf,0xEE,in_size);

    OO_SINTa dec_comp_len1 = OodleLZ_GetCompressedStepForRawStep(comp_buf,comp_len,0,len1);
        
    OodleLZ_Decompress(comp_buf,comp_len,dec_buf,len1,OodleLZ_FuzzSafe_Yes);
        
    OodleLZ_Decompress((char *)comp_buf+dec_comp_len1,comp_len-dec_comp_len1,(char *)dec_buf+len1,len2,
                                OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,dec_buf,in_size);

    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );
    
    }
    //-----------------------------
    // two part no overlap :
    // second compress doesn't use earlier dictionary here
    // so decompression can be done in two pieces with no overlap
    {
    
    OO_SINTa comp_len1 = OodleLZ_Compress(compressor,in1,len1,comp_buf,level,&options);
    OO_SINTa comp_len2 = OodleLZ_Compress(compressor,in2,len2,(char *)comp_buf + comp_len1,level,&options);
    OO_SINTa comp_len = comp_len1 + comp_len2;

    OodleXLog_Printf_v1("Two part compress no overlap : %d -> %d\n",(int)in_size,(int)comp_len); 

    // can decode in two parts :

    // you can always just do a whole buffer decode here :
    memset(dec_buf,0xEE,in_size);
    OodleLZ_Decompress(comp_buf,comp_len,dec_buf,in_size,OodleLZ_FuzzSafe_Yes);
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

    // or incremental :
    memset(dec_buf,0xEE,in_size);
    
    OO_SINTa dec_comp_len1 = OodleLZ_GetCompressedStepForRawStep(comp_buf,comp_len,0,len1);

    // no dictionary backup needed
    // decode in reverse order to simulate random access :

    OodleLZ_Decompress((char *)comp_buf+dec_comp_len1,comp_len-dec_comp_len1,(char *)dec_buf+len1,len2,OodleLZ_FuzzSafe_Yes);
        
    OodleLZ_Decompress(comp_buf,comp_len,dec_buf,len1,OodleLZ_FuzzSafe_Yes);

    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );
    
    }
    //-----------------------------
    // two part no overlap via seek reset :
    // seek reset system is equivalent to splitting Compress calls like the above
    {   
    options.seekChunkReset = true;
    options.seekChunkLen = (OO_S32)block_size;
    
    OodleXLog_Printf_v1("seekChunkLen : %d\n",options.seekChunkLen);

    OO_SINTa comp_len = OodleLZ_Compress(compressor,in_buffer,in_size,comp_buf,level,&options);

    OodleXLog_Printf_v1("Whole buffer compress seek reset : %d -> %d\n",(int)in_size,(int)comp_len); 

    // can decode in two parts :

    // you can always just do a whole buffer decode here :
    memset(dec_buf,0xEE,in_size);

    OodleXLog_Printf_v1("one part : \n");

    OodleLZ_Decompress(comp_buf,comp_len,dec_buf,in_size,OodleLZ_FuzzSafe_Yes);
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

    // or incremental :
    memset(dec_buf,0xEE,in_size);
    
    OodleXLog_Printf_v1("two part : \n");

    OO_SINTa dec_comp_len1 = OodleLZ_GetCompressedStepForRawStep(comp_buf,comp_len,0,len1);
    
    OodleXLog_Printf_v1("dec_comp_len1 = %d\n",(int)dec_comp_len1);

    // no dictionary backup needed
    // decode in reverse order to simulate random access :

    OodleLZ_Decompress((char *)comp_buf+dec_comp_len1,comp_len-dec_comp_len1,(char *)dec_buf+len1,len2,OodleLZ_FuzzSafe_Yes);

    OodleLZ_Decompress(comp_buf,comp_len,dec_buf,len1,OodleLZ_FuzzSafe_Yes);

    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );
    }
    
    //=============================================

    OodleXFree(dec_buf);
    OodleXFree(comp_buf);
}


/*

lz_test_10 :

example of using the incremental/streaming decoder

OodleLZDecoder_Create, etc.

this example shows decoding *from* a limited window

outputs into a single buffer

this example simulates using a limited IO buffer for compressed data

it decodes quanta from the available compressed data

*/


void lz_test_10()
{
    OodleXLog_Printf_v0("lz_test_10\n");
    // allocate compressed buffer & decoded buffer of the correct sizes :

    OodleLZ_Compressor compressor = OodleLZ_Compressor_Kraken;
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Fast;
    
    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken,in_size);
    OO_U8 * comp_buf = (OO_U8 *) OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );

    OO_U8 * dec_buf = (OO_U8 *)OodleXMalloc( in_size );
    OOEX_ASSERT( dec_buf != NULL );
    
    //---------------------------------------------------
    
    // compress buffer -> buffer :
    
    OO_SINTa comp_len = OodleLZ_Compress(compressor,in_buffer,in_size,comp_buf,level);
    
    OodleXLog_Printf_v1("LZ compress %d -> %d\n",(int)in_size,(int)comp_len);

    //---------------------------------------------------
    // decompress with incremental streaming decoder :
    
    // we're now going to pretend that "comp_buf" is in a file
    //  and we can't read the whole thing
    
    // 64k IO buffer to stress the code :
    //  obviously you would use much larger
    //  must be at least enough for 1 whole compressed quantum (OODLELZ_BLOCK_MAX_COMPLEN = 256k + 2)
    //  (for Kraken / whole-block compressors you probably want 512k io_buffer minimum)
    const OO_SINTa io_buffer_size = OODLELZ_BLOCK_LEN*2;
    OO_U8 * io_buffer = (OO_U8 *) OodleXMalloc_IOAligned(io_buffer_size);
    OOEX_ASSERT( io_buffer != NULL );
    
    OO_SINTa io_buffer_avail = 0; // starts empty
    
    OO_SINTa comp_file_io_pos = 0; // simulated compressed file next read pos
    
    {
    
    // make the Decoder object using on-stack memory :
    //OO_S32 memSize = OodleLZDecoder_MemorySizeNeeded(compressor,in_size);
    
    OodleLZDecoder * decoder = OodleLZDecoder_Create(compressor,in_size,NULL,0);

    OO_SINTa io_buffer_pos = 0;
    OO_U8 * dec_buf_ptr = (OO_U8 *) dec_buf;
    OO_U8 * dec_buf_end = (OO_U8 *) dec_buf + in_size;;

    while(dec_buf_ptr<dec_buf_end)
    {
        // see if we can do a "read" into the io_buffer :
        if ( comp_file_io_pos < comp_len )
        {
            // don't bother with an IO unless I have some minimum amount of room :
            OO_SINTa min_io_size = 16*1024;
            if ( (io_buffer_size - io_buffer_avail) > min_io_size )
            {
                OO_SINTa io_size = OOEX_MIN( (io_buffer_size - io_buffer_avail), (comp_len - comp_file_io_pos) );
                // stress - limit IO size :
                //io_size = OOEX_MIN(io_size,64*1024);
                
                //OodleXLog_Printf_v1("IO read : %d at %d\n",(int)io_size,(int)comp_file_io_pos);
                
                // IO read :
                memcpy(io_buffer+io_buffer_avail,comp_buf+comp_file_io_pos,io_size);
                comp_file_io_pos += io_size;
                io_buffer_avail += io_size;
            }
        }
    
        // ask the Decoder for a partial decode :
        OodleLZ_DecodeSome_Out out;
        
        OO_BOOL ok = OodleLZDecoder_DecodeSome(decoder,&out,
            dec_buf,(dec_buf_ptr - dec_buf),in_size,(dec_buf_end - dec_buf_ptr),
            io_buffer+io_buffer_pos,io_buffer_avail-io_buffer_pos);

        // real usage should check error return conditions
        OOEX_ASSERT_ALWAYS( ok );

        OO_S32 decoded = out.decodedCount;
        OO_S32 comp_used = out.compBufUsed;
                                
        // advance the decoder :
        dec_buf_ptr += decoded;
        io_buffer_pos += comp_used;
        
        OOEX_ASSERT( out.curQuantumCompLen < io_buffer_size );
        
        //OodleXLog_Printf_v1("decoded : %d using %d\n",decoded,comp_used);
        
        if ( decoded == 0 )
        {
            // couldn't decode anything
            // this should only happen because we're near the end of the io buffer
            // and don't have enough compressed data to do antyhing
            OOEX_ASSERT( io_buffer_pos > 0 );
            // slide down the io buffer so it can refill
            OO_SINTa io_buffer_keep = io_buffer_avail - io_buffer_pos;
            memmove(io_buffer,io_buffer+io_buffer_pos,io_buffer_keep);
            io_buffer_pos = 0;
            io_buffer_avail = io_buffer_keep;
        }
    }

    OOEX_ASSERT( comp_file_io_pos == comp_len );

    OodleLZDecoder_Destroy(decoder);

    }
    
    // check it's okay :
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_buf,in_size) == 0 );

    //-------------------------------------
    // free buffers :

    OodleXFree(comp_buf);
    OodleXFree(dec_buf);
    OodleXFree_IOAligned(io_buffer);
}


/*

lz_test_11 :

example of using the incremental/streaming decoder

OodleLZDecoder_Create, etc.

this example simulates using a limited IO buffer for compressed data (like lz_test_10)

Kraken does not currently have a true "sliding window" decoder; it can't wrap around a circular
window.  This example shows how to simulate a sliding window with the Kraken decoder by sliding down chunks.

It decodes 256k at a time into a 512k window.  It decodes into the second half of the window, with
the first window filled by the previous decode.  After each decode, it memcopies down the data to be used
as dictionary for the next block.

---------

In general this method should not be used if you can just decode directly into the output buffer.
That's always the best way if possible.

One case where you might want to use this is if your output buffer is in non-cached graphics memory.

---------

The simpler alternative to this is just to reset every 256k block, so there's no dictionary overlap.
eg. just set :

options.seekChunkReset = true;
options.seekChunkLen = OODLELZ_BLOCK_LEN;

then you can use a 256k decode output window and don't need to memcpy to slide down the dictionary.
The disadvantage of resetting is just lower compression.

*/


void lz_test_11()
{
    OodleXLog_Printf_v0("lz_test_11\n");

    //*
    // fast encoder:
    OodleLZ_Compressor compressor = OodleLZ_Compressor_Kraken;
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_VeryFast;
    /*/
    // slowest encoder:
    OodleLZ_Compressor compressor = OodleLZ_Compressor_Leviathan;
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Optimal5;
    /**/

    //---------------------------------------------------
    
    /*
    // minimum size :
    // we will decode 256k (one "block") at a time
    // +256k more for dictionary references to preceding data
    
    OO_S32 decode_window_size = 2*OODLELZ_BLOCK_LEN; // OODLELZ_BLOCK_LEN = 256k
    OO_S32 dictionary_size = OODLELZ_BLOCK_LEN;
    /*/
    // more reasonable size :
    // dictionary limit 2M
    // decode in a 3M window, so we do a memcpy after every 1M streamed
    
    OO_S32 decode_window_size = 3*1024*1024; // OODLELZ_BLOCK_LEN = 256k
    OO_S32 dictionary_size = 2*1024*1024;
    /**/
    
    OodleXLog_Printf_v1("dictionary_size : %d, decode_window_size : %d\n",dictionary_size,decode_window_size);
    
    //---------------------------------------------------
    
    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(compressor,in_size);
    OO_U8 * comp_buf = (OO_U8 *) OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );

    // dec_window is our scratch circular window
    OO_U8 * dec_window = (OO_U8 *)OodleXMalloc( decode_window_size );
    OOEX_ASSERT( dec_window != NULL );
    
    // dec_out_buf is the final output location (perhaps uncached graphics memory)
    OO_U8 * dec_out_buf = (OO_U8 *)OodleXMalloc( in_size );
    OOEX_ASSERT( dec_out_buf != NULL );
    
    // decoderMem is used for the OodleLZ decoder object
    OO_S32 memSize = OodleLZDecoder_MemorySizeNeeded(compressor);
    OO_U8 * decoderMem = (OO_U8 *) OodleXMalloc(memSize);
    
    //---------------------------------------------------
    // compress buffer -> buffer :
    
    // limit dictionarySize so matches can't go out of the decode window :
    OodleLZ_CompressOptions options = * OodleLZ_CompressOptions_GetDefault(compressor,level);
    options.dictionarySize = dictionary_size;
    
    OO_SINTa comp_len = OodleLZ_Compress(compressor,in_buffer,in_size,comp_buf,level,&options);
    
    OodleXLog_Printf_v1("LZ compress %d -> %d\n",(int)in_size,(int)comp_len);

    //---------------------------------------------------
    // decompress with incremental streaming decoder :
    
    // we're now going to pretend that "comp_buf" is in a file
    //  and we can't read the whole thing
    
    // IO buffer must be at least enough for 1 whole quantum (256k + a little bit)
    //  Kraken uses "large block quantum" (256k) not the old 16k quantum
    const OO_SINTa io_buffer_size = (256+63)*1024;
    OO_U8 io_buffer[io_buffer_size];
    
    OO_SINTa io_buffer_avail = 0; // starts empty
    
    OO_SINTa comp_file_io_pos = 0; // simulated compressed file next read pos
    
    {   
    OodleLZDecoder * decoder = OodleLZDecoder_Create(compressor,in_size,decoderMem,memSize);

    OO_SINTa io_buffer_pos = 0;
    OO_U8 * dec_out_ptr = (OO_U8 *) dec_out_buf;
    OO_U8 * dec_out_end = (OO_U8 *) dec_out_buf + in_size;;

    OO_SINTa dec_window_pos = 0;

    while(dec_out_ptr<dec_out_end)
    {
        // see if we can do a "read" into the io_buffer :
        if ( comp_file_io_pos < comp_len )
        {
            OO_SINTa min_io_size = 16*1024;
            if ( (io_buffer_size - io_buffer_avail) > min_io_size )
            {
                OO_SINTa io_size = OOEX_MIN( (io_buffer_size - io_buffer_avail), (comp_len - comp_file_io_pos) );
                
                //OodleXLog_Printf_v1("IO read : %d at %d\n",(int)io_size,(int)comp_file_io_pos);
                
                // IO read :
                memcpy(io_buffer+io_buffer_avail,comp_buf+comp_file_io_pos,io_size);
                comp_file_io_pos += io_size;
                io_buffer_avail += io_size;
            }
        }
    
        // when dec_window_pos reaches end of window :
        if ( (dec_window_pos + OODLELZ_BLOCK_LEN) > decode_window_size )
        {
            OodleXLog_Printf_v1("slide!\n");
        
            // slide down the dictionary for the next block :
            memmove(dec_window,dec_window + dec_window_pos - dictionary_size,dictionary_size);
            dec_window_pos = dictionary_size;
        }
            
        OodleXLog_Printf_v1("decode : at %d in window, %d in output\n",(int)dec_window_pos,(int)(dec_out_ptr - dec_out_buf));
    
        // ask the Decoder for a partial decode :
        OodleLZ_DecodeSome_Out out;
        
        // no need to truncate dec_avail at the end, the "in_size" passed to LZDecoder_Create does this
        //OO_SINTa dec_avail = OOEX_MIN(dec_out_remain,decode_window_size - dec_window_pos);        
        OO_SINTa dec_avail = decode_window_size - dec_window_pos;
        
        OO_BOOL ok = OodleLZDecoder_DecodeSome(decoder,&out,
            dec_window,dec_window_pos,
            in_size, //decode_window_size, // !!
            dec_avail,
            io_buffer+io_buffer_pos,io_buffer_avail-io_buffer_pos,
            OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No);

        // !! = this is a bit funny; we lie about the window size here
        //  if Kraken had a true sliding window decoder (like eg. LZH or LZB16 does)
        //  then you would pass decode_window_size here and let it do the wrapping
        //  but Kraken does not, so we pretend that we are decoding the whole file
        //  so that OodleLZDecoder_DecodeSome doesn't try to use its sliding window path
        //  (which would fail)
        //  the "dec_avail" value prevents DecodeSome from going out of the window
        //  and we simulate the sliding using the memcpy

        // real usage should check error return conditions
        OOEX_ASSERT_ALWAYS( ok );

        OO_S32 decoded = out.decodedCount;
        OO_S32 comp_used = out.compBufUsed;
        
        io_buffer_pos += comp_used;
        
        OOEX_ASSERT( out.curQuantumCompLen < io_buffer_size );
                
        //OodleXLog_Printf_v1("decoded : %d using %d\n",decoded,comp_used);
        
        if ( decoded == 0 )
        {
            // couldn't decode anything
            // this should only happen because we're near the end of the io buffer
            // and don't have enough compressed data to do antyhing
            OOEX_ASSERT( io_buffer_pos > 0 );
            // slide down the io buffer so it can refill
            OO_SINTa io_buffer_keep = io_buffer_avail - io_buffer_pos;
            memmove(io_buffer,io_buffer+io_buffer_pos,io_buffer_keep);
            io_buffer_pos = 0;
            io_buffer_avail = io_buffer_keep;
        }
        else
        {
            // copy out the decoded data :
            OO_U8 * dec_window_ptr = dec_window + dec_window_pos;
            // dec_out_ptr is the final output memory ; eg. perhaps uncached graphics memory
            // "decoded" is always OODLELZ_BLOCK_LEN unless we hit EOF
            memcpy(dec_out_ptr,dec_window_ptr,decoded);
            
            // advance the decoder :
            dec_out_ptr += decoded;
            dec_window_pos += decoded;
        }
    }

    OOEX_ASSERT( comp_file_io_pos == comp_len );
    OOEX_ASSERT( dec_out_ptr == dec_out_end );

    OodleLZDecoder_Destroy(decoder);
    }
    
    // check it's okay :
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,dec_out_buf,in_size) == 0 );

    //-------------------------------------
    // free buffers :

    OodleXFree(decoderMem);
    OodleXFree(comp_buf);
    OodleXFree(dec_out_buf);
    OodleXFree(dec_window);
}


/*

lz_test_12 :

example of directly calling the simple buffer->buffer compression API's using an "in place" buffer

OodleLZ_Compress
OodleLZ_Decompress
OodleLZ_GetInPlaceDecodeBufferSize

*/

void lz_test_12()
{
    OodleXLog_Printf_v0("lz_test_12\n");
    // allocate compressed buffer & decoded buffer of the correct sizes :

    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken,in_size);
    void * comp_buf = OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );
    
    //---------------------------------------------------
    
    // compress buffer -> buffer :
    
    OO_SINTa comp_len;
    
    {
    comp_len = OodleLZ_Compress(OodleLZ_Compressor_Kraken,in_buffer,in_size,comp_buf,OodleLZ_CompressionLevel_Fast);
    }
    
    //---------------------------------------------------

    OO_SINTa inplace_size = OodleLZ_GetInPlaceDecodeBufferSize(OodleLZ_Compressor_Kraken,comp_len,in_size);

    OodleXLog_Printf_v1("Kraken compress %d -> %d ; inplace_size = %d , padding = %d\n",
        (int)in_size,(int)comp_len,
        (int)inplace_size,(int)(inplace_size - in_size));

    void * inplace_buf = OodleXMalloc( inplace_size );
    OOEX_ASSERT( inplace_buf != NULL );

    // in game use, you load the compressed data into the *end* of the inplace buffer 
    // simulate the loading by doing a memcpy :
    
    char * inplace_comp_ptr = (char *)inplace_buf;
    inplace_comp_ptr += inplace_size - comp_len;
    
    memcpy(inplace_comp_ptr,comp_buf,comp_len);

    //---------------------------------------------------
        
    // decompress :
    // note the source (inplace_comp_ptr) and dest (inplace_buf) overlap
    //  - the compressed data at inplace_comp_ptr is destroyed by this call

    OO_SINTa dec_len = OodleLZ_Decompress(inplace_comp_ptr,comp_len,inplace_buf,in_size,OodleLZ_FuzzSafe_Yes);

    OOEX_ASSERT_ALWAYS( dec_len == in_size );
    OOEX_ASSERT_ALWAYS( memcmp(in_buffer,inplace_buf,in_size) == 0 );

    //---------------------------------------------------
    
    // free buffers :

    OodleXFree(comp_buf);
    OodleXFree(inplace_buf);
}


//=================================================

/*

lz_test_13 :

example of dictionary-relative compression

This is a technique in which you train a dictionary offline based on typical data, then for
each packet you wish to compress, the dictionary can be used a reference to improve compression ratio.

Oodle can do dictionary relative compression by putting the packet buffer to compress
in a contiguous buffer immediately following the dictionary.

Then simply use memcpy to move the active packet to the desired memory location.

NOTE that the work space for {dictionary + packet} must be allocated per thread, or mutex controlled
(it cannot be shared by simultaneously decoding threads)

For small packets (under 4 KB or so) such as network packets, consider Oodle Network instead.

For large buffers (over 128 KB or so), dictionary-relative compression doesn't help much and isn't recommended.

dictionary-relative compression is most typically useful on data in the 4 - 128 KB range.

*/

void lz_test_13()
{
    OodleXLog_Printf_v0("lz_test_13\n");
    
    //---------------------------------------------------
    
    // pretend that "in_buffer" consists of a trained dictionary + a packet to compress
    
    void * dictionary = in_buffer;
    OO_SINTa dictionary_size = (in_size * 2) /3;
    // dictionary_size must be a multiple of OODLELZ_BLOCK_LEN :
    dictionary_size &= ~(OODLELZ_BLOCK_LEN-1);
    
    void * packet1 = (char *)dictionary + dictionary_size;
    OO_SINTa packet1_size = in_size / 4;
    
    void * packet2 = (char *)packet1 + packet1_size;
    OO_SINTa packet2_size = in_size - packet1_size - dictionary_size;
    
    OodleXLog_Printf_v1("dictionary_size : %d ; packets : %d + %d\n",dictionary_size,packet1_size,packet2_size);
    
    //---------------------------------------------------
    // allocate compressed buffer & decoded buffer of the correct sizes :

    OO_SINTa max_packet_size = OOEX_MAX(packet1_size,packet2_size);

    // room for dictionary + a packet following :
    void * dictionary_and_packet_buf = OodleXMalloc( dictionary_size + max_packet_size );
    OOEX_ASSERT( dictionary_and_packet_buf != NULL );

    // comp buf just for a packet :
    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken, max_packet_size );
    void * comp_buf = OodleXMalloc( comp_buf_size );
    OOEX_ASSERT( comp_buf != NULL );

    // room for dictionary + a packet following :
    void * dec_buf = OodleXMalloc( dictionary_size + max_packet_size );
    OOEX_ASSERT( dec_buf != NULL );
    
    //---------------------------------------------------
    
    void * packets[2] = { packet1, packet2 };
    OO_SINTa packet_sizes[2] = { packet1_size, packet2_size };

    // setup work that's done in advance :

    // put dictionary at head of dictionary_and_packet_buf (for encoder) :
    memcpy(dictionary_and_packet_buf,dictionary,dictionary_size);
    void * after_dictionary_ptr = (char *)dictionary_and_packet_buf + dictionary_size;
    
    // preload dictionary at head of dec_buf (for decoder) :
    memcpy(dec_buf,dictionary,dictionary_size);
    
    for(int packet_i=0;packet_i<2;packet_i++)
    {
        // work that's done per packet :
    
        void * packet_ptr = packets[packet_i];
        OO_SINTa packet_size = packet_sizes[packet_i];
    
        // compress packet to comp_buf
        // preload with dictionary
        
        // copy packet to be immediately following dictionary :
        memcpy(after_dictionary_ptr,packet_ptr,packet_size);
        
        OO_SINTa comp_len = OodleLZ_Compress(OodleLZ_Compressor_Kraken,after_dictionary_ptr,packet_size,comp_buf,OodleLZ_CompressionLevel_Fast,NULL,dictionary_and_packet_buf);
        
        OodleXLog_Printf_v1("Kraken compress %d -> %d\n",(int)packet_size,(int)comp_len);

        // decompress :
        // decode into buffer containing dictionary, immediately following dictionary :

        void * dec_packet_ptr = (char *)dec_buf + dictionary_size;

        OO_SINTa dec_len = OodleLZ_Decompress(comp_buf,comp_len,dec_packet_ptr,packet_size,OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,dec_buf);

        OOEX_ASSERT_ALWAYS( dec_len == packet_size + dictionary_size );
        OOEX_ASSERT_ALWAYS( memcmp(packet_ptr,dec_packet_ptr,packet_size) == 0 );
        
        // if you need the decoded packet to be in another memory location, memcpy it there now
    }
    
    //---------------------------------------------------
    // free buffers :

    OodleXFree(dictionary_and_packet_buf);
    OodleXFree(comp_buf);
    OodleXFree(dec_buf);
}
