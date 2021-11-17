//idoc(parent,examples)
//idoc(tutorial,example_lz_overlap,example_lz_overlap : Example demonstrating parallel overlap with OodleLZ )

/*

Oodle example_lz_overlap

Demonstration of the benefit of overlapping IO with CPU work and parallelism in LZ decompression.

This example compresses a file, then repeatedly reads the compressed data and decompresses it, in several different ways.

There are two types of parallelism demonstrated here :

1. IO overlap.  When reading and decompressing large files, you get minimum latency by
overlapping the IO with the decompress.  This is done by reading the compressed data in smaller
chunks and decompressing each chunk (in parallel) as it is done.

2. Parallel "wide" decompression.  OodleLZ with seek chunk resets can decompress using many
threads simultaneously.

Combining IO overlap and wide decompression is the fastest way to load compressed data.

*/

#include "../include/oodle2x.h"
#include "ooex.h"

#ifdef _MSC_VER
#pragma warning(disable : 4127) // conditional is constant
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "make_example_input.h"

#ifdef BUILDING_EXAMPLE_CALLER
#define main example_lz_overlap
#endif

extern "C" int main(int argc,char *argv[])
{
    if ( ! OodleX_Init_Default(OODLE_HEADER_VERSION) )
    //if ( ! OodleX_Init_Default(OODLE_HEADER_VERSION,OodleX_Init_GetDefaults_DebugSystems_Yes,OodleX_Init_GetDefaults_Threads_No) )
    {
        fprintf(stderr,"OodleX_Init failed.\n");
        return 10;
    }

/*

    Pass in the file name to compress.  If you don't provide one, I'll make one.
    
    Ideally give this example something big to work on, 20M - 100M , to get better charts.

*/

    // get args :
    const char * inName;
    if ( argc < 2 )
    {
        inName = "oodle_example_input_file";
        make_example_input(inName);
    }
    else
    {
        inName = argv[1];
    }

    // we'll write to this file :
    const char * compName = "oodle_example_overlap_comp";
     
    OodleXLog_Printf_v1("compressing %s -> %s ...\n",inName,compName);

/*
    
    Set up the LZ compression options so that we make independent chunks
    (seekChunkReset = true) and make them 1 MB each.
    
    This hurts compression ratio (vs not chunking) but allows decompression of each chunk
    to run in parallel.

*/
    
    OodleLZ_CompressionLevel lzLevel = OodleLZ_CompressionLevel_Fast;
    OodleLZ_Compressor lzCompressor = OodleLZ_Compressor_Kraken;
    OodleLZ_CompressOptions lzOptions = * OodleLZ_CompressOptions_GetDefault(lzCompressor,lzLevel);

    lzOptions.seekChunkLen = 1<<20; // 1 MB
    lzOptions.seekChunkReset = true;

/*

    Read the input file, compress it, and write it out :

    In practice for maximum speed, you could read each chunk of the input and compress them
    independently as each chunk async IO read finishes.

*/

    OO_SINTa inSize;
    void * inBuf;
    OO_SINTa compSize;
    OodleLZ_SeekTable * seekTable;
    OO_S64 inSize64;
    
    inBuf = OodleXIOQ_ReadMallocWholeFile_AsyncAndWait(inName,&inSize64);
    if ( inBuf == NULL )
    {
        OodleXLog_Printf_v0("ERROR couldn't load : %s\n",inName);
        return 10;
    }

    inSize = OodleX_S64_to_SINTa_check(inSize64);

    { // scope for compBuf
        void * compBuf = OodleXMalloc_IOAligned( OodleLZ_GetCompressedBufferSizeNeeded(lzCompressor,inSize) );
        
        OO_U32 asyncSelect = OodleXAsyncSelect_All;
        compSize = OodleXLZ_Compress_AsyncAndWait(asyncSelect,lzCompressor,inBuf,inSize,compBuf,lzLevel,&lzOptions);
        
        if ( compSize <= 0 )
        {
            OodleXLog_Printf_v0("ERROR failed to compress\n");
            return 10;
        }
        
        if ( ! OodleXIOQ_WriteWholeFile_AsyncAndWait(compName,compBuf,compSize) )
        {
            OodleXLog_Printf_v0("ERROR couldn't write : %s\n",compName);
            return 10;
        }
        
        // log about it :
        OodleXLog_Printf_v1("%s compressed %d -> %d\n",inName,(int)inSize,(int)compSize);

    /*

        Make an OodleLZ_SeekTable on the compressed data for later use with parallel decompression.
        
        You should store the seektable to disk with any header information, and load it before loading the
        compressed bulk data.  Oodle can do this for you with an "OOZ" file.
        
        For this example we'll just keep the seekTable in memory.  In real use you would write the
        seek table with the compressed data.

        Once we make the seekTable, we free compBuf - to get the compressed data again we will
        have to read it from disk.

    */
        
        seekTable = OodleLZ_CreateSeekTable(OodleLZSeekTable_Flags_None,
                                                        lzOptions.seekChunkLen,NULL,inSize,compBuf,compSize);
        
        OodleXFree_IOAligned(compBuf);
        compBuf = NULL;
    }
    
    // we'll decompress into decompBuf
    void * decompBuf = OodleXMalloc(inSize);
    
    //===================================================================================

    // prevent cheating :
    memset(decompBuf,(int)rand(),inSize);

/*

    Read the files with unbuffered IO
    
    We're mainly doing this here for consistent benchmarking so we can see IO times
    otherwise the files would just come from the OS buffers and no show real IO time.
    In practice you should usually use buffered IO. (OodleXFileOpenFlags_Default)

*/

    OodleXFileOpenFlags fileOpenFlags = OodleXFileOpenFlags_NotBuffered;

/*

    Now begins various ways to read the compressed file and decompress it.

    Read-Decomp 1 :
    
    Read the whole compressed file, synchronously.
    When that's done, decompress the whole buffer, synchronously.
    
    Simple, but stalls the main thread and gets no overlap of IO or parallelism in the decompress.

*/
    
    OodleXLog_Printf_v1("Doing read then decomp, synchronously on the main thread :\n");
    
    {
    void * compBuf = OodleXIOQ_ReadMallocWholeFile_AsyncAndWait(compName,NULL,fileOpenFlags);
    if ( compBuf == NULL )
    {
        OodleXLog_Printf_v0("ReadMallocWholeFile failed on %s\n",compName);
        return 10;
    }
    
    OodleLZ_Decompress(compBuf,compSize,decompBuf,inSize,OodleLZ_FuzzSafe_Yes);
    
    // check it :
    OOEX_ASSERT( memcmp(inBuf,decompBuf,inSize) == 0 );
    
    OodleXFree_IOAligned(compBuf);
    }
    
    //===================================================================================
    
/*

    Read-Decomp 2 :
    
    Read the whole compressed file, and fire off a full-buffer decompress, scheduled
    to run automatically when the read is done.

    This has the same latency as method 1, but is a single async operation so the main
    thread can do something else the whole time.

*/

    OodleXLog_Printf_v1("Doing read then decomp, through a job chain :\n");
    
    // prevent cheating :
    memset(decompBuf,(int)rand(),inSize);
    
    {
    
    // for simplicity, we'll just malloc compBuf with our known compSize
    // more generally if you didn't know compSize, you would have to use an OodleWork coroutine
    //  to first open the file, get the size, do the malloc, do the read, then the decompress
    void * compBuf = OodleXMalloc_IOAligned(compSize);

    // make an IO request to open and read the whole file : 
    OodleXIOQFile compFile;
    OodleXHandle openAndReadH = OodleXIOQ_OpenAndRead_Async(&compFile,compName,compBuf,OodleX_IOAlignUpSINTa(compSize),0,fileOpenFlags,0,OodleXHandleAutoDelete_Yes);

    // go ahead and enqueue a Close to follow the OpenAndRead :
    OodleXIOQ_CloseFile_Async(compFile,OODLEX_FILE_CLOSE_NO_TRUNCATE_SIZE,OodleXHandleAutoDelete_Yes);

    // the decompress depends on the OpenAndRead - it will run only when that is done
    //  dependencies are an array, but we have only one, so just point at it :
    const OodleXHandle * deps = &openAndReadH;
    int num_deps = 1;
    
    OodleXHandle decompH = OodleXLZ_Decompress_Narrow_Async(OodleXAsyncSelect_Full,compBuf,compSize,decompBuf,inSize,
                                        OodleLZ_FuzzSafe_No,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,
                                        0,0,0,0,0,0,OodleLZ_Decode_Unthreaded,0,0,0,0,
                                        OodleXHandleAutoDelete_No,
                                        deps,num_deps);
    
    // ... main thread can do other work here ...
    
    OodleXStatus st = OodleX_WaitAndDelete(decompH);
    if ( st != OodleXStatus_Done )
    {
        OodleXLog_Printf_v0("OodleXLZ_Decompress_Narrow_Async failed!\n");
        return 10;
    }
    
    // check it :
    OOEX_ASSERT( memcmp(inBuf,decompBuf,inSize) == 0 );
    
    OodleXFree_IOAligned(compBuf);
    }
    
    //===================================================================================
    
/*

    Read-Decomp 3 :
    
    Read and decompress with IO overlap, but only using a single thread (not "wide").
        
    OodleXLZ_ReadAndDecompress_Stream_Async is an API provided to do IO overlap with decompression.
    It's something you could easily write yourself in Oodle.  It uses a coroutine to do IO on chunks
    and then decompress the chunks as they arive.  It tries to always be doing the IO for the next chunk
    while decompressing the current chunk.
    
*/
    
    OodleXLog_Printf_v1("Doing read and decomp simultaneously :\n");
    
    // prevent cheating :
    memset(decompBuf,(int)rand(),inSize);
    
    {
    void * compBuf = OodleXMalloc_IOAligned(compSize);

    /*
    
    Enqueue an open request + read an initial small chunk into compBuf
    
    We don't wait on the Open, but immediately start the OodleXLZ_ReadAndDecompress_Stream_Async operation,
    and pass it the openAndRead handle.
    
    */  
    
    OodleXIOQFile compFile;
    OO_SINTa initialReadSize = OodleX_IOAlignUpSINTa( OOEX_MIN(compSize,512*1024) );
    OodleXHandle openAndReadH = OodleXIOQ_OpenAndRead_Async(&compFile,compName,compBuf,initialReadSize,0,fileOpenFlags);
    
    OodleXHandle readAndDecomp = OodleXLZ_ReadAndDecompress_Stream_Async(OodleXAsyncSelect_Full,
                                            compBuf,compSize,decompBuf,inSize,
                                            OodleLZ_FuzzSafe_No,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,0,0,
                                            compFile,compBuf,0,openAndReadH,initialReadSize);
                                            
    // enqueue a CloseFile with a dependency on the ReadAndDecomp operation :
    OodleXIOQ_CloseFile_Async(compFile,OODLEX_FILE_CLOSE_NO_TRUNCATE_SIZE,OodleXHandleAutoDelete_Yes,OodleXPriority_Normal,&readAndDecomp,1);
    
    // ... main thread can do other work ...
    
    OodleXStatus st = OodleX_WaitAndDelete(readAndDecomp);
    if ( st != OodleXStatus_Done )
    {
        OodleXLog_Printf_v0("ReadAndDecomp failed\n");
        return 10;
    }
    
    // check it :
    OOEX_ASSERT( memcmp(inBuf,decompBuf,inSize) == 0 );
    
    OodleXFree_IOAligned(compBuf);
    }
    
    //===================================================================================
        
/*

    Read-Decomp 4 :
    
    Read and decompress with IO overlap, and using all worker threads ("wide").
    
    ReadAndDecompress_Wide needs the seekTable to find the compressed block boundaries.  Normally you
    would have to send that in a file (see the OOZ APIs if you want Oodle to do it for you).  Here
    we just use the seekTable we made earlier and kept in memory.
        
*/

    OodleXLog_Printf_v1("Doing read and decomp wide :\n");
    
    // prevent cheating :
    memset(decompBuf,(int)rand(),inSize);
    
    {
    void * compBuf = OodleXMalloc_IOAligned(compSize);
    
    // open compressed file and do an initial read :
    OodleXIOQFile compFile;
    OO_SINTa initialReadSize = OodleX_IOAlignUpSINTa( OOEX_MIN(compSize,256*1024) );
    OodleXHandle openAndReadH = OodleXIOQ_OpenAndRead_Async(&compFile,compName,compBuf,initialReadSize,0,fileOpenFlags);

    // instead of waiting on openAndReadH here, you could pass it as a dependency
    //  to the OodleXLZ_ReadAndDecompress_Wide_Async function to make the whole sequence async
    // but we'll just stall here for simplicity in this example :
    OodleXStatus st = OodleX_WaitAndDelete(openAndReadH);
    
    if ( st != OodleXStatus_Done )
    {
        OodleXLog_Printf_v0("OpenAndRead failed\n");
        return 10;
    }
    
    // fire off the read-and-decomp job :   
    OodleXHandle readAndDecomp = OodleXLZ_ReadAndDecompress_Wide_Async(OodleXAsyncSelect_Full,
                                            seekTable,compBuf,compSize,initialReadSize,compFile,0,
                                            decompBuf,inSize,
                                            OodleLZ_FuzzSafe_No,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None);

    // enqueue a CloseFile with a dependency on the ReadAndDecomp operation :
    OodleXIOQ_CloseFile_Async(compFile,OODLEX_FILE_CLOSE_NO_TRUNCATE_SIZE,OodleXHandleAutoDelete_Yes,OodleXPriority_Normal,&readAndDecomp,1);

    // ... main thread can do other work ...
        
    st = OodleX_WaitAndDelete(readAndDecomp);
    
    if ( st != OodleXStatus_Done )
    {
        OodleXLog_Printf_v0("ReadAndDecomp failed\n");
        return 10;
    }
    
    // check it :
    OOEX_ASSERT( memcmp(inBuf,decompBuf,inSize) == 0 );
    
    OodleXFree_IOAligned(compBuf);
    }
    
    //===================================================================================
    // all done, clean up :
    
    OodleLZ_FreeSeekTable(seekTable);
    
    OodleXFree(decompBuf);
    OodleXFree_IOAligned(inBuf);

    OodleX_Shutdown();

    return 0;
}


