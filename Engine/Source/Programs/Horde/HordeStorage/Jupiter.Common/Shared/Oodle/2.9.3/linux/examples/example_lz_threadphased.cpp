//idoc(parent,examples)
//idoc(tutorial,example_lz_threadphased,example_lz_threadphased : Example of 2-thread ThreadPhased decoding)

/*

Oodle example_lz_threadphased

This example demonstrates the ability of Kraken to decode using "ThreadPhased" parallelism

See $OodleLZ_About_ThreadPhasedDecode for details.

ThreadPhased decoding provides a 1X-2X speedup, typically around 33%-50%

This example implements an entire ThreadPhased decoder, in example_lz_threadphased_decompress.

The idea is you may take this example code and modify it to run in your own threading or job system,
so that you may implement the threading however you look.

example_lz_threadphased_decompress can be used without OodleX or the Oodle Worker system.


In this example I use OodleX_CreateThread and OodleX_Semaphore ; these are just handy cross-platform
implementations for me.  The intention is that you replace them with your own threading system calls.

For the semaphore, it's important that it tries to avoid going into an OS Wait (thread sleep) when the
two threads are nearly synchronized.  To avoid this you want a user-space spin-backoff loop to try to keep
the two threads awake together.  One option is to use something like "fastsemaphore" as a wrapper to your
underlying OS semaphore :

http://cbloomrants.blogspot.com/2011/12/12-08-11-some-semaphores.html

If you use that "fastsemaphore" make sure to set
        int spin_count = 1; // ! set this for your system
something like 100 is usually reasonable, and probably add a backoff.

*/

#include "../include/oodle2x.h"
#include "ooex.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "make_example_input.h"

#ifdef BUILDING_EXAMPLE_CALLER
#define main example_lz_threadphased
#endif

//===========================================================
// file names :

static const char * in_name = "oodle_example_input_file";

//===========================================================

static bool example_lz_threadphased_decompress(void * comp_buf,OO_SINTa comp_len,void * dec_buf,OO_SINTa dec_size,bool async);
    
//===========================================================

extern "C" int main(int argc,char *argv[])
{
    // Init Oodle systems with default options :
    if ( ! OodleX_Init_Default(OODLE_HEADER_VERSION) )
    {
        fprintf(stderr,"OodleX_Init failed.\n");
        return 10;
    }   

    if ( argc >= 2 ) in_name = argv[1];
    else make_example_input(in_name);

    OodleXLog_Printf_v1("example_lz_threadphased : %s\n",in_name);

    // read the input file to the global buffer :
    OO_S64 in_size_64;
    void * in_buffer = OodleXIOQ_ReadMallocWholeFile_AsyncAndWait(in_name,&in_size_64);
    
    if ( ! in_buffer)
    {
        OodleXLog_Printf_v0("failed to read %s\n",in_name); 
        return 10;
    }
    
    OO_SINTa in_size = OodleX_S64_to_SINTa_check( in_size_64 );
    
    //=========================================
    // select options :
    
    OodleLZ_Compressor compressor = OodleLZ_Compressor_Kraken;
    //OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Normal;
    OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Fast;

    // this example is only valid for compressors that support ThreadPhased decode : (eg. Kraken)
    OOEX_ASSERT( OodleLZ_Compressor_CanDecodeThreadPhased(compressor) );

    //=========================================
    
    // allocate memory big enough for compressed data :
    void * comp_buf = OodleXMalloc( OodleLZ_GetCompressedBufferSizeNeeded(compressor,in_size) );    
    // memory to decode to :
    void * dec_buf = OodleXMalloc( in_size );
    
    //=========================================
    // compress the input :
    
    // compress :
    // this is just a normal whole-block compress, no special parallel mode is needed
    OO_SINTa comp_len = OodleLZ_Compress(compressor,in_buffer,in_size,comp_buf,level);
    
    OodleXLog_Printf_v1("Compressed : %d -> %d\n",(int)in_size,(int)comp_len);
    
    //=======================================
    /*

    We can decompress asynchronously using the "ThreadPhased" helper in OodleX.
    
    The Narrow helper uses 2 threads and frees up the calling thread to do other work.  
    
    OodleXLZ_Decompress_ThreadPhased_Narrow_Async uses the OodleX Worker thread system, which is started by
    default in OodleX_Init.
    
    NOTE for maximum speed you should pass in the scratch space needed by OodleXLZ_Decompress_ThreadPhased_Narrow_Async
    pre-allocated, so it doesn't have to do the allocation internally.
    
    */
    
    OodleXLog_Printf_v1("OodleXLZ_Decompress_ThreadPhased_Narrow_Async...\n");
    
    OodleXHandle decomp_handle = OodleXLZ_Decompress_ThreadPhased_Narrow_Async(comp_buf,comp_len,dec_buf,in_size);
    
    // .. can do other work on the main thread now ..
    
    OodleXStatus decomp_status = OodleX_WaitAndDelete(decomp_handle);
    if ( decomp_status == OodleXStatus_Error )
    {
        OodleXLog_Printf_v1("Error!\n");
    }
    
    // check it :
    OOEX_ASSERT( memcmp(in_buffer,dec_buf,in_size) == 0 );
    
    //=======================================
    // do our own thread-phased decode :
        
    OodleXLog_Printf_v1("example_lz_threadphased_decompress ");

    // run a few reps to stress test :  
    for(int rep=0;rep<10;rep++)
    {
        // run async and sync options :
        for(int async = 0;async<=1;async++)
        {
            OodleXLog_Printf_v1(async ? "+" : "-");
        
            // wipe out dec_buf to make sure we decode correctly :
            memset(dec_buf,0xEE,in_size);
        
            if ( !  example_lz_threadphased_decompress(comp_buf,comp_len,dec_buf,in_size,!!async) )
            {
                OodleXLog_Printf_v1("Error!\n");
            }
        
            // check it :
            OOEX_ASSERT( memcmp(in_buffer,dec_buf,in_size) == 0 );
        }
    }
    OodleXLog_Printf_v1("\n");
    
    //=======================================
    
    OodleXFree(comp_buf);
    OodleXFree(dec_buf);
    OodleXFree_IOAligned(in_buffer);

    OodleX_Shutdown(NULL,OodleX_Shutdown_LogLeaks_Yes,0);
    
    return 0;
}

//===========================================================
/***

example_lz_threadphased_threadfunc

Example of how to run a ThreadPhased decode yourself.

The basic idea of ThreadPhased decoding is that the OodleLZ_Decompress work on each BLOCK can be
split into two phases.  This can be invoked by just calling OodleLZ_Decompress twice on the same
block, first with OodleLZ_Decode_ThreadPhase1, then with OodleLZ_Decode_ThreadPhase2.

To get parallelism, we can run the two phases on two separate threads.

The rule is that you must run the Phase2 on each block after the Phase1 for that block is done,
and with the same "decoderMem" pointer.  The Phase2 decodes on all blocks must be done in
sequential order (unless they are Seek Resets).
The decoder memory used for OodleLZ_Decompress here must be larger than normal, of size
OodleLZ_ThreadPhased_BlockDecoderMemorySizeNeeded().

Our thread model here is a two-thread circular buffer scan with semaphore signalling.

Thread 1 does :

    For each block
    Wait on sem_blocksavail to get an available circular buffer slot
    Do Phase1 Decompress into a slot
    Post sem_phase1done

Thread 2 does :
    
    For each block
    Wait on sem_phase1done to get a slot with phase1 decode done
    Do Phase2 Decompress on that slot
    Post sem_blocksavail to signal Thread 1 that this slot may be reused

    
***/

struct example_lz_threadphased_threaddata
{
    volatile OO_U32 * error_cancel;
    OodleX_Semaphore * sem_consume;
    OodleX_Semaphore * sem_produce;
    
    OO_SINTa num_blocks;
    OO_SINTa num_scratch_blocks;
    OO_U8 * scratch_mem;
    OO_SINTa scratch_block_size;
    
    OO_U8 * rawBuf; OO_SINTa rawSize;
    const OO_U8 * compBuf; OO_SINTa compSize;
    OodleLZ_Decode_ThreadPhase threadPhase;
    
    bool success;
};

#define THREAD_ERROR    0
#define THREAD_SUCCESS  1

// thread_function_DecodePhase
//  can be used for both Phase1 and Phase2 !
static OO_U32 OODLE_CALLBACK example_lz_threadphased_threadfunc( void * user_data )
{
    example_lz_threadphased_threaddata * data = (example_lz_threadphased_threaddata *)user_data;
    
    const OO_U8 * compPtr = (OO_U8 *)(data->compBuf);
    const OO_U8 * compEnd = compPtr + data->compSize;
    
    OO_SINTa decoderMemSize = data->scratch_block_size;
        
    OO_SINTa scratch_i = 0;
    for(OO_SINTa block_pos = 0;block_pos < data->rawSize;block_pos += OODLELZ_BLOCK_LEN, scratch_i++)
    {
        // consume one :
        OodleX_Semaphore_Wait(data->sem_consume);
    
        // relaxed load of shared error_cancel variable ; Sem Wait acts as Acquire barrier
        if ( *(data->error_cancel) ) return THREAD_ERROR;
    
        if ( scratch_i == data->num_scratch_blocks ) scratch_i = 0;
        
        OO_U8 * decoderMem = data->scratch_mem + scratch_i * decoderMemSize;
        
        OO_U8 * chunk_ptr = (OO_U8 *)(data->rawBuf) + block_pos;
                    
        OO_SINTa block_len = OOEX_MIN((data->rawSize - block_pos),OODLELZ_BLOCK_LEN);
                    
        OO_BOOL indy;
        OO_SINTa block_complen = OodleLZ_GetCompressedStepForRawStep(compPtr,compEnd-compPtr,block_pos,block_len,NULL,&indy);
        if ( block_complen <= 0 )
        {
            // handle error
            // relaxed store of shared variable; Sem Post acts as release barrier
            *(data->error_cancel) = 1;
            OodleX_Semaphore_Post(data->sem_produce);
            return THREAD_ERROR;
        }
                    
        OO_SINTa gotLen = OodleLZ_Decompress(compPtr,block_complen,chunk_ptr,block_len,
            OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_No,OodleLZ_Verbosity_None,
            data->rawBuf,data->rawSize,
            NULL,NULL,
            decoderMem,decoderMemSize,
            data->threadPhase
            );
        OOEX_ASSERT( gotLen == block_len+block_pos );
        if ( gotLen != block_len+block_pos )
        {
            // handle error
            // relaxed store of shared variable; Sem Post acts as release barrier
            *(data->error_cancel) = 1;
            OodleX_Semaphore_Post(data->sem_produce);
            return THREAD_ERROR;
        }
        
        compPtr += block_complen;
        
        OodleX_Semaphore_Post(data->sem_produce);
    }

    data->success = true;   
    return THREAD_SUCCESS;
}

static bool example_lz_threadphased_decompress(void * comp_buf,OO_SINTa comp_len,void * dec_buf,OO_SINTa dec_size,bool async)
{
    /**
    
    circularBufferBlockCount is the number of circular buffer slots for the two threads to
    communicate through
    
    circularBufferBlockCount >= 2
    higher is faster because it allows less synchronization of the two threads
    lower means less memory required
    circularBufferBlockCount >= 4 is reasonable
    circularBufferBlockCount >= 6 is close to full speed
    
    **/
    OO_SINTa circularBufferBlockCount = 6; // parameter

    // async is an option for whether the whole operation is run async off this thread or not
    //  (eg. with 2 additional threads or 1 additional thread)

    // check that the data contains a valid ThreadPhased compressor :
    OodleLZ_Compressor compressor = OodleLZ_GetAllChunksCompressor(comp_buf,comp_len,dec_size);
    if (! OodleLZ_Compressor_CanDecodeThreadPhased(compressor) )
    {
        OodleXLog_Printf_v1("Asked for ThreadPhase decode but ! OodleLZ_Compressor_CanDecodeThreadPhased\n");
        return false;
    }

    // count the number of OODLELZ_BLOCK_LEN in the total size :
    OO_SINTa nBlocks = (dec_size + OODLELZ_BLOCK_LEN-1)/OODLELZ_BLOCK_LEN;

    // don't need more than nBlocks :
    circularBufferBlockCount = OOEX_MIN(circularBufferBlockCount,nBlocks);

    // allocate space for the scratch circular buffer :
    OO_SINTa scratchBlockSize = OodleLZ_ThreadPhased_BlockDecoderMemorySizeNeeded();
    OO_SINTa scratchBufSize = scratchBlockSize * circularBufferBlockCount;
    
    // NOTE in production you may wish to preallocate this memory
    void * scratchBuf = OodleXMalloc(scratchBufSize);
    
    //=========================================================
    // set up the data needed for the thread phases
    // NOTE if you want to make the whole decode asynchronous,
    //   you can't put this on the stack, you need to package it up in memory
    
    // OodleX_Semaphore just initialize with 0 :
    OodleX_Semaphore sem_blocksavail = 0;
    OodleX_Semaphore sem_phase1done = 0;
    OO_U32 shared_error_cancel = 0; // shared atomic variable
    
    // starting state is that all circular buffer slots are available :
    OodleX_Semaphore_Post(&sem_blocksavail,(OO_S32)circularBufferBlockCount);
    
    example_lz_threadphased_threaddata td1 = { 0 };
    
    td1.error_cancel = &shared_error_cancel;
    td1.success = false;
    td1.compBuf = (OO_U8 *)(comp_buf);
    td1.compSize = comp_len;
    td1.rawBuf = (OO_U8 *)(dec_buf);
    td1.rawSize = dec_size;
    td1.num_blocks = nBlocks;
    td1.num_scratch_blocks = circularBufferBlockCount;
    td1.scratch_mem = (OO_U8 *)(scratchBuf);
    td1.scratch_block_size = scratchBlockSize;
        
    // thread 1 waits for blocks to be available in the circular buffer
    //  and posts that phase1 is done
    td1.threadPhase = OodleLZ_Decode_ThreadPhase1;
    td1.sem_consume = &sem_blocksavail;
    td1.sem_produce = &sem_phase1done;
    
    // thread 2 waits for each block to reach phase1done
    //  and then posts that the block is reusable
    //  same as thread 1, just swap the semaphores :
    example_lz_threadphased_threaddata td2;
    td2 = td1;
    td2.threadPhase = OodleLZ_Decode_ThreadPhase2;
    td2.sem_consume = &sem_phase1done;
    td2.sem_produce = &sem_blocksavail;
    
    // create a thread to run Phase1 :
    // NOTE : in production you probably don't want to create a thread every time you make
    //  this decompress call.  Rather use an idle thread that's already created.
    OodleX_Thread thread1 = OodleX_CreateThread(example_lz_threadphased_threadfunc,&td1);
    
    // either run Phase2 asynchronously (on another thread) or synchronously on this thread :
    if ( async )
    {
        OodleX_Thread thread2 = OodleX_CreateThread(example_lz_threadphased_threadfunc,&td2);
    
        // ... current thread is now available while decompress runs on 2 other threads ...
        // ... return and do other work ...
    
        OodleX_WaitAndDestroyThread(thread2);
    }
    else
    {
        // synchronous version - just run Phase2 on this thread :
        example_lz_threadphased_threadfunc(&td2);
    }
    
    OodleX_WaitAndDestroyThread(thread1);
    
    // OodleX_Semaphore doesn't need cleanup
    
    //===========================================================
    
    OodleXFree(scratchBuf);
    
    bool ok = td1.success && td2.success;
    
    return ok;
}

