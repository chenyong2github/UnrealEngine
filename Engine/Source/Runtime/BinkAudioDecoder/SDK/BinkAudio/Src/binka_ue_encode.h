#include "rrCore.h"

//
// Compresses a bink audio _file_. This is distinct from _blocks_, which is
// what binka_ue_decode handles.
//
// Compresses up to 16 channels. Sample rates above 48000 technically supported however
// it doesn't really add anything.
// Quality is between 0-9, with 0 being high quality. Below 4 is pretty bad.
// PcmData should be interleaved 16 bit pcm data.
// PcmDataLen is in bytes.
// OutData will be filled with a buffer allocated by MemAlloc that contains the
// compressed file data.
RADEXPFUNC U8 RADEXPLINK UECompressBinkAudio(
    void* PcmData, 
    U32 PcmDataLen, 
    U32 PcmRate, 
    U8 PcmChannels, 
    U8 Quality, 
    U8 GenerateSeekTable, 
    void* (*MemAlloc)(UINTa bytes), 
    void (*MemFree)(void* ptr),
    void** OutData, 
    U32* OutDataLen);

