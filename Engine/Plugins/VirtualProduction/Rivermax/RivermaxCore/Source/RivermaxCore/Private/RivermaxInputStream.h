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
	using UE::RivermaxCore::FRivermaxInputStreamOptions;

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
		const void* DeviceWritePointerOne = nullptr;
		const void* DeviceWritePointerTwo = nullptr;
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
		virtual bool Initialize(const FRivermaxInputStreamOptions& InOptions, IRivermaxInputStreamListener& InListener) override;
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

		/** Process reception state. i.e Extract RTP, tracks data received, etc... */
		void FrameReceptionState(const FRTPHeader& RTPHeader, const uint8* DataPtr);

		/** State where we just wait to receive an end of frame in RTP. Used to align for the first frame and when an error occurs during reception */
		void WaitForMarkerState(const FRTPHeader& RTPHeader);

		/** 
		 * State we go in when an error was detected during the reception of a frame.  
		 * Once the end of frame is received, will go back to frame reception state.
		 */
		void FrameErrorState(const FRTPHeader& RTPHeader);

		/** Extract raw RTP header data, in network endianness to our own intermediate data structure */
		bool TranslateRTPHeader(const FRawRTPHeader& RawHeader, FRTPHeader& OutParameter);

		/** 
		 * Iterates over each chunks received in the Completion data structure.
		 * For each item, its header is parsed and validated before processing data it contains
		 */
		void ParseChunks(const rmax_in_completion& Completion);

		/** Process a SRD / packet by copying data associated to it into output buffer or tracking memory received for gpudirect */
		void ProcessSRD(const FRTPHeader& RTPHeader, const uint8* DataPtr);

		/** Handles wrapping up of a frame when marker bit is detected (last SRD). Will notify listener of a new frame received */
		void ProcessLastSRD(const FRTPHeader& RTPHeader, const uint8* DataPtr);

		/** Requests a new frame from our listener. If it fails, it will fall in error state waiting for the next one */
		bool PrepareNextFrame(const FRTPHeader& RTPHeader);

		/** If enabled, will printout reception stats to logs */
		void LogStats();

		/** Allocates system memory buffer used for reception and used by rivermax */
		void AllocateBuffers();

		/** Allocates gpu memory buffer used for reception and used by rivermax */
		bool AllocateGPUBuffers();

		/** Cleans up allocated buffers */
		void DeallocateBuffers();

		/** Gets the mapped gpu buffer for the incoming RHI buffer. Caching will occur if a new buffer is given */
		void* GetMappedBuffer(const FBufferRHIRef& InBuffer);

	private:

		/** Options used for this stream, such has resolution, frame rate etc... */
		FRivermaxInputStreamOptions Options;

		/** Thread on which reception is handled */
		FRunnableThread* RivermaxThread = nullptr;

		/** Whether reception thread should keep running */
		std::atomic<bool> bIsActive = false;

		/** Stream id given back by rivermax */
		rmax_stream_id StreamId = 0;

		/** Flow attributes used at stream creation. Used to compare incoming data to our flow and detect mismatches */
		rmax_in_flow_attr FlowAttribute;

		/** Memory configuration used for this stream. */
		FInputStreamBufferConfiguration BufferConfiguration;

		/** Used to know when the first packet of a frame has been received. Used mostly for visualization in insights */
		bool bIsFirstPacketReceived = false;

		/** Type of stream we are creating. Only 2110-20 (video) supported at the moment */
		ERivermaxStreamType RivermaxStreamType = ERivermaxStreamType::VIDEO_2110_20_STREAM;

		/** Information about the incoming frame we are receiving and general sequence numbering we are using for packet loss */
		FInputStreamData StreamData;

		/** Various stats kept for this stream */
		FInputStreamStats StreamStats;

		/** Creator / listener of this stream */
		IRivermaxInputStreamListener* Listener;
		
		/** True when player has requested stream to shutdown */
		std::atomic<bool> bIsShuttingDown = false;

		/** Future used for the initialization task */
		TFuture<void> InitTaskFuture;

		/**  Timestamp used to know when to print out stats */
		double LastLoggingTimestamp = 0.0;

		/** Info about the video format that was selected such as pixel group, group size etc */
		FVideoFormatInfo FormatInfo;
		
		/** Whether gpudirect is used for this stream. Even though listener requests it, doesn't mean it will/can be used */
		bool bIsUsingGPUDirect = false;

		/** GPU device index to use when mapping to cuda. Will be needed for mgpu support */
		int32 GPUDeviceIndex = 0;

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

		/** 
		 * States used to track reception of a frame
		 */
		enum class EReceptionState
		{
			/** Used for the first frame receptionand when an error happened while receiving to detect reception of the end of frame */
			WaitingForMarker, 

			/** Used while receiving a frame and processing each SampleRowData */
			Receiving,

			/** Used when an error is detected during reception. Will wait for the end of frame detection before moving back to Receiving */
			FrameError
		};

		/** Starts stream waiting for a marker to align ourselves with a full frame */
		EReceptionState State = EReceptionState::WaitingForMarker;
	};
}


