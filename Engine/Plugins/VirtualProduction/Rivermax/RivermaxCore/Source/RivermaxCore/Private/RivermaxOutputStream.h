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
		uint16 PayloadSize = 0;
		uint32 PixelGroupPerPacket = 0;
		uint32 PixelsPerPacket = 0;
		uint32 PixelsPerFrame = 0;

		uint32 HeaderStrideSize = 20;
		uint32 LinesInChunk = 4;

		uint32 PacketsInLine = 0;
		uint32 ChunkSizeInStrides = 0;

		uint32 FramesFieldPerMemoryBlock = 0;
		uint32 PacketsPerFrame = 0;
		uint32 PacketsPerMemoryBlock = 0;
		uint32 ChunksPerFrameField = 0;
		uint32 ChunksPerMemoryBlock = 0;
		uint32 MemoryBlockCount = 0; 
		uint32 StridesPerMemoryBlock = 0;

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
		uint32 CommitImmediate = 0;
		uint64 MemoryBlockSentCounter = 0;
	};

	struct FRivermaxOutputStreamData
	{
		/** Current sequence number being done */
		uint32 SequenceNumber = 0;
		double FrameFieldTimeIntervalNs = 0.0;

		/** Used to detect misalignment between chunk being sent and frame memory we are writing in */
		bool bHasFrameFirstChunkBeenFetched = false;

		/** Next alignment point based on PTP standard */
		uint64 NextAlignmentPointNanosec = 0;

		/** Next schedule time using 2110 gapped model timing and controllable offset */
		uint64 NextScheduleTimeNanosec = 0;

		/** Whether next alignment frame number is deemed valid or not to detect missed frames. */
		bool bHasValidNextFrameNumber = false;
		
		/** Next alignment point frame number treated to detect missed frames */
		uint64 NextAlignmentPointFrameNumber = 0;
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
		bool InitializeMemory();
		void InitializeNextFrame(const TSharedPtr<FRivermaxOutputFrame>& NextFrame);
		TSharedPtr<FRivermaxOutputFrame> GetNextFrameToSend();
		TSharedPtr<FRivermaxOutputFrame> GetNextAvailableFrame(uint32 InFrameIdentifier);
		void BuildRTPHeader(FRTPHeader& OutHeader) const;
		void DestroyStream();
		void WaitForNextRound();
		void GetNextChunk();
		void SetupRTPHeaders();
		void CommitNextChunks();
		void PrepareNextFrame();
		void InitializeStreamTimingSettings();
		void ShowStats();
		uint32 GetTimestampFromTime(uint64 InTimeNanosec, double InMediaClockRate) const;

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

		TUniquePtr<FRunnableThread> RivermaxThread;
		std::atomic<bool> bIsActive;

		FEvent* ReadyToSendEvent = nullptr;

		IRivermaxOutputStreamListener* Listener = nullptr;

		static constexpr double MediaClockSampleRate = 90000.0; //Required to comply with SMTPE 2110-10.The Media Clock and RTP Clock rate for streams compliant to this standard shall be 90 kHz.
		ERivermaxStreamType StreamType = ERivermaxStreamType::VIDEO_2110_20_STREAM; //todo

		/** TRoffset time calculated based on ST2110 - 21 Gapped(for now) method. This is added to next alignment point */
		uint64 TransmitOffsetNanosec = 0;

		/** Format info for the active stream */
		FVideoFormatInfo FormatInfo;

		/** Timestamp at which we logged stats */
		double LastStatsShownTimestamp = 0.0;
	};
}


