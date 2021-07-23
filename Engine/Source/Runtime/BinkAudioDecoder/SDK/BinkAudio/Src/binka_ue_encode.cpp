// Copyright Epic Games, Inc. All Rights Reserved.
#include "rrCore.h"

#include "binkace.h"

#include "binka_ue_encode.h"
#include "binka_ue_file_header.h"

#include <stdio.h>
#include <string.h>

#define MAX_STREAMS 8

#define BINKA_COMPRESS_SUCCESS 0
#define BINKA_COMPRESS_ERROR_CHANS 1


struct MemBufferEntry
{
    U32 bytecount;
    MemBufferEntry* next;
    char bytes[1];
};

struct MemBuffer
{
    void* (*memalloc)(uintptr_t bytes);
    void (*memfree)(void* ptr);
    MemBufferEntry* head;
    MemBufferEntry* tail;
    U32 total_bytes;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void MemBufferAdd(MemBuffer* mem, void* data, U32 data_len)
{
    MemBufferEntry* entry = (MemBufferEntry*)mem->memalloc(sizeof(MemBufferEntry) + data_len);
    entry->bytecount = data_len;
    entry->next = 0;
    memcpy(entry->bytes, data, data_len);

    mem->total_bytes += data_len;
    if (mem->head == 0)
    {
        mem->tail = entry;
        mem->head = entry;
    }
    else
    {
        mem->tail->next = entry;
        mem->tail = entry;
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void MemBufferFree(MemBuffer* mem)
{
    MemBufferEntry* entry = mem->head;
    while (entry)
    {
        MemBufferEntry* next = entry->next;
        mem->memfree(entry);
        entry = next;
    }
    mem->total_bytes = 0;
    mem->head = 0;
    mem->tail = 0;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void MemBufferWriteBuffer(MemBuffer* mem, void* buffer)
{
    char* write_cursor = (char*)buffer;
    MemBufferEntry* entry = mem->head;
    while (entry)
    {
        MemBufferEntry* next = entry->next;
        memcpy(write_cursor, entry->bytes, entry->bytecount);
        write_cursor += entry->bytecount;
        entry = next;
    }
}

#define SEEK_TABLE_BUFFER_CACHE 16
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
struct SeekTableBuffer
{
    MemBuffer buffer;
    U16 current_stack[SEEK_TABLE_BUFFER_CACHE];
    U32 current_index;
    U32 total_count;
    void* collapsed;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void SeekTableBufferAdd(SeekTableBuffer* seek, U16 entry)
{
    seek->current_stack[seek->current_index] = entry;
    seek->current_index++;
    if (seek->current_index >= SEEK_TABLE_BUFFER_CACHE)
    {
        MemBufferAdd(&seek->buffer, seek->current_stack, sizeof(seek->current_stack));
        seek->current_index = 0;
    }
    seek->total_count++;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void SeekTableBufferFree(SeekTableBuffer* seek)
{
    seek->buffer.memfree(seek->collapsed);
    MemBufferFree(&seek->buffer);
    seek->current_index = 0;
    seek->total_count = 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static U32 SeekTableBufferTrim(SeekTableBuffer* seek)
{
    // Trim to 4096 size, or 2048 total entries.
    // Return # of frames per entry.

    // resolve to a single buffer for ease.
    U16* table = (U16*)seek->buffer.memalloc(seek->total_count * sizeof(U16));
    MemBufferWriteBuffer(&seek->buffer, table);
    memcpy((char*)table + seek->buffer.total_bytes, seek->current_stack, sizeof(U16) * seek->current_index);

    S32 frames_per_entry = 1;
    while (seek->total_count > 4096)
    {
        // collapse pairs.
        frames_per_entry <<= 1;

        S32 new_total_count = seek->total_count;
        U32 read_index = 0;
        U32 write_index = 0;
        while (read_index < seek->total_count)
        {
            U16 total = table[read_index];
            if (read_index + 1 < seek->total_count)
            {
                total += table[read_index + 1];
                new_total_count--;
            }

            table[write_index] = total;
            write_index++;
            read_index += 2;
        }

        seek->total_count = new_total_count;
    }

    seek->collapsed = table;
    return frames_per_entry;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
uint8_t UECompressBinkAudio(
    void* WavData, uint32_t WavDataLen, uint32_t WavRate, uint8_t WavChannels, 
    uint8_t Quality, uint8_t GenerateSeekTable, BAUECompressAllocFnType* MemAlloc, BAUECompressFreeFnType* MemFree,
    void** OutData, uint32_t* OutDataLen)
{
    //
    // Deinterlace the input.
    //
    U32 SamplesPerChannel = WavDataLen / (sizeof(S16) * WavChannels);
    U8 NumBinkStreams = (WavChannels / 2) + (WavChannels  & 1);
    if (NumBinkStreams > MAX_STREAMS)
        return BINKA_COMPRESS_ERROR_CHANS;

    S32 ChannelsPerStream[MAX_STREAMS] = {};
    char* SourceStreams[MAX_STREAMS] = {};
    S32 BytesPerStream[MAX_STREAMS] = {};
    S32 CurrentDeintChannel = 0;
    for (S32 i = 0; i < NumBinkStreams; i++)
    {
        ChannelsPerStream[i] = (WavChannels - CurrentDeintChannel) > 1 ? 2 : 1;
        BytesPerStream[i] = ChannelsPerStream[i] * sizeof(S16) * SamplesPerChannel;
        SourceStreams[i] = (char*)MemAlloc(BytesPerStream[i]);

        S32 InputStride = sizeof(S16) * WavChannels;
        char* pInput = (char*)WavData + CurrentDeintChannel*sizeof(S16);
        char* pEnd = pInput + WavDataLen;
        char* pOutput = SourceStreams[i];

        if (ChannelsPerStream[i] == 2)
        {
            while (pInput != pEnd)
            {
                *(S32*)pOutput = *(S32*)pInput;
                pInput += InputStride;
                pOutput += 2*sizeof(S16);
            }
        }
        else
        {
            while (pInput != pEnd)
            {
                *(S16*)pOutput = *(S16*)pInput;
                pInput += InputStride;
                pOutput += sizeof(S16);
            }
        }
        CurrentDeintChannel += ChannelsPerStream[i];
    }

    //
    // We have a number of streams that need to be encoded.
    //
    char* StreamCursors[MAX_STREAMS];
    for (int i = 0; i < MAX_STREAMS; i++)
        StreamCursors[i] = SourceStreams[i];

    S32 StreamBytesGenerated[MAX_STREAMS] = {0};

    HBINKAUDIOCOMP hBink[MAX_STREAMS] = {0};
    for (S32 i = 0; i < NumBinkStreams; i++)
    {
        // the fn pointer cast is because on linux uintptr_t is unsigned long
        // when UINTa is unsigned long long. They have the same size, but are
        // technically different types.
        hBink[i] = BinkAudioCompressOpen(WavRate, ChannelsPerStream[i], BINKAC20, (BinkAudioCompressAllocFnType*)MemAlloc, MemFree);
    }

    SeekTableBuffer SeekTable = {};
    MemBuffer DataBuffer = {};
    DataBuffer.memalloc = MemAlloc;
    DataBuffer.memfree = MemFree;
    SeekTable.buffer.memalloc = MemAlloc;
    SeekTable.buffer.memfree = MemFree;

    U32 LastFrameLocation = 0;
    S32 MaxBlockSize = 0;

    for (;;)
    {
        void* InputBuffers[MAX_STREAMS];
        U32 InputLens[MAX_STREAMS];
        U32 OutputLens[MAX_STREAMS];
        void* OutputBuffers[MAX_STREAMS];
        U32 InputUseds[MAX_STREAMS];

        //
        // Run the compression for all of the streams at once.
        //
        U32 LimitedToSamples = ~0U;
        for (S32 StreamIndex = 0; StreamIndex < NumBinkStreams; StreamIndex++)
        {
            BinkAudioCompressLock(hBink[StreamIndex], InputBuffers + StreamIndex, InputLens + StreamIndex);

            // Copy only what we have remaining over. We could be zero filling at this point.
            U32 RemainingBytesInStream = (U32)(BytesPerStream[StreamIndex] - (StreamCursors[StreamIndex] - SourceStreams[StreamIndex]));

            U32 CopyAmount;
            {
                CopyAmount = InputLens[StreamIndex];
                if (RemainingBytesInStream < CopyAmount)
                    CopyAmount = RemainingBytesInStream;
            }

            memcpy(InputBuffers[StreamIndex], StreamCursors[StreamIndex], CopyAmount);

            // Zero the rest of the buffer, in case the last frame needs it.
            memset((char*)InputBuffers[StreamIndex] + CopyAmount, 0, InputLens[StreamIndex] - CopyAmount);

            // Do the actual compression.
            BinkAudioCompressUnlock(
                hBink[StreamIndex], 
                Quality, 
                InputLens[StreamIndex], 
                OutputBuffers + StreamIndex,
                OutputLens + StreamIndex,
                InputUseds + StreamIndex);
        }

        // Update cursors.
        S32 AllDone = 1;
        {
            for (S32 i = 0; i < NumBinkStreams; i++)
            {
                StreamCursors[i] += InputLens[i];
                if (StreamCursors[i] > SourceStreams[i] + BytesPerStream[i])
                    StreamCursors[i] = SourceStreams[i] + BytesPerStream[i];

                StreamBytesGenerated[i] += InputUseds[i];
                if (StreamBytesGenerated[i] < BytesPerStream[i]) 
                    AllDone = 0;
                else if (StreamBytesGenerated[i] > BytesPerStream[i])
                {
                    // We generated more samples that necessary - trim the
                    // block. We need to know how many of the samples this
                    // frame generated were valid - so subtract back off 
                    // to get where we ended last frame and take that from
                    // the len of the stream.
                    LimitedToSamples = (BytesPerStream[i] - (StreamBytesGenerated[i] - InputUseds[i])) >> 1;
                    if (ChannelsPerStream[i] == 2)
                        LimitedToSamples >>= 1;
                }
            }
        }

        // Figure how big this block is.
        S32 TotalBytesUsedForBlock = 0;
        for (S32 i = 0; i < NumBinkStreams; i++)
            TotalBytesUsedForBlock += OutputLens[i];

        // Write the header for the block.
        if (LimitedToSamples == ~0U)
        {
            U32 BlockHeader = (TotalBytesUsedForBlock << 16) | (0x9999);
            MemBufferAdd(&DataBuffer, &BlockHeader, 4);
        }
        else
        {
            U32 BlockHeader = 0xffff0000 | 0x9999;
            MemBufferAdd(&DataBuffer, &BlockHeader, 4);
            U32 LimitHeader = (LimitedToSamples << 16) | TotalBytesUsedForBlock;
            MemBufferAdd(&DataBuffer, &LimitHeader, 4);
        }

        // Write the compressed data.
        for (S32 i = 0; i < NumBinkStreams; i++)
            MemBufferAdd(&DataBuffer, OutputBuffers[i], OutputLens[i]);

        // Figure stats.
        if (TotalBytesUsedForBlock > MaxBlockSize) 
            MaxBlockSize = TotalBytesUsedForBlock;

        // Write the seek table to the actual output.
        if (GenerateSeekTable)
        {
            SeekTableBufferAdd(&SeekTable, (U16)(DataBuffer.total_bytes - LastFrameLocation));
            LastFrameLocation = DataBuffer.total_bytes;
        }

        if (AllDone) 
            break;
    }

    // Trim the table to 4k and get how many frames ended up per entry.
    U32 FramesPerEntry = SeekTableBufferTrim(&SeekTable);

    BinkAudioFileHeader Header;
    Header.tag = 'UEBA';
    Header.channels = (U8)WavChannels;
    Header.rate = WavRate;
    Header.sample_count = SamplesPerChannel;
    Header.max_comp_space_needed = (U16)MaxBlockSize;
    Header.flags = 1;
    Header.version = 1;
    Header.output_file_size = DataBuffer.total_bytes + sizeof(BinkAudioFileHeader) + SeekTable.total_count*sizeof(U16);
    Header.blocks_per_seek_table_entry = (U16)FramesPerEntry;
    Header.seek_table_entry_count = (U16)SeekTable.total_count;

    // Create the actual output buffer.
    char* Output = (char*)MemAlloc(Header.output_file_size);
    memcpy(Output, &Header, sizeof(BinkAudioFileHeader));
    memcpy(Output + sizeof(BinkAudioFileHeader), SeekTable.collapsed, SeekTable.total_count * sizeof(U16));
    MemBufferWriteBuffer(&DataBuffer, Output + sizeof(BinkAudioFileHeader) + SeekTable.total_count * sizeof(U16));

    MemBufferFree(&DataBuffer);
    SeekTableBufferFree(&SeekTable);
    for (S32 i = 0; i < NumBinkStreams; i++)
    {
        MemFree(SourceStreams[i]);
        BinkAudioCompressClose(hBink[i]);
    }

    *OutData = Output;
    *OutDataLen = Header.output_file_size;

    return BINKA_COMPRESS_SUCCESS;
}


