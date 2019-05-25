// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ADPCMAudioInfo.h"
#include "CoreMinimal.h"
#include "Interfaces/IAudioFormat.h"
#include "Sound/SoundWave.h"
#include "Audio.h"
#include "ContentStreaming.h"

#define WAVE_FORMAT_LPCM  1
#define WAVE_FORMAT_ADPCM 2

namespace ADPCM
{
	const uint32 MaxChunkSize = 256 * 1024;

	void DecodeBlock(const uint8* EncodedADPCMBlock, int32 BlockSize, int16* DecodedPCMData);
	void DecodeBlockStereo(const uint8* EncodedADPCMBlockLeft, const uint8* EncodedADPCMBlockRight, int32 BlockSize, int16* DecodedPCMData);
}

FADPCMAudioInfo::FADPCMAudioInfo(void)
	: DeinterleavedUncompressedAudio(nullptr)
	, SamplesPerBlock(0)
	, bSeekPending(false)
{
}

FADPCMAudioInfo::~FADPCMAudioInfo(void)
{
	if (DeinterleavedUncompressedAudio != nullptr)
	{
		FMemory::Free(DeinterleavedUncompressedAudio);
		DeinterleavedUncompressedAudio = nullptr;
	}
}

void FADPCMAudioInfo::SeekToTime(const float SeekTime)
{
	CurCompressedChunkData = nullptr;

	if (SeekTime <= 0.0f)
	{
		CurrentCompressedBlockIndex = 0;

		CurrentUncompressedFrameIndex = 0;
		CurrentChunkIndex = 0;
		CurrentChunkBufferOffset = 0;
		TotalFramesStreamed = 0;
		return;
	}

	// Calculate block index & force SeekTime to be in bounds.
	check(WaveInfo.pSamplesPerSec != nullptr);
	const uint32 SamplesPerSec = *WaveInfo.pSamplesPerSec;
	const uint32 SeekedSamples = static_cast<uint32>(SeekTime * SamplesPerSec);
	TotalFramesStreamed = FMath::Min<uint32>(SeekedSamples, TotalFrames - 1);

	const uint32 HeaderOffset = static_cast<uint32>(WaveInfo.SampleDataStart - SrcBufferData);

	if (Format == WAVE_FORMAT_ADPCM)
	{
		CurrentCompressedBlockIndex = TotalFramesStreamed / SamplesPerBlock; // Compute the block index that where SeekTime resides.
		CurrentChunkIndex = 0;
		CurrentChunkBufferOffset = HeaderOffset;

		const int32 ChannelBlockSize = BlockSize * NumChannels;
		for (uint32 BlockIndex = 0; BlockIndex < CurrentCompressedBlockIndex; ++BlockIndex)
		{
			if (CurrentChunkBufferOffset + ChannelBlockSize >= ADPCM::MaxChunkSize)
			{
				++CurrentChunkIndex;
				CurrentChunkBufferOffset = 0;
			}

			// Always add chunks in NumChannels pairs
			CurrentChunkBufferOffset += ChannelBlockSize;
		}
	}
	else if (Format == WAVE_FORMAT_LPCM)
	{
		const int32 ChannelBlockSize = sizeof(int16) * NumChannels;

		// 1. Find total offset
		CurrentChunkBufferOffset = HeaderOffset + (TotalFramesStreamed * ChannelBlockSize);

		// 2. Calculate index and remove from offset
		CurrentChunkIndex = CurrentChunkBufferOffset / ADPCM::MaxChunkSize;
		CurrentChunkBufferOffset %= ADPCM::MaxChunkSize;

		// 3. Trim remainder of block size, effectively aligning the block to a channel pair boundary
		CurrentChunkBufferOffset -= CurrentChunkBufferOffset % ChannelBlockSize;
	}
	else
	{
		return;
	}

	bSeekPending = true;
}

bool FADPCMAudioInfo::ReadCompressedInfo(const uint8* InSrcBufferData, uint32 InSrcBufferDataSize, struct FSoundQualityInfo* QualityInfo)
{
	if (!InSrcBufferData)
	{
		FString Name = QualityInfo ? QualityInfo->DebugName : TEXT("Unknown");
		UE_LOG(LogAudio, Warning, TEXT("Failed to read compressed ADPCM audio from ('%s') because there was no resource data."), *Name);

		return false;
	}

	SrcBufferData = InSrcBufferData;
	SrcBufferDataSize = InSrcBufferDataSize;

	void*	FormatHeader;

	if (!WaveInfo.ReadWaveInfo((uint8*)SrcBufferData, SrcBufferDataSize, nullptr, false, &FormatHeader))
	{
		UE_LOG(LogAudio, Warning, TEXT("WaveInfo.ReadWaveInfo Failed"));
		return false;
	}

	Format = *WaveInfo.pFormatTag;
	NumChannels = *WaveInfo.pChannels;

	if (Format == WAVE_FORMAT_ADPCM)
	{
		ADPCM::ADPCMFormatHeader* ADPCMHeader = (ADPCM::ADPCMFormatHeader*)FormatHeader;
		TotalFrames = ADPCMHeader->SamplesPerChannel;
		SamplesPerBlock = ADPCMHeader->wSamplesPerBlock;

		const uint32 PreambleSize = 7;
		BlockSize = *WaveInfo.pBlockAlign;

		// ADPCM starts with 2 uncompressed samples and then the remaining compressed sample data has 2 samples per byte
		const uint32 NumSamplesPerUncompressedChannel = (2 + (BlockSize - PreambleSize) * 2);
		NumBytesPerUncompressedChannel = NumSamplesPerUncompressedChannel * sizeof(int16);
		CompressedBlockSize = BlockSize;

		const uint32 TargetBlocks = MONO_PCM_BUFFER_SAMPLES / NumSamplesPerUncompressedChannel;
		StreamBufferSize = TargetBlocks * NumBytesPerUncompressedChannel;
		TotalDecodedSize = TotalFrames * NumChannels * sizeof(int16);

		DeinterleavedUncompressedAudio = (int16*)FMemory::Realloc(DeinterleavedUncompressedAudio, NumChannels * NumBytesPerUncompressedChannel);
		check(DeinterleavedUncompressedAudio != nullptr);
		TotalCompressedBlocksPerChannel = (WaveInfo.SampleDataSize + CompressedBlockSize - 1) / CompressedBlockSize / NumChannels;
	}
	else if (Format == WAVE_FORMAT_LPCM)
	{
		// There are no "blocks" in this case
		BlockSize = 0;
		NumBytesPerUncompressedChannel = 0;
		CompressedBlockSize = 0;
		StreamBufferSize = 0;
		DeinterleavedUncompressedAudio = nullptr;
		TotalCompressedBlocksPerChannel = 0;

		TotalDecodedSize = WaveInfo.SampleDataSize;
		TotalFrames = TotalDecodedSize / (sizeof(int16) * NumChannels);
	}
	else
	{
		return false;
	}

	if (QualityInfo)
	{
		QualityInfo->SampleRate = *WaveInfo.pSamplesPerSec;
		QualityInfo->NumChannels = *WaveInfo.pChannels;
		QualityInfo->SampleDataSize = TotalDecodedSize;
		QualityInfo->Duration = (float)TotalFrames / QualityInfo->SampleRate;
	}

	CurrentCompressedBlockIndex = 0;
	TotalFramesStreamed = 0;
	// This is set to the max value to trigger the decompression of the first audio block
	CurrentUncompressedFrameIndex = NumBytesPerUncompressedChannel / sizeof(uint16);
	return true;
}

bool FADPCMAudioInfo::ReadCompressedData(uint8* Destination, bool bLooping, uint32 BufferSize)
{
	// This correctly handles any BufferSize as long as its a multiple of sample size * number of channels
	check(Destination);
	check((BufferSize % (sizeof(uint16) * NumChannels)) == 0);
	
	int16* OutData = (int16*)Destination;
	
	bool ReachedEndOfSamples = false;

	const int32 NumSamplesPerUncompressedChannel = NumBytesPerUncompressedChannel / sizeof(int16);

	if (Format == WAVE_FORMAT_ADPCM)
	{
		int32 NumFramesToOutput = BufferSize / (NumChannels * sizeof(int16));

		// We need to loop over the number of samples requested since an uncompressed block will not match the number of frames requested
		while (NumFramesToOutput > 0)
		{
			if (CurrentUncompressedFrameIndex >= NumSamplesPerUncompressedChannel)
			{
				// we need to decompress another block of compressed data from the current chunk
				check(CurrentCompressedBlockIndex < TotalCompressedBlocksPerChannel);
				// Decompress one block for each channel and store it in UncompressedBlockData
				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
				{
					const int32 CompressedAudioByteOffset = (ChannelIndex * TotalCompressedBlocksPerChannel + CurrentCompressedBlockIndex) * CompressedBlockSize;
					const int32 OffsetIntoUncompressedBuffer = ChannelIndex * NumSamplesPerUncompressedChannel;

					ADPCM::DecodeBlock(
						&WaveInfo.SampleDataStart[CompressedAudioByteOffset],
						CompressedBlockSize,
						&DeinterleavedUncompressedAudio[OffsetIntoUncompressedBuffer]);
				}

				// Move on to the next compressed block:
				CurrentUncompressedFrameIndex = 0;
				++CurrentCompressedBlockIndex;
			}

			// Only copy over the number of samples we currently have available, we will loop around if needed
			int32 NumFramesToInterleave = FMath::Min<int32>(NumSamplesPerUncompressedChannel - CurrentUncompressedFrameIndex, NumFramesToOutput);

			check(NumFramesToInterleave > 0);
			const int32 NumFramesLeft = TotalFrames - TotalFramesStreamed;
			NumFramesToInterleave = FMath::Clamp<int32>(NumFramesToInterleave, 0, NumFramesLeft);

			// Interleave audio from DeinterleavedUncompressedAudio into OutData:
			for (int32 FrameIndex = 0; FrameIndex < NumFramesToInterleave; ++FrameIndex)
			{
				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
				{
					const int32 OffsetIntoDeinterleavedBuffer = ChannelIndex * NumSamplesPerUncompressedChannel + CurrentUncompressedFrameIndex + FrameIndex;
					*OutData++ = DeinterleavedUncompressedAudio[OffsetIntoDeinterleavedBuffer];
				}
			}

			// Update bookkeeping
			CurrentUncompressedFrameIndex += NumFramesToInterleave;
			NumFramesToOutput -= NumFramesToInterleave;
			TotalFramesStreamed += NumFramesToInterleave;
			
			// Check for the end of the audio samples and loop if needed
			if (TotalFramesStreamed >= TotalFrames || CurrentCompressedBlockIndex >= TotalCompressedBlocksPerChannel)
			{
				ReachedEndOfSamples = true;
				// This is set to the max value to trigger the decompression of the first audio block
				CurrentUncompressedFrameIndex = NumBytesPerUncompressedChannel / sizeof(int16);
				CurrentCompressedBlockIndex = 0;
				TotalFramesStreamed = 0;
				if (!bLooping)
				{
					// Set the remaining buffer to 0
					FMemory::Memzero(OutData, NumFramesToOutput * sizeof(int16) * NumChannels);
					return true;
				}
			}
		}
	}
	else
	{
		// For LPCM, we simply copy the audio from WaveInfo.SampleDataStart into OutData.
		uint32	NumFramesToCopyToOutData = BufferSize / (sizeof(int16) * NumChannels);
			
		// Ensure we don't read past the number of frames left in the compressed audio buffer.
		const int32 NumFramesLeftInBuffer = TotalFrames - TotalFramesStreamed;
		NumFramesToCopyToOutData = FMath::Clamp<int32>(NumFramesToCopyToOutData, 0, NumFramesLeftInBuffer);

		int32 OffsetIntoCompressedAudio = TotalFramesStreamed * sizeof(int16) * NumChannels;
		FMemory::Memcpy(OutData, &WaveInfo.SampleDataStart[OffsetIntoCompressedAudio], NumFramesToCopyToOutData * sizeof(int16) * NumChannels);
		TotalFramesStreamed += NumFramesToCopyToOutData;
		BufferSize -= NumFramesToCopyToOutData * sizeof(int16) * NumChannels;
		
		// Check for the end of the audio samples and loop if needed
		if (TotalFramesStreamed >= TotalFrames)
		{
			ReachedEndOfSamples = true;
			TotalFramesStreamed = 0;
			if (!bLooping)
			{
				// Set the remaining buffer to 0
				FMemory::Memzero(OutData, BufferSize);
				return true;
			}
		}
	}

	return ReachedEndOfSamples;
}

void FADPCMAudioInfo::ExpandFile(uint8* DstBuffer, struct FSoundQualityInfo* QualityInfo)
{
	check(DstBuffer);

	ReadCompressedData(DstBuffer, false, TotalDecodedSize);
}

int FADPCMAudioInfo::GetStreamBufferSize() const
{
	return StreamBufferSize;
}

bool FADPCMAudioInfo::StreamCompressedInfoInternal(USoundWave* Wave, struct FSoundQualityInfo* QualityInfo)
{
	check(QualityInfo);

	check(StreamingSoundWave == Wave);

	// Get the first chunk of audio data (should already be loaded)
	uint8 const* FirstChunk = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(Wave, 0, &CurrentChunkDataSize);

	if (FirstChunk == nullptr)
	{
		return false;
	}

	SrcBufferData = nullptr;
	SrcBufferDataSize = 0;

	void* FormatHeader;

	if (!WaveInfo.ReadWaveInfo((uint8*)FirstChunk, Wave->RunningPlatformData->Chunks[0].AudioDataSize, nullptr, true, &FormatHeader))
	{
		UE_LOG(LogAudio, Warning, TEXT("WaveInfo.ReadWaveInfo Failed"));
		return false;
	}

	SrcBufferData = FirstChunk;

	FirstChunkSampleDataOffset = WaveInfo.SampleDataStart - FirstChunk;
	CurrentChunkBufferOffset = 0;
	CurCompressedChunkData = nullptr;
	CurrentUncompressedFrameIndex = 0;
	CurrentChunkIndex = 0;
	TotalFramesStreamed = 0;
	Format = *WaveInfo.pFormatTag;
	NumChannels = *WaveInfo.pChannels;

	if (Format == WAVE_FORMAT_ADPCM)
	{
		ADPCM::ADPCMFormatHeader* ADPCMHeader = (ADPCM::ADPCMFormatHeader*)FormatHeader;
		TotalFrames = ADPCMHeader->SamplesPerChannel;
		SamplesPerBlock = ADPCMHeader->wSamplesPerBlock;

		const uint32 PreambleSize = 7;

		BlockSize = *WaveInfo.pBlockAlign;
		NumBytesPerUncompressedChannel = (2 + (BlockSize - PreambleSize) * 2) * sizeof(int16);
		CompressedBlockSize = BlockSize;

		// Calculate buffer sizes and total number of samples
		const uint32 NumUncompressedSamplesPerBlock = (2 + (BlockSize - PreambleSize) * 2);
		const uint32 TargetNumBlocks = MONO_PCM_BUFFER_SAMPLES / NumUncompressedSamplesPerBlock;
		StreamBufferSize = TargetNumBlocks * NumBytesPerUncompressedChannel;
		TotalDecodedSize = ((WaveInfo.SampleDataSize + CompressedBlockSize - 1) / CompressedBlockSize) * NumBytesPerUncompressedChannel;

		DeinterleavedUncompressedAudio = (int16*)FMemory::Realloc(DeinterleavedUncompressedAudio, NumChannels * NumBytesPerUncompressedChannel);
		check(DeinterleavedUncompressedAudio != nullptr);
	}
	else if (Format == WAVE_FORMAT_LPCM)
	{
		BlockSize = 0;
		NumBytesPerUncompressedChannel = 0;
		CompressedBlockSize = 0;

		// This is uncompressed, so decoded size and buffer size are the same
		TotalDecodedSize = WaveInfo.SampleDataSize;
		StreamBufferSize = WaveInfo.SampleDataSize;
		TotalFrames = StreamBufferSize / sizeof(uint16) / NumChannels;
	}
	else
	{
		UE_LOG(LogAudio, Error, TEXT("Unsupported wave format"));
		return false;
	}

	if (QualityInfo)
	{
		QualityInfo->SampleRate = *WaveInfo.pSamplesPerSec;
		QualityInfo->NumChannels = *WaveInfo.pChannels;
		QualityInfo->SampleDataSize = TotalDecodedSize;
		QualityInfo->Duration = (float)TotalFrames / QualityInfo->SampleRate;
	}

	return true;
}

bool FADPCMAudioInfo::StreamCompressedData(uint8* Destination, bool bLooping, uint32 BufferSize)
{
	// Destination samples are interlaced by channel, BufferSize is in bytes
	const int32 ChannelSampleSize = sizeof(uint16) * NumChannels;

	// Ensure that BuffserSize is a multiple of the sample size times the number of channels
	checkf((BufferSize % ChannelSampleSize) == 0, TEXT("Invalid buffer size %d requested for %d channels"), BufferSize, NumChannels);
	
	// If UncompressedBlockData is NULL, we haven't called FADPCM::StreamCompressedInfo yet.
	if (DeinterleavedUncompressedAudio == nullptr || Destination == nullptr)
	{
		return false;
	}

	int16* OutData = (int16*)Destination;

	int32 NumFramesLeftInOutData = BufferSize / (sizeof(int16) * NumChannels);
	bool ReachedEndOfSamples = false;

	if (Format == WAVE_FORMAT_ADPCM)
	{
		// We need to loop over the number of samples requested since an uncompressed block will not match the number of frames requested
		while (NumFramesLeftInOutData > 0)
		{
			const int32 NumSamplesPerUncompressedChannel = NumBytesPerUncompressedChannel / sizeof(uint16);
			if (CurCompressedChunkData == nullptr || CurrentUncompressedFrameIndex >= NumSamplesPerUncompressedChannel)
			{
				// we need to decompress another block of compressed data from the current chunk

				if (CurCompressedChunkData == nullptr || CurrentChunkBufferOffset >= CurrentChunkDataSize)
				{
					// Get another chunk of compressed data from the streaming engine

					// CurrentChunkIndex is used to keep track of the current chunk for loading/unloading by the streaming engine, but chunk 0 is
					// preloaded so don't increment this when getting chunk 0, only later chunks. If previous chunk retrieval failed because it was
					// not ready, don't re-increment the CurrentChunkIndex.  Only increment if seek not pending as seek determines the current chunk index
					// and invalidates CurCompressedChunkData.
					if (CurCompressedChunkData != nullptr)
					{
						++CurrentChunkIndex;
					}

					// Request the next chunk of data from the streaming engine
					CurCompressedChunkData = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(StreamingSoundWave, CurrentChunkIndex, &CurrentChunkDataSize);

					if (CurCompressedChunkData == nullptr)
					{
						// We only need to worry about missing the stream chunk if we were seeking. Seeking might cause a bit of latency with chunk loading. That is expected.
						if (!bSeekPending)
						{
							// If we did not get it then just bail, CurrentChunkIndex will not get incremented on the next callback so in effect another attempt will be made to fetch the chunk
							// Since audio streaming depends on the general data streaming mechanism used by other parts of the engine and new data is prefectched on the game tick thread its possible a game hickup can cause this
							UE_LOG(LogAudio, Verbose, TEXT("Didn't load chunk %d in time for playback!"), CurrentChunkIndex);
						}

						// zero out remaining data and bail
						FMemory::Memzero(OutData, NumFramesLeftInOutData * NumChannels * sizeof(int16));
						return false;
					}

					// Set the current buffer offset accounting for the header in the first chunk
					if (!bSeekPending)
					{
						CurrentChunkBufferOffset = CurrentChunkIndex == 0 ? FirstChunkSampleDataOffset : 0;
					}
					bSeekPending = false;
				}

				// Decompress one block for each channel and store it in UncompressedBlockData
				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
				{
					const int32 CompressedBufferOffset = CurrentChunkBufferOffset + ChannelIndex * CompressedBlockSize;
					const int32 DeinterleavedUncompressedBufferOffset = ChannelIndex * NumSamplesPerUncompressedChannel;
					ADPCM::DecodeBlock(
						&CurCompressedChunkData[CompressedBufferOffset],
						CompressedBlockSize,
						&DeinterleavedUncompressedAudio[DeinterleavedUncompressedBufferOffset]);
				}

				// Update some bookkeeping
				CurrentUncompressedFrameIndex = 0;
				CurrentChunkBufferOffset += NumChannels * CompressedBlockSize;
			}

			// Only copy over the number of samples we currently have available, we will loop around if needed
			int32 NumFramesToInterleave = FMath::Min<int32>(NumSamplesPerUncompressedChannel - CurrentUncompressedFrameIndex, NumFramesLeftInOutData);
			check(NumFramesToInterleave > 0);
			
			// Ensure we don't go over the number of samples left in the audio data
			const int32 FramesLeft = TotalFrames - TotalFramesStreamed;
			NumFramesToInterleave = FMath::Clamp<int32>(NumFramesToInterleave, 0, FramesLeft);

			// Interleave audio from DeinterleavedUncompressedAudio
			for (int32 FrameIndex = 0; FrameIndex < NumFramesToInterleave; ++FrameIndex)
			{
				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
				{
					const int32 OffsetInDeinterleavedBuffer = ChannelIndex * NumSamplesPerUncompressedChannel + (CurrentUncompressedFrameIndex + FrameIndex);
					*OutData++ = DeinterleavedUncompressedAudio[OffsetInDeinterleavedBuffer];
				}
			}

			// Update bookkeeping
			CurrentUncompressedFrameIndex += NumFramesToInterleave;
			NumFramesLeftInOutData -= NumFramesToInterleave;
			TotalFramesStreamed += NumFramesToInterleave;
			
			// Check for the end of the audio samples and loop if needed
			if (TotalFramesStreamed >= TotalFrames)
			{
				ReachedEndOfSamples = true;
				CurrentUncompressedFrameIndex = 0;
				CurrentChunkIndex = 0;
				CurrentChunkBufferOffset = 0;
				TotalFramesStreamed = 0;
				CurCompressedChunkData = nullptr;
				if (!bLooping)
				{
					// Set the remaining buffer to 0
					FMemory::Memzero(OutData, NumFramesLeftInOutData * NumChannels * sizeof(int16));
					return true;
				}
			}
		}
	}
	else
	{
		while (NumFramesLeftInOutData > 0)
		{
			if (CurCompressedChunkData == nullptr || CurrentChunkBufferOffset >= CurrentChunkDataSize)
			{
				// Get another chunk of compressed data from the streaming engine

				// CurrentChunkIndex is used to keep track of the current chunk for loading/unloading by the streaming engine but chunk 0
				// is preloaded so we don't want to increment this when getting chunk 0, only later chunks
				// Also, if we failed to get a previous chunk because it was not ready we don't want to re-increment the CurrentChunkIndex
				if (CurCompressedChunkData != nullptr)
				{
					++CurrentChunkIndex;
				}

				// Request the next chunk of data from the streaming engine
				CurCompressedChunkData = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(StreamingSoundWave, CurrentChunkIndex, &CurrentChunkDataSize);

				if (CurCompressedChunkData == nullptr)
				{
					// Only report missing the stream chunk if we were seeking. Seeking
					// may cause a bit of latency with chunk loading, which is expected.
					if (!bSeekPending)
					{
						// CurrentChunkIndex will not get incremented on the next callback, effectively causing another attempt to fetch the chunk.
						// Since audio streaming depends on the general data streaming mechanism used by other parts of the engine and new data is
						// prefetched on the game tick thread, this may be caused by a game hitch.
						UE_LOG(LogAudio, Warning, TEXT("Didn't load chunk %d in time for playback!"), CurrentChunkIndex);
					}

					FMemory::Memzero(OutData, NumFramesLeftInOutData * NumChannels * sizeof(int16));
					return false;
				}

				// Set the current buffer offset accounting for the header in the first chunk
				if (!bSeekPending)
				{
					CurrentChunkBufferOffset = CurrentChunkIndex == 0 ? FirstChunkSampleDataOffset : 0;
				}
				bSeekPending = false;
			}

			int32 NumFramesToInterleave = FMath::Min<int32>((CurrentChunkDataSize - CurrentChunkBufferOffset), NumFramesLeftInOutData);
			check(NumFramesToInterleave > 0);
			
			// Ensure we don't go over the number of samples left in the audio data
			const int32 FramesLeft = TotalFrames - TotalFramesStreamed;
			check(FramesLeft > 0);
			FMath::Clamp<int32>(NumFramesToInterleave, 0, FramesLeft);

			FMemory::Memcpy(OutData, &CurCompressedChunkData[CurrentChunkBufferOffset], NumFramesToInterleave * (sizeof(int16) * NumChannels));
			
			OutData += NumFramesToInterleave * NumChannels;
			CurrentChunkBufferOffset += NumFramesToInterleave * (sizeof(uint16) * NumChannels);
			BufferSize -= NumFramesToInterleave * (sizeof(uint16) * NumChannels);
			TotalFramesStreamed += NumFramesToInterleave;
			
			// Check for the end of the audio samples and loop if needed
			if (TotalFramesStreamed >= TotalFrames)
			{
				ReachedEndOfSamples = true;
				CurrentChunkIndex = 0;
				CurrentChunkBufferOffset = 0;
				TotalFramesStreamed = 0;
				CurCompressedChunkData = nullptr;
				if (!bLooping)
				{
					// Set the remaining buffer to 0
					FMemory::Memzero(OutData, NumFramesLeftInOutData * NumChannels * sizeof(int16));
					return true;
				}
			}
		}
	}

	return ReachedEndOfSamples;
}
