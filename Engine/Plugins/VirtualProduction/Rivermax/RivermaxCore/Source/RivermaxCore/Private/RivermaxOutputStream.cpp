// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxOutputStream.h"

#include "Async/Async.h"
#include "CudaModule.h"
#include "IRivermaxCoreModule.h"
#include "IRivermaxBoundaryMonitor.h"
#include "IRivermaxManager.h"
#include "Misc/ByteSwap.h"
#include "RivermaxFrameManager.h"
#include "RivermaxLog.h"
#include "RivermaxPTPUtils.h"
#include "RivermaxTypes.h"
#include "RivermaxUtils.h"

namespace UE::RivermaxCore::Private
{
	static TAutoConsoleVariable<int32> CVarRivermaxWakeupOffset(
		TEXT("Rivermax.WakeupOffset"), 0,
		TEXT("Wakeup is done on alignment point. This offset will be substracted from it to wake up earlier. Units: nanoseconds"),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarRivermaxScheduleOffset(
		TEXT("Rivermax.ScheduleOffset"), 0,
		TEXT("Scheduling is done at alignment point plus TRO. This offset will be added to it to delay or schedule earlier. Units: nanoseconds"),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarRivermaxOutputShowStats(
		TEXT("Rivermax.ShowOutputStats"), 0,
		TEXT("Enable stats logging at fixed interval"),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarRivermaxOutputShowStatsInterval(
		TEXT("Rivermax.ShowStatsInterval"), 1.0,
		TEXT("Interval at which to show stats in seconds"),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarRivermaxOutputEnableMultiSRD(
		TEXT("Rivermax.Output.EnableMultiSRD"), 1,
		TEXT("When enabled, payload size will be constant across the frame except the last one.\n" 
		     "If disabled, a payload size that fits the line will be used causing some resolution to not be supported."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarRivermaxOutputLinesPerChunk(
		TEXT("Rivermax.Output.LinesPerChunk"), 4,
		TEXT("Defines the number of lines to pack in a chunk. Higher number will increase latency"),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarRivermaxOutputMaximizePacketSize(
		TEXT("Rivermax.Output.MaximizePacketSize"), 1,
		TEXT("Enables bigger packet sizes to maximize utilisation of potential UDP packet. If not enabled, packet size will be aligned with HD/4k sizes"),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarRivermaxOutputTROOverride(
		TEXT("Rivermax.Output.TRO"), 0,
		TEXT("If not 0, overrides transmit offset calculation (TRO) based on  frame rate and resolution with a fixed value. Value in seconds."),
		ECVF_Default);

	bool FindPayloadSize(const FRivermaxOutputStreamOptions& InOptions, uint32 InBytesPerLine, const FVideoFormatInfo& FormatInfo, uint16& OutPayloadSize)
	{
		using namespace UE::RivermaxCore::Private::Utils;

		// For now, we only find a payload size that can be equal across one line
		// Support for last payload of a line being smaller is there but is causing issue
		// We fail to output if we receive a resolution for which we can't find an equal payload size
		int32 TestPoint = InBytesPerLine / MaxPayloadSize;
		if (TestPoint == 0)
		{
			if (InBytesPerLine > MinPayloadSize)
			{
				if (InBytesPerLine % FormatInfo.PixelGroupSize == 0)
				{
					OutPayloadSize = InBytesPerLine;
					return true;
				}
			}
			return false;
		}

		while (true)
		{
			const int32 TestSize = InBytesPerLine / TestPoint;
			if (TestSize < MinPayloadSize)
			{
				break;
			}

			if (TestSize <= MaxPayloadSize)
			{
				if ((TestSize % FormatInfo.PixelGroupSize) == 0 && (InBytesPerLine % TestPoint) == 0)
				{
					OutPayloadSize = TestSize;
					return true;
				}
			}

			++TestPoint;
		}

		return false;
	}

	uint32 FindLinesPerChunk(const FRivermaxOutputStreamOptions& InOptions)
	{
		// More lines per chunks mean we will do more work prior to start sending a chunk. So, added 'latency' in terms of packet / parts of frame.
		// Less lines per chunk mean that sender thread might starve.
		// SDK sample uses 4 lines for UHD and 8 for HD. 
		return CVarRivermaxOutputLinesPerChunk.GetValueOnAnyThread();
	}

	uint16 GetPayloadSize(ESamplingType SamplingType)
	{
		const FVideoFormatInfo FormatInfo = FStandardVideoFormat::GetVideoFormatInfo(SamplingType);
		uint16 PayloadSize;
		switch (SamplingType)
		{

		case ESamplingType::YUV444_10bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_10bit:
		{
			PayloadSize = 1200;
			break;
		}
		case ESamplingType::YUV444_8bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_8bit:
		{
			PayloadSize = 1152;
			break;
		}
		case ESamplingType::YUV444_12bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_12bit:
		{
			PayloadSize = 1152;
			break;
		}
		case ESamplingType::YUV444_16bit:
		{
			// Passthrough
		}
		case ESamplingType::YUV444_16bitFloat:
		{
			// Passthrough
		}
		case ESamplingType::RGB_16bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_16bitFloat:
		{
			PayloadSize = 1152;
			break;
		}
		case ESamplingType::YUV422_8bit:
		{
			PayloadSize = 1280;
			break;
		}
		case ESamplingType::YUV422_10bit:
		{
			PayloadSize = 1200;
			break;
		}
		case ESamplingType::YUV422_12bit:
		{
			PayloadSize = 1152;
			break;
		}
		case ESamplingType::YUV422_16bit:
		{
			// Passthrough
		}
		case ESamplingType::YUV422_16bitFloat:
		{
			PayloadSize = 1280;
			break;
		}
		default:
		{
			checkNoEntry();
			PayloadSize = 1200;
			break;
		}
		}

		ensure(PayloadSize % FormatInfo.PixelGroupSize == 0);
		return PayloadSize;
	}

	/** 
	 * Returns a payload closer to the max value we can have for standard UDP size 
	 * RTPHeader can be bigger depending on configuration so we'll cap payload at 1400.
	 */
	uint16 GetMaximizedPayloadSize(ESamplingType SamplingType)
	{
		const FVideoFormatInfo FormatInfo = FStandardVideoFormat::GetVideoFormatInfo(SamplingType);
		uint16 PayloadSize;
		switch (SamplingType)
		{
		
		case ESamplingType::YUV444_10bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_10bit:
		{
			PayloadSize = 1395;
			break;
		}
		case ESamplingType::YUV444_8bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_8bit:
		{
			PayloadSize = 1398;	
			break;
		}
		case ESamplingType::YUV444_12bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_12bit:
		{
			PayloadSize = 1395;
			break;
		}
		case ESamplingType::YUV444_16bit:
		{
			// Passthrough
		}
		case ESamplingType::YUV444_16bitFloat:
		{
			// Passthrough
		}
		case ESamplingType::RGB_16bit:
		{
			// Passthrough
		}
		case ESamplingType::RGB_16bitFloat:
		{
			PayloadSize = 1398;
			break;
		}
		case ESamplingType::YUV422_8bit:
		{
			PayloadSize = 1400;
			break;
		}
		case ESamplingType::YUV422_10bit:
		{
			PayloadSize = 1400;
			break;
		}
		case ESamplingType::YUV422_12bit:
		{
			PayloadSize = 1398;
			break;
		}
		case ESamplingType::YUV422_16bit:
		{
			// Passthrough
		}
		case ESamplingType::YUV422_16bitFloat:
		{
			PayloadSize = 1400;
			break;
		}
		default:
		{
			checkNoEntry();
			PayloadSize = 1200;
			break;
		}
		}

		ensure(PayloadSize % FormatInfo.PixelGroupSize == 0);
		return PayloadSize;
	}


	FRivermaxOutputStream::FRivermaxOutputStream()
		: bIsActive(false)
	{

	}

	FRivermaxOutputStream::~FRivermaxOutputStream()
	{
		Uninitialize();
	}

	bool FRivermaxOutputStream::Initialize(const FRivermaxOutputStreamOptions& InOptions, IRivermaxOutputStreamListener& InListener)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRivermaxOutputStream::Initialize);

		RivermaxModule = FModuleManager::GetModulePtr<IRivermaxCoreModule>(TEXT("RivermaxCore"));
		if (RivermaxModule->GetRivermaxManager()->IsLibraryInitialized() == false)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't create Rivermax Output Stream. Library isn't initialized."));
			return false;
		}

		Options = InOptions;
		Listener = &InListener;
		FormatInfo = FStandardVideoFormat::GetVideoFormatInfo(Options.PixelFormat);

		// Verify resolution for sampling type
		if (Options.AlignedResolution.X % FormatInfo.PixelGroupCoverage != 0)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't create Rivermax Output Stream. Aligned horizontal resolution of %d doesn't align with pixel group coverage of %d."), Options.AlignedResolution.X, FormatInfo.PixelGroupCoverage);
			return false;
		}

		if (!SetupFrameManagement())
		{
			return false;
		}

		// Enable frame boundary monitoring
		MonitoringGuid = RivermaxModule->GetRivermaxBoundaryMonitor().StartMonitoring(Options.FrameRate);

		Async(EAsyncExecution::TaskGraph, [this]()
		{
			TAnsiStringBuilder<2048> SDPDescription;
			UE::RivermaxCore::Private::Utils::StreamOptionsToSDPDescription(Options, SDPDescription);

			// Initialize internal memory and rivermax configuration 
			if(InitializeStreamMemoryConfig())
			{
				// Initialize rivermax output stream with desired config
				const uint32 NumberPacketsPerFrame = StreamMemory.PacketsPerFrame;
				const uint32 MediaBlockIndex = 0;
				rmax_stream_id NewId;
				rmax_qos_attr QOSAttributes = { 0, 0 }; //todo

				rmax_buffer_attr BufferAttributes;
				FMemory::Memset(&BufferAttributes, 0, sizeof(BufferAttributes));
				BufferAttributes.chunk_size_in_strides = StreamMemory.PacketsPerChunk;
				BufferAttributes.data_stride_size = StreamMemory.PayloadSize; 
				BufferAttributes.app_hdr_stride_size = StreamMemory.HeaderStrideSize;
				BufferAttributes.mem_block_array = StreamMemory.MemoryBlocks.GetData();
				BufferAttributes.mem_block_array_len = StreamMemory.MemoryBlocks.Num();
				BufferAttributes.attr_flags = RMAX_OUT_BUFFER_ATTR_FLAG_NONE;

				rmax_status_t Status = rmax_out_create_stream(SDPDescription.GetData(), &BufferAttributes, &QOSAttributes, NumberPacketsPerFrame, MediaBlockIndex, &NewId);
				if (Status == RMAX_OK)
				{
					struct sockaddr_in SourceAddress;
					struct sockaddr_in DestinationAddress;
					FMemory::Memset(&SourceAddress, 0, sizeof(SourceAddress));
					FMemory::Memset(&DestinationAddress, 0, sizeof(DestinationAddress));
					Status = rmax_out_query_address(NewId, MediaBlockIndex, &SourceAddress, &DestinationAddress);
					if (Status == RMAX_OK)
					{
						StreamId = NewId;

						StreamData.FrameFieldTimeIntervalNs = 1E9 / Options.FrameRate.AsDecimal();
						InitializeStreamTimingSettings();

						UE_LOG(LogRivermax, Display, TEXT("Output stream started sending on stream %s:%d on interface %s%s")
							, *Options.StreamAddress
							, Options.Port
							, *Options.InterfaceAddress
							, bUseGPUDirect ? TEXT(" using GPUDirect") : TEXT(""));

						bIsActive = true;
						RivermaxThread.Reset(FRunnableThread::Create(this, TEXT("Rmax OutputStream Thread"), 128 * 1024, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask()));
					}
				}
				else
				{
					UE_LOG(LogRivermax, Warning, TEXT("Failed to create Rivermax output stream. Status: %d"), Status);
				}
			}

			Listener->OnInitializationCompleted(bIsActive);
		});

		return true;
	}

	void FRivermaxOutputStream::Uninitialize()
	{
		if (RivermaxThread != nullptr)
		{
			Stop();
			
			FrameAvailableSignal->Trigger();
			FrameReadyToSendSignal->Trigger();
			RivermaxThread->Kill(true);
			RivermaxThread.Reset();

			CleanupFrameManagement();

			RivermaxModule->GetRivermaxBoundaryMonitor().StopMonitoring(MonitoringGuid, Options.FrameRate);
			
			UE_LOG(LogRivermax, Log, TEXT("Rivermax Output stream has shutdown"));
		}
	}

	bool FRivermaxOutputStream::PushVideoFrame(const FRivermaxOutputVideoFrameInfo& NewFrame)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRivermaxOutputStream::PushVideoFrame);

		const bool bSuccess = FrameManager->SetFrameData(NewFrame);
		if (!bSuccess)
		{
			Listener->OnStreamError();
		}

		return bSuccess;
	}

	void FRivermaxOutputStream::Process_AnyThread()
	{
		using namespace UE::RivermaxCore::Private::Utils;

		// Not the best path but it seems to work without tearing for now
		// Wait for the next time a frame should be sent (based on frame interval)
		// If a frame had been sent, make it available again
		// This is to avoid making it available right after scheduling it. It's not sent yet and we start overwriting it
		// Get next available frame that was rendered (Wait if none are)
		// Send frame
		// Restart
		{
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FRivermaxOutputStream::Wait);
				WaitForNextRound();
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FRivermaxOutputStream::PrepareNextFrame);
				PrepareNextFrame();
			}

			// At this point, if there is no frame to send, move on to wait for next round
			if(CurrentFrame.IsValid() == false || !bIsActive)
			{
				return;
			}

			StreamData.LastSendStartTimeNanoSec = RivermaxModule->GetRivermaxManager()->GetTime();
			
			// Add markup when we start pushing out a frame with its timestamp to track it across network
			if (StreamData.bHasFrameFirstChunkBeenFetched == false)
			{
				const uint32 MediaTimestamp = GetTimestampFromTime(StreamData.NextAlignmentPointNanosec, MediaClockSampleRate);
				const FString TraceName = FString::Format(TEXT("RmaxOutput::StartSending {0}|{1}"), { MediaTimestamp, CurrentFrame->FrameIdentifier });
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*TraceName);
				UE_LOG(LogRivermax, Verbose, TEXT("RmaxTX frame number %u with timestamp %u."), CurrentFrame->FrameIdentifier, MediaTimestamp);
			}

			const FString TraceName = FString::Format(TEXT("RmaxOutput::SendFrame {0}"), { CurrentFrame->FrameIndex });
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*TraceName);
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("MediaCapturePipe: %u"), CurrentFrame->FrameIdentifier));

			do 
			{
				if (bIsActive)
				{	
					GetNextChunk();
				}

				if (bIsActive)
				{
					SetupRTPHeaders();
				}

				if (bIsActive)
				{
					CommitNextChunks();
				}

				//Update frame progress
				if (bIsActive)
				{
					Stats.TotalStrides += StreamMemory.PacketsPerChunk;
					++CurrentFrame->ChunkNumber;
				}

			} while (CurrentFrame->ChunkNumber < StreamMemory.ChunksPerMemoryBlock && bIsActive);
			

			Stats.MemoryBlockSentCounter++;
			StreamData.bHasFrameFirstChunkBeenFetched = false;
		}
	}

	bool FRivermaxOutputStream::InitializeStreamMemoryConfig()
	{
		using namespace UE::RivermaxCore::Private::Utils;

		//2110 stream type
		//We need to use the fullframe allocated size to compute the payload size.

		const int32 BytesPerLine = GetStride();

		// Find out payload we want to use. Either we go the 'potential' multi SRD route or we keep the old way of finding a common payload
		// with more restrictions on resolution supported. Kept in place to be able to fallback in case there are issues with the multiSRD one.
		if (CVarRivermaxOutputEnableMultiSRD.GetValueOnAnyThread() >= 1)
		{
			if (CVarRivermaxOutputMaximizePacketSize.GetValueOnAnyThread() >= 1)
			{
				StreamMemory.PayloadSize = GetMaximizedPayloadSize(FormatInfo.Sampling);
			}
			else
			{
				StreamMemory.PayloadSize = GetPayloadSize(FormatInfo.Sampling);
			}
		}
		else
		{
			const bool bFoundPayload = FindPayloadSize(Options, BytesPerLine, FormatInfo, StreamMemory.PayloadSize);
			if (bFoundPayload == false)
			{
				UE_LOG(LogRivermax, Warning, TEXT("Could not find payload size for desired resolution %dx%d for desired pixel format"), Options.AlignedResolution.X, Options.AlignedResolution.Y);
				return false;
			}
		}

		// With payload size in hand, figure out how many packets we will need, how many chunks (group of packets) and configure descriptor arrays

		const uint32 PixelCount = Options.AlignedResolution.X * Options.AlignedResolution.Y;
		const uint64 FrameSize = PixelCount / FormatInfo.PixelGroupCoverage * FormatInfo.PixelGroupSize;

		StreamMemory.PixelGroupPerPacket = StreamMemory.PayloadSize / FormatInfo.PixelGroupSize;
		StreamMemory.PixelsPerPacket = StreamMemory.PixelGroupPerPacket * FormatInfo.PixelGroupCoverage;

		// We might need a smaller packet to complete the end of frame so ceil to the next value
		StreamMemory.PacketsPerFrame = FMath::CeilToInt32((float)PixelCount / StreamMemory.PixelsPerPacket);

		// Depending on resolution and payload size, last packet of a line might not be fully utilized but we need the remaining bytes so ceil to next value
		StreamMemory.PacketsInLine = FMath::CeilToInt32((float)StreamMemory.PacketsPerFrame / Options.AlignedResolution.Y);

		StreamMemory.LinesInChunk = FindLinesPerChunk(Options);
		StreamMemory.PacketsPerChunk = StreamMemory.LinesInChunk * StreamMemory.PacketsInLine;
		StreamMemory.FramesFieldPerMemoryBlock = 1;

		// Chunk count won't necessarily align with the number of packets required. We need an integer amount of chunks to initialize our stream
		// and calculate how many packets that represents. Rivermax will expect the payload/header array to be that size. It just means that
		// we will mark the extra packets as 0 size.
		StreamMemory.ChunksPerFrameField = FMath::CeilToInt32((float)StreamMemory.PacketsPerFrame / StreamMemory.PacketsPerChunk);
		StreamMemory.PacketsPerMemoryBlock = StreamMemory.ChunksPerFrameField * StreamMemory.PacketsPerChunk * StreamMemory.FramesFieldPerMemoryBlock;
		StreamMemory.ChunksPerMemoryBlock = StreamMemory.FramesFieldPerMemoryBlock * StreamMemory.ChunksPerFrameField;
		StreamMemory.MemoryBlockCount = Options.NumberOfBuffers;

		// Setup arrays with the right sizes so we can give pointers to rivermax
		StreamMemory.RTPHeaders.SetNumZeroed(StreamMemory.MemoryBlockCount);
		StreamMemory.PayloadSizes.SetNumUninitialized(StreamMemory.PacketsPerMemoryBlock);
		StreamMemory.HeaderSizes.SetNumUninitialized(StreamMemory.PacketsPerMemoryBlock);
		StreamMemory.HeaderStrideSize = sizeof(FRawRTPHeader);

		uint64 TotalSize = 0;
		uint64 LineSize = 0;
		for (int32 PayloadSizeIndex = 0; PayloadSizeIndex < StreamMemory.PayloadSizes.Num(); ++PayloadSizeIndex)
		{
			uint32 HeaderSize = FRawRTPHeader::OneSRDSize;
			uint32 ThisPayloadSize = StreamMemory.PayloadSize;
			if (TotalSize < FrameSize)
			{
				if ((LineSize + StreamMemory.PayloadSize) == BytesPerLine)
				{
					LineSize = 0;
				}
				else if ((LineSize + StreamMemory.PayloadSize) > BytesPerLine)
				{
					HeaderSize = FRawRTPHeader::TwoSRDSize;
					LineSize = StreamMemory.PayloadSize - (BytesPerLine - LineSize);
					if (LineSize > BytesPerLine)
					{
						UE_LOG(LogRivermax, Warning, TEXT("Unsupported small resolution, %dx%d, needing more than 2 SRD to express"), Options.AlignedResolution.X, Options.AlignedResolution.Y);
						return false;
					}
				}
				else
				{
					// Keep track of line size offset to know when to use TwoSRDs
					LineSize += StreamMemory.PayloadSize;
				}

				if ((TotalSize + StreamMemory.PayloadSize) > FrameSize)
				{
					HeaderSize = FRawRTPHeader::OneSRDSize;
				}
			}
			else
			{
				// Extra header/payload required for the chunk alignment are set to 0. Nothing has to be sent out the wire.
				HeaderSize = 0;
				ThisPayloadSize = 0;
			}
			
			StreamMemory.HeaderSizes[PayloadSizeIndex] = HeaderSize;
			StreamMemory.PayloadSizes[PayloadSizeIndex] = ThisPayloadSize;
			TotalSize += ThisPayloadSize;
		}
		StreamMemory.MemoryBlocks.SetNumZeroed(StreamMemory.MemoryBlockCount);
		for (uint32 BlockIndex = 0; BlockIndex < StreamMemory.MemoryBlockCount; ++BlockIndex)
		{
			rmax_mem_block& Block = StreamMemory.MemoryBlocks[BlockIndex];
			Block.chunks_num = StreamMemory.ChunksPerMemoryBlock;
			Block.app_hdr_size_arr = StreamMemory.HeaderSizes.GetData();
			Block.data_size_arr = StreamMemory.PayloadSizes.GetData();
			Block.data_ptr = FrameManager->GetFrame(BlockIndex)->VideoBuffer;
			
			StreamMemory.RTPHeaders[BlockIndex].SetNumZeroed(StreamMemory.PacketsPerFrame);
			Block.app_hdr_ptr = &StreamMemory.RTPHeaders[BlockIndex][0];
		}

		return true;
	}

	void FRivermaxOutputStream::InitializeNextFrame(const TSharedPtr<FRivermaxOutputFrame>& NextFrame)
	{
		NextFrame->LineNumber = 0;
		NextFrame->PacketCounter = 0;
		NextFrame->SRDOffset = 0;
		NextFrame->ChunkNumber = 0;
		NextFrame->PayloadPtr = nullptr;
		NextFrame->HeaderPtr = nullptr;
	}

	void FRivermaxOutputStream::BuildRTPHeader(FRawRTPHeader& OutHeader) const
	{
		using namespace UE::RivermaxCore::Private::Utils;

		OutHeader = {};
		OutHeader.Version = 2;
		OutHeader.PaddingBit = 0;
		OutHeader.ExtensionBit = 0;
		OutHeader.PayloadType = 96; //Payload type should probably be infered from SDP
		OutHeader.SequenceNumber = ByteSwap((uint16)(StreamData.SequenceNumber & 0xFFFF));

		// For now, in order to be able to use a framelocked input, we pipe frame number in the timestamp for a UE-UE interaction
		// Follow up work to investigate adding this in RTP header
		uint64 InputTime = StreamData.NextScheduleTimeNanosec;
		if (Options.bDoFrameCounterTimestamping)
		{
			InputTime = UE::RivermaxCore::GetAlignmentPointFromFrameNumber(CurrentFrame->FrameIdentifier, Options.FrameRate);
		}

		const uint32 MediaTimestamp = GetTimestampFromTime(InputTime, MediaClockSampleRate);
		OutHeader.Timestamp = ByteSwap(MediaTimestamp);

		//2110 specific header
		OutHeader.SynchronizationSource = ByteSwap((uint32)0x0eb51dbd);  // Should Unreal has its own synch source ID

		if (StreamType == ERivermaxStreamType::VIDEO_2110_20_STREAM)
		{
			if (CurrentFrame->PacketCounter + 1 == StreamMemory.PacketsPerFrame)
			{
				OutHeader.MarkerBit = 1; // last packet in frame (Marker)
			}

			OutHeader.ExtendedSequenceNumber = ByteSwap((uint16)((StreamData.SequenceNumber >> 16) & 0xFFFF));

			// Verify if payload size exceeds line 
			const uint32 CurrentPayloadSize = StreamMemory.PayloadSizes[CurrentFrame->PacketCounter];

			const uint32 LineSizeOffset = ((CurrentFrame->SRDOffset / FormatInfo.PixelGroupCoverage) * FormatInfo.PixelGroupSize);
			const uint32 LineSize = ((Options.AlignedResolution.X / FormatInfo.PixelGroupCoverage) * FormatInfo.PixelGroupSize);

			const uint16 SRD1Length = FMath::Min(LineSize - LineSizeOffset, CurrentPayloadSize);
			const uint16 SRD1PixelCount = SRD1Length / FormatInfo.PixelGroupSize * FormatInfo.PixelGroupCoverage;
			uint16 SRD2Length = SRD1Length < CurrentPayloadSize ? CurrentPayloadSize - SRD1Length : 0;
			if (SRD2Length && CurrentFrame->LineNumber == ((uint32)Options.AlignedResolution.Y-1))
			{
				SRD2Length = 0;
			}

			OutHeader.SRD1Length = ByteSwap(SRD1Length);	
			OutHeader.SetSrd1RowNumber(CurrentFrame->LineNumber); //todo divide by 2 if interlaced
			OutHeader.FieldIdentification1 = 0; //todo when fields are sent for interlace
			OutHeader.SetSrd1Offset(CurrentFrame->SRDOffset); 

			CurrentFrame->SRDOffset += SRD1PixelCount;
			if (CurrentFrame->SRDOffset >= Options.AlignedResolution.X)
			{
				CurrentFrame->SRDOffset = 0;
				++CurrentFrame->LineNumber;
			}

			if (SRD2Length > 0)
			{
				OutHeader.SRD2Length = ByteSwap(SRD2Length);

				OutHeader.ContinuationBit1 = 1;
				OutHeader.FieldIdentification2 = 0;
				OutHeader.SetSrd2RowNumber(CurrentFrame->LineNumber);
				OutHeader.SetSrd2Offset(CurrentFrame->SRDOffset);

				const uint16 SRD2PixelCount = SRD2Length / FormatInfo.PixelGroupSize * FormatInfo.PixelGroupCoverage;
				CurrentFrame->SRDOffset += SRD2PixelCount;
				if (CurrentFrame->SRDOffset >= Options.AlignedResolution.X)
				{
					CurrentFrame->SRDOffset = 0;
					++CurrentFrame->LineNumber;
				}
			}
		}
	}

	void FRivermaxOutputStream::DestroyStream()
	{
		rmax_status_t Status = rmax_out_cancel_unsent_chunks(StreamId);
		if (Status != RMAX_OK)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Could not cancel unsent chunks when destroying output stream. Status: %d"), Status);
		}

		do 
		{
			Status = rmax_out_destroy_stream(StreamId);
			if (RMAX_ERR_BUSY == Status) 
			{
				FPlatformProcess::SleepNoStats(SleepTimeSeconds);
			}
		} while (Status == RMAX_ERR_BUSY);
	}

	void FRivermaxOutputStream::WaitForNextRound()
	{
		const uint64 CurrentTimeNanosec = RivermaxModule->GetRivermaxManager()->GetTime();
		const double CurrentPlatformTime = FPlatformTime::Seconds();
		const uint64 CurrentFrameNumber = UE::RivermaxCore::GetFrameNumber(CurrentTimeNanosec, Options.FrameRate);

		switch (Options.AlignmentMode)
		{
			case ERivermaxAlignmentMode::AlignmentPoint:
			{
				CalculateNextScheduleTime_AlignementPoints(CurrentTimeNanosec, CurrentFrameNumber);
				break;
			}
			case ERivermaxAlignmentMode::FrameCreation:
			{
				CalculateNextScheduleTime_FrameCreation(CurrentTimeNanosec, CurrentFrameNumber);
				break;
			}
			default:
			{
				checkNoEntry();
			}
		}

		// Offset wakeup if desired to give more time for scheduling. 
		const uint64 WakeupTime = StreamData.NextAlignmentPointNanosec - CVarRivermaxWakeupOffset.GetValueOnAnyThread();
		
		uint64 WaitTimeNanosec = WakeupTime - CurrentTimeNanosec;

		// Wakeup can be smaller than current time with controllable offset
		if (WakeupTime < CurrentTimeNanosec)
		{
			WaitTimeNanosec = 0;
		}

		static constexpr float SleepThresholdSec = 5.0f * (1.0f / 1000.0f);
		static constexpr float YieldTimeSec = 2.0f * (1.0f / 1000.0f);
		const double WaitTimeSec = FMath::Min(WaitTimeNanosec / 1E9, 1.0);

		// Sleep for the largest chunk of time
		if (WaitTimeSec > SleepThresholdSec)
		{
			FPlatformProcess::SleepNoStats(WaitTimeSec - YieldTimeSec);
		}

		// Yield our thread time for the remainder of wait time. 
		// Should we spin for a smaller time to be more precise?
		// Should we use platform time instead of rivermax get PTP to avoid making calls to it?
		constexpr bool bUsePlatformTimeToSpin = true;
		if (bUsePlatformTimeToSpin == false)
		{
			while (RivermaxModule->GetRivermaxManager()->GetTime() < WakeupTime)
			{
				FPlatformProcess::SleepNoStats(0.f);
			}
		}
		else
		{
			while (FPlatformTime::Seconds() < (CurrentPlatformTime + WaitTimeSec))
			{
				FPlatformProcess::SleepNoStats(0.f);
			}
		}

		if (StreamData.bHasValidNextFrameNumber)
		{
			const uint64 AfterSleepTimeNanosec = RivermaxModule->GetRivermaxManager()->GetTime();
			UE_LOG(LogRivermax, Verbose, TEXT("Scheduling at %llu. CurrentTime %llu. NextAlign %llu. Waiting %0.9f"), StreamData.NextScheduleTimeNanosec, CurrentTimeNanosec, StreamData.NextAlignmentPointNanosec, (double)WaitTimeNanosec / 1E9);
		}
	}

	void FRivermaxOutputStream::GetNextChunk()
	{
		bool bHasAddedTrace = false;
		rmax_status_t Status;
		do
		{
			Status = rmax_out_get_next_chunk(StreamId, &CurrentFrame->PayloadPtr, &CurrentFrame->HeaderPtr);
			if (Status == RMAX_OK)
			{
				if (StreamData.bHasFrameFirstChunkBeenFetched == false)
				{
					StreamData.bHasFrameFirstChunkBeenFetched = true;
					if (CurrentFrame->VideoBuffer != CurrentFrame->PayloadPtr)
					{
						//Debug code to track rivermax chunk processing
						UE_LOG(LogRivermax, Warning, TEXT("Frame being sent (%d) doesn't match chunks being processed."), CurrentFrame->FrameIndex);
					}
				}

				break;
			}
			else if (Status == RMAX_ERR_NO_FREE_CHUNK)
			{
				//We should not be here
				Stats.ChunkRetries++;
				if (!bHasAddedTrace)
				{
					UE_LOG(LogRivermax, Verbose, TEXT("No free chunks to get for chunk '%u'. Waiting"), CurrentFrame->ChunkNumber);
					TRACE_CPUPROFILER_EVENT_SCOPE(GetNextChunk::NoFreeChunk);
					bHasAddedTrace = true;
				}
			}
			else
			{
				UE_LOG(LogRivermax, Error, TEXT("Invalid error happened while trying to get next chunks. Status: %d"), Status);
				Listener->OnStreamError();
				Stop();
			}
		} while (Status != RMAX_OK && bIsActive);
	}

	void FRivermaxOutputStream::SetupRTPHeaders()
	{
		FRawRTPHeader* HeaderRawPtr = reinterpret_cast<FRawRTPHeader*>(CurrentFrame->HeaderPtr);
		check(HeaderRawPtr);
		for (uint32 StrideIndex = 0; StrideIndex < StreamMemory.PacketsPerChunk && CurrentFrame->PacketCounter < StreamMemory.PacketsPerFrame; ++StrideIndex)
		{
			BuildRTPHeader(*HeaderRawPtr);
			++StreamData.SequenceNumber;
			CurrentFrame->BytesSent += StreamMemory.PayloadSizes[CurrentFrame->PacketCounter];
			++CurrentFrame->PacketCounter;
			++HeaderRawPtr;
		}
	}

	void FRivermaxOutputStream::CommitNextChunks()
	{
		rmax_status_t Status;
		int32 ErrorCount = 0;
		const uint64 CurrentTimeNanosec = RivermaxModule->GetRivermaxManager()->GetTime();
		do
		{
			//Only first chunk gets scheduled with a timestamp. Following chunks are queued after it using 0
			uint64 ScheduleTime = CurrentFrame->ChunkNumber == 0 ? StreamData.NextScheduleTimeNanosec : 0;
			const rmax_commit_flags_t CommitFlags{};
			if (ScheduleTime != 0)
			{
				//TRACE_BOOKMARK(TEXT("Sched A: %llu, C: %llu"), StreamData.NextAlignmentPointNanosec, CurrentTimeNanosec);
				if (ScheduleTime <= CurrentTimeNanosec)
				{
					ScheduleTime = 0;
					++Stats.CommitImmediate;
				}
			}

			Status = rmax_out_commit(StreamId, ScheduleTime, CommitFlags);

			if (Status == RMAX_OK)
			{
				break;
			}
			else if (Status == RMAX_ERR_HW_SEND_QUEUE_FULL)
			{
				Stats.CommitRetries++;
				TRACE_CPUPROFILER_EVENT_SCOPE(CommitNextChunks::QUEUEFULL);
				++ErrorCount;
			}
			else if (Status == RMAX_ERR_HW_COMPLETION_ISSUE)
			{
				UE_LOG(LogRivermax, Error, TEXT("Completion issue while trying to commit next round of chunks."));
				Listener->OnStreamError();
				Stop();
			}
			else
			{
				UE_LOG(LogRivermax, Error, TEXT("Unhandled error (%d) while trying to commit next round of chunks."), Status);
				Listener->OnStreamError();
				Stop();
			}

		} while (Status != RMAX_OK && bIsActive);

		if (bIsActive && CurrentFrame->ChunkNumber == 0)
		{
			UE_LOG(LogRivermax, Verbose, TEXT("Committed frame [%u]. Scheduled for '%llu'. Aligned with '%llu'. Current time '%llu'. Was late: %d. Slack: %llu. Errorcount: %d")
				, CurrentFrame->FrameIdentifier
				, StreamData.NextScheduleTimeNanosec
				, StreamData.NextAlignmentPointNanosec
				, CurrentTimeNanosec
				, CurrentTimeNanosec >= StreamData.NextScheduleTimeNanosec ? 1 : 0
				, StreamData.NextScheduleTimeNanosec >= CurrentTimeNanosec ? StreamData.NextScheduleTimeNanosec - CurrentTimeNanosec : 0
				, ErrorCount);
		}
	}

	bool FRivermaxOutputStream::Init()
	{
		return true;
	}

	uint32 FRivermaxOutputStream::Run()
	{
		// Initial wait for a frame to be produced
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::InitialWait);
			FrameReadyToSendSignal->Wait();
		}

		while (bIsActive)
		{
			ShowStats();
			Process_AnyThread();
		}

		DestroyStream();

		return 0;
	}

	void FRivermaxOutputStream::Stop()
	{
		bIsActive = false;
	}

	void FRivermaxOutputStream::Exit()
	{

	}

	void FRivermaxOutputStream::PrepareNextFrame()
	{
		switch (Options.AlignmentMode)
		{
			case ERivermaxAlignmentMode::FrameCreation:
			{
				PrepareNextFrame_FrameCreation();
				break;
			}
			case ERivermaxAlignmentMode::AlignmentPoint:
			{
				PrepareNextFrame_AlignmentPoint();
				break;
			}
			default:
			{
				checkNoEntry();
			}
		}
	}

	void FRivermaxOutputStream::PrepareNextFrame_FrameCreation()
	{
		// When aligning on frame creation, we will always wait for a frame to be available.
		TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::WaitForReadyFrame);
		TSharedPtr<FRivermaxOutputFrame> NextFrameToSend = FrameManager->GetReadyFrame();
		while (!NextFrameToSend && bIsActive)
		{
			FrameReadyToSendSignal->Wait();
			NextFrameToSend = FrameManager->GetReadyFrame();
		}

		// In frame creation alignment, we always release the last frame sent
		if (CurrentFrame.IsValid())
		{
			FrameManager->MarkAsSent(CurrentFrame);
		}

		// Make the next frame to send the current one and update its state
		if (NextFrameToSend)
		{
			CurrentFrame = MoveTemp(NextFrameToSend);
			FrameManager->MarkAsSending(CurrentFrame);

			InitializeNextFrame(CurrentFrame);
		}
	}

	void FRivermaxOutputStream::PrepareNextFrame_AlignmentPoint()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::GetNextFrame_AlignmentPoint);

		// When aligning on alignment points:
		// We prepare to send the next frame that is ready if there is one available
		// if none are available and bDoContinuousOutput == true
		//		Repeat the last frame
		// if none are available and bDoContinuousOutput == false
		//		Don't send a frame and go back waiting for the next alignment point
		
		TSharedPtr<FRivermaxOutputFrame> NextFrameToSend = FrameManager->GetReadyFrame();

		// If we have a new frame, release the previous one
		// If we don't have a frame and we're not doing continuous output, we release it and we won't send a new one
		// If we don't have a frame but we are doing continuous output, we will reschedule the current one, so no release.
		if (!Options.bDoContinuousOutput || NextFrameToSend)
		{
			if (CurrentFrame)
			{
				FrameManager->MarkAsSent(CurrentFrame);
				CurrentFrame.Reset();
			}

			// Make the next frame to send the current one and update its state
			if (NextFrameToSend)
			{
				CurrentFrame = MoveTemp(NextFrameToSend);
				FrameManager->MarkAsSending(CurrentFrame);
				InitializeNextFrame(CurrentFrame);
			}
		}
		else
		{
			// We will resend the last one so just reinitialize it to resend
			InitializeNextFrame(CurrentFrame);
			
			// No frame to send, keep last one and restart its internal counters
			UE_LOG(LogRivermax, Verbose, TEXT("No frame to send. Reusing last frame '%d' with Id %u"), CurrentFrame->FrameIndex, CurrentFrame->FrameIdentifier);

			// Since we want to resend last frame, we need to fast forward chunk pointer to re-point to the one we just sent
			rmax_status_t Status;
			bool bHasAddedTrace = false;
			do
			{
				Status = rmax_out_skip_chunks(StreamId, StreamMemory.ChunksPerFrameField * (Options.NumberOfBuffers - 1));
				if (Status != RMAX_OK)
				{
					if (Status == RMAX_ERR_NO_FREE_CHUNK)
					{
						// Wait until there are enough free chunk to be skipped
						if (!bHasAddedTrace)
						{
							UE_LOG(LogRivermax, Warning, TEXT("No chunks ready to skip. Waiting"));
							TRACE_CPUPROFILER_EVENT_SCOPE(PrepareNextFrame::NoFreeChunk);
							bHasAddedTrace = true;
						}
					}
					else
					{
						ensure(false);
						UE_LOG(LogRivermax, Error, TEXT("Invalid error happened while trying to skip chunks. Status: %d."), Status);
						Listener->OnStreamError();
						Stop();
					}
				}
			} while (Status != RMAX_OK && bIsActive);
		}
	}

	void FRivermaxOutputStream::InitializeStreamTimingSettings()
	{
		using namespace UE::RivermaxCore::Private::Utils;

		const double TROOverride = CVarRivermaxOutputTROOverride.GetValueOnAnyThread();
		if (TROOverride != 0)
		{
			TransmitOffsetNanosec = TROOverride * 1E9;
			return;
		}

		double FrameIntervalNs = StreamData.FrameFieldTimeIntervalNs;
		const bool bIsProgressive = true;//todo MediaConfiguration.IsProgressive() 
		uint32 PacketsInFrameField = StreamMemory.PacketsPerFrame;
		if (bIsProgressive == false)
		{
			FrameIntervalNs *= 2;
			PacketsInFrameField *= 2;
		}

		// See https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=8165971 for reference
		// Gapped PRS doesn't support non standard resolution. Linear PRS would but Rivermax doesn't support it.
		if (StreamType == ERivermaxStreamType::VIDEO_2110_20_STREAM)
		{
			double RActive;
			double TRODefaultMultiplier;
			if (bIsProgressive)
			{
				RActive = (1080.0 / 1125.0);
				if (Options.AlignedResolution.Y >= FullHDHeight)
				{
					// As defined by SMPTE 2110-21 6.3.2
					TRODefaultMultiplier = (43.0 / 1125.0);
				}
				else
				{
					TRODefaultMultiplier = (28.0 / 750.0);
				}
			}
			else
			{
				if (Options.AlignedResolution.Y >= FullHDHeight)
				{
					// As defined by SMPTE 2110-21 6.3.3
					RActive = (1080.0 / 1125.0);
					TRODefaultMultiplier = (22.0 / 1125.0);
				}
				else if (Options.AlignedResolution.Y >= 576)
				{
					RActive = (576.0 / 625.0);
					TRODefaultMultiplier = (26.0 / 625.0);
				}
				else
				{
					RActive = (487.0 / 525.0);
					TRODefaultMultiplier = (20.0 / 525.0);
				}
			}

			// Need to reinvestigate the implication of this and possibly add cvar to control it runtime
			const double TRSNano = (FrameIntervalNs * RActive) / PacketsInFrameField;
			TransmitOffsetNanosec = (uint64)((TRODefaultMultiplier * FrameIntervalNs));
		}
	}

	uint32 FRivermaxOutputStream::GetTimestampFromTime(uint64 InTimeNanosec, double InMediaClockRate) const
	{
		// RTP timestamp is 32 bits and based on media clock (usually 90kHz).
		// Conversion based on rivermax samples

		const uint64 Nanoscale = 1E9;
		const uint64 Seconds = InTimeNanosec / Nanoscale;
		const uint64 Nanoseconds = InTimeNanosec % Nanoscale;
		const uint64 MediaFrameNumber = Seconds * InMediaClockRate;
		const uint64 MediaSubFrameNumber = Nanoseconds * InMediaClockRate / Nanoscale;
		const double Mask = 0x100000000;
		const double MediaTime = FMath::Fmod(MediaFrameNumber, Mask);
		const double MediaSubTime = FMath::Fmod(MediaSubFrameNumber, Mask);
		return MediaTime + MediaSubTime;
	}

	void FRivermaxOutputStream::ShowStats()
	{
		if (CVarRivermaxOutputShowStats.GetValueOnAnyThread() != 0)
		{
			const double CurrentTime = FPlatformTime::Seconds();
			if (CurrentTime - LastStatsShownTimestamp > CVarRivermaxOutputShowStatsInterval.GetValueOnAnyThread())
			{
				LastStatsShownTimestamp = CurrentTime;
				UE_LOG(LogRivermax, Log, TEXT("Stats: FrameSent: %u. CommitImmediate: %u. CommitRetries: %u. ChunkRetries: %u. ChunkSkippingRetries: %u"), Stats.MemoryBlockSentCounter, Stats.CommitImmediate, Stats.CommitRetries, Stats.ChunkRetries, Stats.ChunkSkippingRetries);
			}
		}
	}

	bool FRivermaxOutputStream::IsGPUDirectSupported() const
	{
		return bUseGPUDirect;
	}

	bool FRivermaxOutputStream::SetupFrameManagement()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRivermaxOutputStream::SetupFrameManagement);

		FrameManager = MakeUnique<FFrameManager>();

		// We do (try) to make gpu allocations here to let the capturer know if we require it or not.
		bool bTryGPUDirect = RivermaxModule->GetRivermaxManager()->IsGPUDirectOutputSupported() && Options.bUseGPUDirect;
		if (bTryGPUDirect)
		{
			const ERHIInterfaceType RHIType = RHIGetInterfaceType();
			if (RHIType != ERHIInterfaceType::D3D12)
			{
				UE_LOG(LogRivermax, Warning, TEXT("Can't initialize output to use GPUDirect. RHI is %d but only Dx12 is supported at the moment."), RHIType);
				bTryGPUDirect = false;
			}
		}

		FFrameManagerSetupArgs FrameManagerArgs;
		FrameManagerArgs.Resolution = Options.AlignedResolution;
		FrameManagerArgs.bTryGPUAllocation = bTryGPUDirect;
		FrameManagerArgs.NumberOfFrames = Options.NumberOfBuffers;
		FrameManagerArgs.Stride = GetStride();
		FrameManagerArgs.OnFreeFrameDelegate = FOnFrameReadyDelegate::CreateRaw(this, &FRivermaxOutputStream::OnFrameReadyToBeUsed);
		FrameManagerArgs.OnPreFrameReadyDelegate = FOnFrameReadyDelegate::CreateRaw(this, &FRivermaxOutputStream::OnPreFrameReadyToBeSent);
		FrameManagerArgs.OnFrameReadyDelegate = FOnFrameReadyDelegate::CreateRaw(this, &FRivermaxOutputStream::OnFrameReadyToBeSent);
		const EFrameMemoryLocation FrameLocation = FrameManager->Initialize(FrameManagerArgs);
		bUseGPUDirect = FrameLocation == EFrameMemoryLocation::GPU;

		return FrameLocation != EFrameMemoryLocation::None;
	}

	void FRivermaxOutputStream::CleanupFrameManagement()
	{
		FrameManager->Cleanup();
		FrameManager.Reset();
	}

	int32 FRivermaxOutputStream::GetStride() const
	{
		check(FormatInfo.PixelGroupCoverage != 0);
		return (Options.AlignedResolution.X / FormatInfo.PixelGroupCoverage) * FormatInfo.PixelGroupSize;
	}

	void FRivermaxOutputStream::CalculateNextScheduleTime_AlignementPoints(uint64 CurrentClockTimeNanosec, uint64 CurrentFrameNumber)
	{
		// Frame number we will want to align with
		uint64 NextFrameNumber = CurrentFrameNumber;

		bool bFoundValidTimings = true;
		
		if (StreamData.bHasValidNextFrameNumber == false)
		{
			// Now that the stream starts when a frame was produced, we can reduce our wait
			// We wait one frame here to start sending at the next frame boundary.
			// Since it takes a frame to send it, we could detect if we are in the first 10% (arbitrary)
			// of the interval and start sending right away but we might be overlapping with the next one
			NextFrameNumber = CurrentFrameNumber + 1;
		}
		else
		{
			// Case where we are back and frame number is the previous one. Depending on offsets, this could happen
			if (CurrentFrameNumber == StreamData.NextAlignmentPointFrameNumber - 1)
			{
				NextFrameNumber = StreamData.NextAlignmentPointFrameNumber + 1;
				UE_LOG(LogRivermax, Verbose, TEXT("Scheduling last frame was faster than expected. (CurrentFrame: '%llu' LastScheduled: '%llu') Scheduling for following expected one.")
					, CurrentFrameNumber
					, StreamData.NextAlignmentPointFrameNumber);
			}
			else
			{
				// We expect current frame number to be the one we scheduled for the last time or greater if something happened
				if (CurrentFrameNumber >= StreamData.NextAlignmentPointFrameNumber)
				{
					// If current frame is greater than last scheduled, we missed an alignment point. Shouldn't happen with continuous thread independent of engine.
					const uint64 DeltaFrames = CurrentFrameNumber - StreamData.NextAlignmentPointFrameNumber;
					if (DeltaFrames >= 1)
					{
						UE_LOG(LogRivermax, Warning, TEXT("Output missed %llu frames."), DeltaFrames);
						
						// If we missed a sync point, this means that last scheduled frame might still be ongoing and 
						// sending it might be crossing the frame boundary so we skip one entire frame to empty the queue.
						NextFrameNumber = CurrentFrameNumber + 2;
					}
					else
					{
						NextFrameNumber = CurrentFrameNumber + 1;
					}
				}
				else
				{
					// This is not expected (going back in time) but we should be able to continue. Scheduling immediately
					ensureMsgf(false, TEXT("Unexpected behaviour during output stream's alignment point calculation. Current time has gone back in time compared to last scheduling."));
					bFoundValidTimings = false;
				}
			}
		}

		// Get next alignment point based on the frame number we are aligning with
		const uint64 NextAlignmentNano = UE::RivermaxCore::GetAlignmentPointFromFrameNumber(NextFrameNumber, Options.FrameRate);

		// Add Tro offset to next alignment point and configurable offset
		StreamData.NextAlignmentPointNanosec = NextAlignmentNano;
		StreamData.NextScheduleTimeNanosec = NextAlignmentNano + TransmitOffsetNanosec + CVarRivermaxScheduleOffset.GetValueOnAnyThread();
		StreamData.NextAlignmentPointFrameNumber = NextFrameNumber;

		StreamData.bHasValidNextFrameNumber = bFoundValidTimings;
	}

	void FRivermaxOutputStream::CalculateNextScheduleTime_FrameCreation(uint64 CurrentClockTimeNanosec, uint64 CurrentFrameNumber)
	{
		double NextWaitTime = 0.0;
		if (StreamData.bHasValidNextFrameNumber == false)
		{
			StreamData.NextAlignmentPointNanosec = CurrentClockTimeNanosec;
			StreamData.NextScheduleTimeNanosec = StreamData.NextAlignmentPointNanosec + CVarRivermaxScheduleOffset.GetValueOnAnyThread();
			StreamData.NextAlignmentPointFrameNumber = CurrentFrameNumber;
			StreamData.bHasValidNextFrameNumber = true;
		}
		else
		{
			// In this mode, we just take last time we started to send and add a frame interval
			StreamData.NextAlignmentPointNanosec = StreamData.LastSendStartTimeNanoSec + StreamData.FrameFieldTimeIntervalNs;
			StreamData.NextScheduleTimeNanosec = StreamData.NextAlignmentPointNanosec + CVarRivermaxScheduleOffset.GetValueOnAnyThread();
			StreamData.NextAlignmentPointFrameNumber = UE::RivermaxCore::GetFrameNumber(StreamData.NextAlignmentPointNanosec, Options.FrameRate);
		}
	}

	bool FRivermaxOutputStream::ReserveFrame(uint32 FrameIdentifier) const
	{
		TSharedPtr<FRivermaxOutputFrame> ReservedFrame = FrameManager->GetFreeFrame();
		if (!ReservedFrame && Options.FrameLockingMode == EFrameLockingMode::BlockOnReservation)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::WaitForAvailableFrame);
			while(!ReservedFrame && bIsActive)
			{
				FrameAvailableSignal->Wait();
				ReservedFrame = FrameManager->GetFreeFrame();
			}
		}
		return ReservedFrame != nullptr;
	}

	void FRivermaxOutputStream::OnFrameReadyToBeSent()
	{	
		FrameReadyToSendSignal->Trigger();
	}

	void FRivermaxOutputStream::OnFrameReadyToBeUsed()
	{
		FrameAvailableSignal->Trigger();
	}

	void FRivermaxOutputStream::OnPreFrameReadyToBeSent()
	{
		Listener->OnPreFrameEnqueue();
	}
}

