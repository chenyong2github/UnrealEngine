// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRivermaxOutputStream.h"

#include "HAL/Runnable.h"
#include "RivermaxHeader.h"
#include "RivermaxOutputFrame.h"
#include "RivermaxTypes.h"


class FEvent;

namespace UE::RivermaxCore::Private
{
	using UE::RivermaxCore::FRivermaxStreamOptions;
	using UE::RivermaxCore::FRivermaxOutputVideoFrameInfo;
	
	// Todo make a proper RTP header struct
	struct FRTPHeader
	{
		uint8 RawHeader[20];
	};

	struct FRivermaxOutputStreamMemory
	{
		uint16 PayloadSize = 0; //todo
		uint32 DataStrideSize = 1280; //todo align cache line?
		uint32 HeaderStrideSize = 20; //todo align cache line?
		uint32 LinesInChunk = 4;

		uint32 PacketsInLine = 0;
		uint32 ChunkSizeInStrides = LinesInChunk * PacketsInLine;

		uint32 FramesFieldPerMemoryBlock = 1;
		uint32 PacketsInFrameField = 0;
		uint32 PacketsPerMemoryBlock = PacketsInFrameField * FramesFieldPerMemoryBlock;
		uint32 ChunksPerFrameField = FMath::CeilToInt32(PacketsInFrameField / (double)ChunkSizeInStrides);
		uint32 ChunksPerMemoryBlock = FramesFieldPerMemoryBlock * ChunksPerFrameField;
		uint32 MemoryBlockCount = 10; //todo
		uint32 StridesPerMemoryBlock = ChunkSizeInStrides * ChunksPerMemoryBlock;

		TArray<rmax_mem_block> MemoryBlocks;
		TArray<uint16_t> PayloadSizes; //Array describing stride payload size
		TArray<uint16_t> HeaderSizes; //Array describing header payload size
		TArray<FRTPHeader> RTPHeaders;

		rmax_buffer_attr BufferAttributes;
	};

	struct FRivermaxOutputStreamStats
	{
		uint32 ChunkRetries = 0;
		uint32 TotalStrides = 0;
		uint32 ChunkWait = 0;
		uint32 CommitWaits = 0;
		uint32 CommitRetries = 0;
		uint64 MemoryBlockSentCounter = 0; //Global for an active capture session to track timestamp for next packet
	};

	struct FRivermaxOutputStreamData
	{
		/** Current sequence number being done */
		uint32 SequenceNumber = 0;
		double FrameFieldTimeIntervalNs = 0.0;
		double StartSendTimeNs = 0.0;
		double SendTimeNs = 0.0;
		double InitialTimestampTick = 0.0;
		bool bHasFrameFirstChunkBeenFetched = false;
	};


	class FRivermaxOutputStream : public UE::RivermaxCore::IRivermaxOutputStream, public FRunnable
	{
	public:
		FRivermaxOutputStream();
		virtual ~FRivermaxOutputStream();

	public:

		//~ Begin IRivermaxOutputStream interface
		virtual bool Initialize(const FRivermaxStreamOptions& Options, IRivermaxOutputStreamListener& InListener) override;
		virtual void Uninitialize() override;
		virtual bool PushVideoFrame(const FRivermaxOutputVideoFrameInfo& NewFrame) override;
		//~ End IRivermaxOutputStream interface

		void Process_AnyThread();

		//~ Begin FRunnable interface
		virtual bool Init() override;
		virtual uint32 Run() override;
		virtual void Stop() override;
		virtual void Exit() override;
		//~ End FRunnable interface

	private:
		void InitializeBuffers();
		void InitializeMemory();
		void InitializeNextFrame(const TSharedPtr<FRivermaxOutputFrame>& NextFrame);
		TSharedPtr<FRivermaxOutputFrame> GetNextFrameToSend();
		TSharedPtr<FRivermaxOutputFrame> GetNextAvailableFrame(uint32 InFrameIdentifier);
		void BuildRTPHeader(FRTPHeader& OutHeader) const;
		void DestroyStream();
		void WaitForNextRound(double NextRoundTime);
		void GetNextChunk();
		void SetupRTPHeaders();
		void CommitNextChunks();

	private:
		FRivermaxStreamOptions Options;
		FRivermaxOutputStreamMemory StreamMemory;
		FRivermaxOutputStreamStats Stats;
		FRivermaxOutputStreamData StreamData;

		rmax_stream_id StreamId;
		FCriticalSection FrameCriticalSection;

		TSharedPtr<FRivermaxOutputFrame> CurrentFrame;

		TArray<TSharedPtr<FRivermaxOutputFrame>> AvailableFrames;
		TArray<TSharedPtr<FRivermaxOutputFrame>> FramesToSend;
		double LastTime = 0.0;

		TUniquePtr<FRunnableThread> RivermaxThread;
		std::atomic<bool> bIsActive;

		FEvent* ReadyToSendEvent = nullptr;

		IRivermaxOutputStreamListener* Listener = nullptr;

		static constexpr double MediaClockSampleRate = 90000.0; //Required to comply with SMTPE 2110-10.The Media Clock and RTP Clock rate for streams compliant to this standard shall be 90 kHz.
		ERivermaxStreamType StreamType = ERivermaxStreamType::VIDEO_2110_20_STREAM; //todo
	};
}


