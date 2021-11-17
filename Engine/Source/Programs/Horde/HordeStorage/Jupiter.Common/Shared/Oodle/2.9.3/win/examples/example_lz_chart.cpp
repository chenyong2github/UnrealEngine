//idoc(parent,examples)
//idoc(tutorial,example_lz_chart,example_lz_chart : Example that makes a chart of OodleLZ options )

/*

Oodle example_lz_chart

The Oodle SDK comes with a pre-built exe for example_lz_chart in the bin/ directory

usage :
example_lz_chart <file>

Run with a file name, which will be loaded and used as data to test on.

You can also toggle compile-time options with the define EXAMPLE_LZ_CHART_NUM_LEVELS below.

makes an output like this :

<PRE>
Oodle 2.6.3 example_lz_chart <file>
lz_chart loading r:\testsets\lztestset\lzt99...
file size : 24700820
------------------------------------------------------------------------------
Selkie : super fast to encode & decode, least compression
Mermaid: fast decode with better-than-zlib compression
Kraken : good compression, fast decoding, great tradeoff!
Leviathan : very high compression, slowest decode
------------------------------------------------------------------------------
chart cell shows | raw/comp ratio : encode MB/s : decode MB/s |
All compressors run at various encoder effort levels (SuperFast - Optimal).
Many repetitions are run for accurate timing.
------------------------------------------------------------------------------
       |   HyperFast4|   HyperFast3|   HyperFast2|   HyperFast1|   SuperFast |
Selkie |1.41:675:3895|1.45:622:3888|1.53:465:3696|1.68:369:3785|1.70:342:3759|
Mermaid|1.66:436:2189|1.66:436:2188|1.79:352:2090|2.01:276:2055|2.04:261:2025|
Kraken |1.55:588:1839|1.71:419:1136|1.88:331:1087|2.10:279:1093|2.27:167:1010|
------------------------------------------------------------------------------
compression ratio (raw/comp):
       |   HyperFast4|   HyperFast3|   HyperFast2|   HyperFast1|   SuperFast |
Selkie |    1.412    |    1.447    |    1.526    |    1.678    |    1.698    |
Mermaid|    1.660    |    1.660    |    1.793    |    2.011    |    2.041    |
Kraken |    1.548    |    1.711    |    1.877    |    2.103    |    2.268    |
------------------------------------------------------------------------------
encode speed (MB/s):
       |   HyperFast4|   HyperFast3|   HyperFast2|   HyperFast1|   SuperFast |
Selkie |    674.548  |    621.811  |    464.555  |    369.364  |    341.588  |
Mermaid|    435.650  |    435.923  |    352.475  |    276.199  |    260.511  |
Kraken |    588.488  |    418.921  |    331.423  |    279.129  |    167.206  |
------------------------------------------------------------------------------
decode speed (MB/s):
       |   HyperFast4|   HyperFast3|   HyperFast2|   HyperFast1|   SuperFast |
Selkie |   3894.644  |   3887.820  |   3695.984  |   3785.457  |   3758.594  |
Mermaid|   2189.030  |   2187.863  |   2090.319  |   2054.897  |   2024.692  |
Kraken |   1839.091  |   1135.920  |   1086.922  |   1093.407  |   1009.967  |
------------------------------------------------------------------------------
       |   VeryFast  |   Fast      |   Normal    |   Optimal1  |   Optimal3  |
Selkie |1.75:205:3490|1.83:105:3687|1.86: 43:3815|1.93:5.1:3858|1.94:2.6:3856|
Mermaid|2.12:173:1991|2.19: 84:2177|2.21: 32:2291|2.37:2.8:2058|2.44:1.8:1978|
Kraken |2.32:112:1104|2.39: 37:1187|2.43: 20:1189|2.55:3.1:1103|2.65:1.2:1038|
Leviath|2.50: 31: 738|2.57: 17: 787|2.62:9.5: 807|2.71:1.6: 811|2.76:0.9: 776|
------------------------------------------------------------------------------
compression ratio (raw/comp):
       |   VeryFast  |   Fast      |   Normal    |   Optimal1  |   Optimal3  |
Selkie |    1.748    |    1.833    |    1.863    |    1.933    |    1.943    |
Mermaid|    2.118    |    2.194    |    2.207    |    2.367    |    2.437    |
Kraken |    2.320    |    2.390    |    2.434    |    2.551    |    2.646    |
Leviath|    2.504    |    2.572    |    2.617    |    2.707    |    2.756    |
------------------------------------------------------------------------------
encode speed (MB/s):
       |   VeryFast  |   Fast      |   Normal    |   Optimal1  |   Optimal3  |
Selkie |    204.621  |    104.758  |     42.504  |      5.102  |      2.554  |
Mermaid|    172.681  |     84.227  |     32.030  |      2.798  |      1.836  |
Kraken |    111.858  |     37.126  |     19.859  |      3.091  |      1.204  |
Leviath|     31.031  |     16.697  |      9.461  |      1.621  |      0.869  |
------------------------------------------------------------------------------
decode speed (MB/s):
       |   VeryFast  |   Fast      |   Normal    |   Optimal1  |   Optimal3  |
Selkie |   3490.442  |   3686.689  |   3814.655  |   3857.857  |   3856.226  |
Mermaid|   1991.442  |   2176.725  |   2291.498  |   2057.575  |   1977.721  |
Kraken |   1104.172  |   1186.638  |   1189.372  |   1103.148  |   1038.352  |
Leviath|    737.934  |    787.152  |    806.523  |    811.161  |    775.800  |
------------------------------------------------------------------------------
</PRE>

*/

#include "../include/oodle2x.h"
#include "ooex.h"

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef BUILDING_EXAMPLE_CALLER
#define main example_lz_chart
#endif

//===========================================================
// NOTE : these timings are hot cache (no cache invalidation between repeats)
//  that could be significant on very small buffers

struct time_and_len
{
    double time;
    OO_SINTa len;
};

// if scratch is NULL or insufficient, Oodle will allocate internally
static void * s_scratch_memory = NULL;
static OO_SINTa s_scratch_memory_size = 0;

static time_and_len Encode_And_Time(const void * raw_buf, OO_SINTa raw_len, void * comp_buf,
    OodleLZ_Compressor compressor,
    OodleLZ_CompressionLevel compression_level,
    OodleLZ_CompressOptions * compression_options,
    int min_repeats,
    double min_total_seconds)
{
    double total_seconds = 0;
    time_and_len ret;
    ret.time = 999999.9;

    for(;;)
    {
        double dt = OodleX_GetSeconds();
        
        ret.len = OodleLZ_Compress(compressor, raw_buf, raw_len, comp_buf, compression_level, compression_options,
            NULL, NULL, s_scratch_memory, s_scratch_memory_size);

        dt = OodleX_GetSeconds() - dt;
        
        total_seconds += dt;
        ret.time = OOEX_MIN(ret.time,dt);
        min_repeats--;
        
        if ( min_repeats <= 0 && total_seconds >= min_total_seconds )
            break;
    }
    
    return ret;
}

static double Decode_And_Time( void * comp_buf, OO_SINTa comp_len, void * decode_buffer, OO_SINTa in_size,
    int min_repeats,
    double min_total_seconds)
{
    double total_seconds = 0;
    double ret = 999999.9;

    for(;;)
    {
        double dt = OodleX_GetSeconds();
                
        OO_SINTa decode_len = OodleLZ_Decompress( comp_buf, comp_len, decode_buffer, in_size , OodleLZ_FuzzSafe_Yes,
            OodleLZ_CheckCRC_No, OodleLZ_Verbosity_None, NULL, 0, NULL,NULL, s_scratch_memory,s_scratch_memory_size );

        dt = OodleX_GetSeconds() - dt;
        
        OOEX_ASSERT_ALWAYS( decode_len == in_size );
        
        total_seconds += dt;
        ret = OOEX_MIN(ret,dt);
        min_repeats--;
        
        if ( min_repeats <= 0 && total_seconds >= min_total_seconds )
            break;
    }
    
    return ret;
}
        
//===========================================================

static void bar()
{
    OodleXLog_Printf_v1("------------------------------------------------------------------------------\n");
}

static void header(const OodleLZ_CompressionLevel * chart_levels, int num_levels)
{
    //OodleXLog_Printf_v1("%5s |   %-10s|   %-10s|   %-10s|   %-10s|\n",
    //  "","VeryFast","Fast","Normal","Optimal1");
        
    OodleXLog_Printf_v1("%7s|","");
    
    for(int l=0;l<num_levels;l++)
    {           
        OodleLZ_CompressionLevel level = chart_levels[l];
        
        OodleXLog_Printf_v1("   %-10s|",
           OodleLZ_CompressionLevel_GetName(level));
    }
    OodleXLog_Printf_v1("\n");
}

static const char * truncated_compressor_name(  OodleLZ_Compressor compressor )
{
    const char * n = OodleLZ_Compressor_GetName(compressor);
    static char buf[10];
    memset(buf,' ',sizeof(buf));
    memcpy(buf,n,OOEX_MIN( strlen(n), sizeof(buf) ) );
    buf[7] = 0;
    return buf;
}   
        
extern "C" int main(int argc,char *argv[])
{
    // Init Oodle systems with default options :
    OodleXInitOptions opts;
    if ( ! OodleX_Init_GetDefaults(OODLE_HEADER_VERSION,&opts) )
    {
        fprintf(stderr,"Oodle header version mismatch.\n");
        return 10;
    }
    // change opts here if you like
    // NOTE : default options enable the OodleX thread system
    //  so encoders will be "Jobified"
    if ( ! OodleX_Init(OODLE_HEADER_VERSION,&opts) )
    {
        fprintf(stderr,"OodleX_Init failed.\n");
        return 10;
    }

    OodleXLog_Printf_v1("Oodle %s example_lz_chart <file>\n",OodleVersion);
    
    if ( argc < 2 )
    {
        fprintf(stderr,"error: specify a sample data file to test on.\n");
        return 10;
    }
    
    const char * in_name = argv[1];
    OodleXLog_Printf_v1("lz_chart loading %s...\n",in_name);

    // read the input file to the global buffer :
    OO_S64 in_size_64;
    void * in_buffer = OodleXIOQ_ReadMallocWholeFile_AsyncAndWait(in_name,&in_size_64);
    
    if ( ! in_buffer)
    {
        OodleXLog_Printf_v0("failed to read %s\n",in_name); 
        return 10;
    }
    
    OodleXLog_Printf_v1("file size : " OOEX_S64_FMT "\n",in_size_64);
    
    OO_SINTa in_size = OodleX_S64_to_SINTa_check( in_size_64 );
        
    //-----------------------------------------------------
    
    // test parameters :
    //  increase these to get more reliable timing
    //  decrease these to run faster
    int min_encode_repeats = 2;
    int min_decode_repeats = 5;
    double min_total_seconds = 2.0;
    
    #if 1
    
    // test set of compressors : 
    OodleLZ_Compressor chart_compressors[] = {
        OodleLZ_Compressor_Selkie, OodleLZ_Compressor_Mermaid, OodleLZ_Compressor_Kraken, OodleLZ_Compressor_Leviathan
    };
        
    #define EXAMPLE_LZ_CHART_NUM_COMPRESSORS    ((int)(sizeof(chart_compressors)/sizeof(chart_compressors[0])))
    OOEX_ASSERT( EXAMPLE_LZ_CHART_NUM_COMPRESSORS == 4 );
    
    #define EXAMPLE_LZ_CHART_NUM_LEVELS 5
    
    OodleLZ_CompressionLevel chart_levels1[] = {
        OodleLZ_CompressionLevel_HyperFast4, OodleLZ_CompressionLevel_HyperFast3, OodleLZ_CompressionLevel_HyperFast2, OodleLZ_CompressionLevel_HyperFast1, OodleLZ_CompressionLevel_SuperFast,
    };
    OodleLZ_CompressionLevel chart_levels2[] = {
        OodleLZ_CompressionLevel_VeryFast, OodleLZ_CompressionLevel_Fast, OodleLZ_CompressionLevel_Normal, OodleLZ_CompressionLevel_Optimal1, OodleLZ_CompressionLevel_Optimal3
    };
    OOEX_ASSERT( ((int)(sizeof(chart_levels1)/sizeof(chart_levels1[0]))) == EXAMPLE_LZ_CHART_NUM_LEVELS );
    OOEX_ASSERT( ((int)(sizeof(chart_levels2)/sizeof(chart_levels2[0]))) == EXAMPLE_LZ_CHART_NUM_LEVELS );
    
    #else
    
    // test just 1 :
    
    OodleLZ_Compressor chart_compressors[] = {
        OodleLZ_Compressor_Kraken
    };
        
    #define EXAMPLE_LZ_CHART_NUM_COMPRESSORS    1
    
    #define EXAMPLE_LZ_CHART_NUM_LEVELS 1
    
    OodleLZ_CompressionLevel chart_levels1[] = {
        OodleLZ_CompressionLevel_None
    };
    OodleLZ_CompressionLevel chart_levels2[] = {
        OodleLZ_CompressionLevel_Optimal2
    };
    
    #endif
    
    //===========================================================

    OO_SINTa comp_lens[EXAMPLE_LZ_CHART_NUM_COMPRESSORS][EXAMPLE_LZ_CHART_NUM_LEVELS];
    double decode_speeds[EXAMPLE_LZ_CHART_NUM_COMPRESSORS][EXAMPLE_LZ_CHART_NUM_LEVELS];
    double encode_speeds[EXAMPLE_LZ_CHART_NUM_COMPRESSORS][EXAMPLE_LZ_CHART_NUM_LEVELS];
    
    //-----------------------------------------------------
    
    void * comp_buf;

    OO_SINTa comp_buf_size = OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Invalid,in_size);

    comp_buf = OodleXMallocBig(comp_buf_size);
        
    void * decode_buffer = OodleXMallocBig( in_size );
        
    //-----------------------------------------------------
    // allocate scratch 

    // get enough so the decoder won't allocate
    //  and encoders up to Leviathan-Normal won't either
    //  but optimals will
        
    s_scratch_memory_size = OodleLZ_GetCompressScratchMemBound(OodleLZ_Compressor_Leviathan,OodleLZ_CompressionLevel_Normal,in_size,NULL);
    
    s_scratch_memory_size = OOEX_MAX( s_scratch_memory_size , OodleLZDecoder_MemorySizeNeeded() );
    
    s_scratch_memory = OodleXMalloc(s_scratch_memory_size);

    //-----------------------------------------------------
    
    bar();
    OodleXLog_Printf_v1("Selkie : super fast to encode & decode, least compression\n");
    OodleXLog_Printf_v1("Mermaid: fast decode with better-than-zlib compression\n");
    OodleXLog_Printf_v1("Kraken : good compression, fast decoding, great tradeoff!\n");
    OodleXLog_Printf_v1("Leviathan : very high compression, slowest decode\n");
    bar();
    OodleXLog_Printf_v1("chart cell shows | raw/comp ratio : encode MB/s : decode MB/s | \n");
    OodleXLog_Printf_v1("All compressors run at various encoder effort levels (SuperFast - Optimal).\n");
    OodleXLog_Printf_v1("Many repetitions are run for accurate timing.\n");
    bar();
    
    for(int twice=0;twice<2;twice++)
    {
        const OodleLZ_CompressionLevel * chart_levels =
            twice ? chart_levels2 : chart_levels1;
    
        int num_compressors = EXAMPLE_LZ_CHART_NUM_COMPRESSORS;
        // don't bother running Leviathan at the HyperFast modes :
        if ( ! twice && chart_compressors[num_compressors-1] == OodleLZ_Compressor_Leviathan ) num_compressors--;
    
        header(chart_levels,EXAMPLE_LZ_CHART_NUM_LEVELS);
        
        for(int c=0;c<num_compressors;c++)
        {
            OodleLZ_Compressor compressor = chart_compressors[c];
            
            OodleXLog_Printf_v1("%s|",truncated_compressor_name(compressor));
            
            // iterate backwards so we start the slowest first
            for(int l=0;l<EXAMPLE_LZ_CHART_NUM_LEVELS;l++)
            {           
                OodleLZ_CompressionLevel level = chart_levels[l];
            
                time_and_len tl = 
                    Encode_And_Time(in_buffer,in_size,comp_buf,compressor,level,NULL,min_encode_repeats,min_total_seconds);
                
                OO_SINTa comp_len = tl.len;
                double encode_seconds = tl.time;
                
                double encode_mbps = (in_size/1000000.0) / encode_seconds;      
                encode_speeds[c][l] = encode_mbps;
                
                comp_lens[c][l] = comp_len;
                
                double ratio = (double)in_size/comp_len;
                
                if ( ratio >= 10.0 )
                    OodleXLog_Printf_v1("%4.1f",ratio);
                else
                    OodleXLog_Printf_v1("%4.2f",ratio);
                
                if ( encode_mbps >= 10.0 )
                    OodleXLog_Printf_v1(":%3d:",(int)(encode_mbps+0.5));
                else
                    OodleXLog_Printf_v1(":%3.1f:",encode_mbps);
                    
                double decode_seconds = Decode_And_Time( comp_buf, comp_len, decode_buffer, in_size , 
                    min_decode_repeats,min_total_seconds);
        
                OOEX_ASSERT_ALWAYS( memcmp(decode_buffer,in_buffer,in_size) == 0 );
                        
                double decode_mbps = (in_size/1000000.0) / decode_seconds;              
                decode_speeds[c][l] = decode_mbps;
                
                OodleXLog_Printf_v1("%4d|",(int)(decode_mbps+0.5));
            }
            OodleXLog_Printf_v1("\n");
            
        }

        //-----------------------------------------------------

        bar();  
        OodleXLog_Printf_v1("compression ratio (raw/comp):\n");
        header(chart_levels,EXAMPLE_LZ_CHART_NUM_LEVELS);
        
        for(int c=0;c<num_compressors;c++)
        {
            OodleLZ_Compressor compressor = chart_compressors[c];
            
            OodleXLog_Printf_v1("%s|",truncated_compressor_name(compressor));
            
            // iterate backwards so we start the slowest first
            for(int l=0;l<EXAMPLE_LZ_CHART_NUM_LEVELS;l++)
            {           
                double ratio = (double)in_size/comp_lens[c][l];
                
                OodleXLog_Printf_v1("%9.3f    |",ratio);
            }
            OodleXLog_Printf_v1("\n");
        }

        bar();
        OodleXLog_Printf_v1("encode speed (MB/s):\n");
        header(chart_levels,EXAMPLE_LZ_CHART_NUM_LEVELS);
        
        for(int c=0;c<num_compressors;c++)
        {
            OodleLZ_Compressor compressor = chart_compressors[c];
            
            OodleXLog_Printf_v1("%s|",truncated_compressor_name(compressor));
            
            // iterate backwards so we start the slowest first
            for(int l=0;l<EXAMPLE_LZ_CHART_NUM_LEVELS;l++)
            {           
                OodleXLog_Printf_v1("%11.3f  |",encode_speeds[c][l]);
            }
            OodleXLog_Printf_v1("\n");
        }

        bar();
        OodleXLog_Printf_v1("decode speed (MB/s):\n");
        header(chart_levels,EXAMPLE_LZ_CHART_NUM_LEVELS);
        
        for(int c=0;c<num_compressors;c++)
        {
            OodleLZ_Compressor compressor = chart_compressors[c];
            
            OodleXLog_Printf_v1("%s|",truncated_compressor_name(compressor));
            
            // iterate backwards so we start the slowest first
            for(int l=0;l<EXAMPLE_LZ_CHART_NUM_LEVELS;l++)
            {           
                OodleXLog_Printf_v1("%11.3f  |",decode_speeds[c][l]);
            }
            OodleXLog_Printf_v1("\n");
        }

        bar();

    } // twice

    //-----------------------------------------------------

    OodleXFree(s_scratch_memory);
    OodleXFreeBig(decode_buffer);
    OodleXFreeBig(comp_buf);        
    OodleXFree_IOAligned(in_buffer);

    //OodleX_Shutdown();
    OodleX_Shutdown(NULL,OodleX_Shutdown_LogLeaks_Yes,0);
    
    //OodleXLog_Printf_v1("press a key\n");
    //fgetc(stdin);
    
    return 0;
}

//=================================================
