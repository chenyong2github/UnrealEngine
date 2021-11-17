/****

example_jobify_test :

test OodleLZ Jobify using the"example_jobify" job system implementations

****/

#include "../include/oodle2.h"
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 

//===================================

#include "example_jobify.h"

#include "ooex.h"
#include "read_whole_file.h"
 
// ---- Job system tester

//#include "job_stress_test.h"

// ---- Main program

#ifdef BUILDING_EXAMPLE_CALLER
#define main example_jobify_test
#endif

extern "C" int main(int argc,char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: example_jobify_test <filename>\n");
        return 1;
    }
    
    //example_jobify_init_oodleX();
    void * jobifyUserPtr = example_jobify_init();
    
    OodleCore_Plugins_SetJobSystemAndCount(example_jobify_run_job_fptr, example_jobify_wait_job_fptr, example_jobify_target_parallelism);
    //etc :
    //OodleNet_Plugins_SetJobSystemAndCount(example_jobify_run_job_fptr, example_jobify_wait_job_fptr, example_jobify_target_parallelism);

    const char *filename = argv[1];

    // Read the provided input file
    OO_SINTa file_size = 0;
    char * file_bytes = read_whole_file(filename, &file_size);
    if (!file_bytes)
        OOEX_ASSERT_FAILURE_ALWAYS("Error reading input file!\n");

    printf("\"%s\": %lld bytes.\n", filename, (long long)file_size);

    // Determine the required size of the output buffer, then allocate it.
    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Invalid,file_size);
    unsigned char * comp_buf = new unsigned char[comp_buf_size];
    if (!comp_buf)
        OOEX_ASSERT_FAILURE_ALWAYS("Error allocating compressed data buffer!\n");

    // Run the job system test.
    //Oodle_JobSystemStressTest(thread_pool_run_job, thread_pool_wait_job, NULL, 0, 256, 50000);

    unsigned char * decomp_buf = new unsigned char[file_size];
    if (!decomp_buf)
        OOEX_ASSERT_FAILURE_ALWAYS("Error allocating decompressed data buffer!\n");

    for (int iter=0;iter<3;iter++)
    {
        // Configure compresion parameters
        OodleLZ_Compressor compressor = OodleLZ_Compressor_Kraken;
        // Jobify only helps Optimal level encoders :
        OodleLZ_CompressionLevel level = OodleLZ_CompressionLevel_Optimal2;
        
        OO_SINTa comp_result;
            
        if ( iter == 0 ) 
        {
            printf("Compressing with Default args:\n");
            
            // Default args have Jobify ON (Jobify_Default)
            // jobifyUserPtr will be NULL
            // this means if you call OodleLZ_Compress like this with default args
            //  you will get a callback to RunJob but with NULL user pointer
            
            comp_result = OodleLZ_Compress(compressor, file_bytes, file_size, comp_buf, level);
        
        }
        else
        {       
            OodleLZ_CompressOptions options = *OodleLZ_CompressOptions_GetDefault(compressor, level);
            
            if ( iter == 1 )
            {
                printf("Compressing with Jobify Disabled:\n");
                
                options.jobify = OodleLZ_Jobify_Disable;
            }
            else
            {
                // Compress the file contents!
                printf("Compressing with Jobify Aggressive:\n");
                
                // Turn on aggressive jobifying for this example. This tends to use
                // extra memory; if you're worried about memory usage, stick with the
                // default.
                options.jobify = OodleLZ_Jobify_Aggressive;
            }
            
            // pass required pointer to jobify context :
            options.jobifyUserPtr = jobifyUserPtr;

            comp_result = OodleLZ_Compress(compressor, file_bytes, file_size, comp_buf, level, &options);
        }
        
        if (comp_result == OODLELZ_FAILED)
            OOEX_ASSERT_FAILURE_ALWAYS("Error occured during compression!\n");
                
        printf("Compressed %lld bytes -> %lld bytes.\n", 
            (long long)file_size, 
            (long long)comp_result);

        // Decompress to make sure it worked.
        printf("Decompressing...\n");

        OO_SINTa decomp_result = OodleLZ_Decompress(comp_buf, comp_result, decomp_buf, file_size, OodleLZ_FuzzSafe_Yes);
        if (decomp_result == OODLELZ_FAILED)
            OOEX_ASSERT_FAILURE_ALWAYS("Error occured during decompression!\n");

        // Verify that the decompressed results indeed match!
        if (memcmp(file_bytes, decomp_buf, file_size) != 0)
            OOEX_ASSERT_FAILURE_ALWAYS("The original and decompressed data disagree!\n");

    }

    printf("All done!\n");

    delete[] decomp_buf;
    delete[] comp_buf;
    free(file_bytes);
    return 0;
}


