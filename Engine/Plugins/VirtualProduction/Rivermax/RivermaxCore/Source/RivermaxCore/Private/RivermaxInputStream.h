// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRivermaxInputStream.h"

#include <atomic>
#include "Async/Future.h"
#include "HAL/Runnable.h"
#include "RHI.h"
#include "RivermaxHeader.h"
#include "RivermaxTypes.h"



namespace UE::RivermaxCore::Private
{
	using UE::RivermaxCore::IRivermaxInputStream;
	using UE::RivermaxCore::IRivermaxInputStreamListener;
	using UE::RivermaxCore::FRivermaxStreamOptions;

	struct FRawRTPHeader;
	struct FRTPHeader;
	
	struct FInputStreamBufferConfiguration
	{
		size_t PayloadSize = 0;
		size_t HeaderSize = 0;
		uint16 PayloadExpectedSize = 1500;
		uint16 HeaderExpectedSize = 20; //for 2110

		rmax_in_memblock DataMemory;
		rmax_in_memblock HeaderMemory;
	};

	struct FInputStreamStats
	{
		uint64 InvalidHeadercount = 0;
		uint64 FramePacketLossCount = 0;
		uint64 TotalPacketLossCount = 0;
		uint64 BiggerFramesCount = 0;
		uint64 InvalidFramesCount = 0;
		uint64 BytesReceived = 0;
		uint64 FramesReceived = 0;
		uint64 ChunksReceived = 0;
		uint64 EndOfFrameReceived = 0;
		uint64 EmptyCompletionCount = 0;
	};

	struct FInputStreamData
	{
		uint64 LastSequenceNumber = 0;
		void* CurrentFrame = nullptr;
		uint32 WritingOffset = 0;
		uint32 ReceivedSize = 0;
		uint32 ExpectedSize = 0;

		// GPUDirect data handling. Rivermax uses a cyclic buffer so we might have part of the frame at the end of the first buffer and the rest on the second part.
		void* DeviceWritePointerOne = nullptr;
		void* DeviceWritePointerTwo = nullptr;
		uint32 SizeToWriteOne = 0;
		uint32 SizeToWriteTwo = 0;
	};

	class FRivermaxInputStream : public IRivermaxInputStream, public FRunnable
	{
	public:
		FRivermaxInputStream();
		virtual ~FRivermaxInputStream();

	public:

		//~ Begin IRivermaxInputStreamListener interface
		virtual bool Initialize(const FRivermaxStreamOptions& InOptions, IRivermaxInputStreamListener& InListener) override;
		virtual void Uninitialize() override;
		//~ End IRivermaxInputStreamListener interface 

		void Process_AnyThread();

		//~ Begin FRunnable interface
		virtual bool Init() override;
		virtual uint32 Run() override;
		virtual void Stop() override;
		virtual void Exit() override;
		//~ End FRunnable interface

	private:

		bool TranslateRTPHeader(const FRawRTPHeader& RawHeader, FRTPHeader& OutParameter);
		void ParseChunks(const rmax_in_completion& Completion);
		void ProcessSRD(const FRTPHeader& RTPHeader, uint8* DataPtr);
		void ProcessLastSRD(const FRTPHeader& RTPHeader, uint8* DataPtr);
		void PrepareNextFrame();
		void LogStats();
		void AllocateBuffers();
		bool AllocateGPUBuffers();
		void DeallocateBuffers();
		void* GetMappedBuffer(const FBufferRHIRef& InBuffer);

	private:



		FRivermaxStreamOptions Options;

		FRunnableThread* RivermaxThread = nullptr;
		std::atomic<bool> bIsActive;

		rmax_stream_id StreamId = 0;
		rmax_in_flow_attr FlowAttribute;
		FInputStreamBufferConfiguration BufferConfiguration;
		bool bIsFirstFrameReceived = false;
		bool bIsFirstPacketReceived = false;
		ERivermaxStreamType RivermaxStreamType = ERivermaxStreamType::VIDEO_2110_20_STREAM;
		FInputStreamData StreamData;
		FInputStreamStats StreamStats;
		IRivermaxInputStreamListener* Listener;
		std::atomic<bool> bIsShuttingDown;
		TFuture<void> InitTaskFuture;
		double LastLoggingTimestamp = 0.0;
		FVideoFormatInfo FormatInfo;
		bool bIsUsingGPUDirect = false;
		int32 DeviceIndex = 0;

		/** Allocated memory base address used when it's time to free */
		void* GPUAllocatedMemoryBaseAddress = nullptr;

		/** Total allocated gpu memory. */
		int32 GPUAllocatedMemorySize = 0;

		/** Map between buffer we are writing to and their mapped address in cuda space */
		TMap<FRHIBuffer*, void*> BufferGPUMemoryMap;

		/** Used to wait for a frame to be copied from gpu to gpu memory */
		struct FCallbackPayload
		{
			volatile bool bIsWaitingForPendingCopy = false;
		};
		TSharedPtr<FCallbackPayload> CallbackPayload;

		/** Cuda stream used for our operations */ 
		void* GPUStream = nullptr;

		/** Payload size used to initialize rivermax stream and know differences between received SRD and config */
		uint32 ExpectedPayloadSize = 1500;

		/** Used to track SRD length received and detect a change across a frame, which doesn't work with gpudirect */
		TOptional<uint16> LastSRDLength = 0;
	};
}


