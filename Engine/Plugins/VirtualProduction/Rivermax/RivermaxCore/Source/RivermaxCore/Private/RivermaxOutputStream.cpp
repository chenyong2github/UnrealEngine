// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxOutputStream.h"

#include "Async/Async.h"
#include "IRivermaxCoreModule.h"
#include "IRivermaxManager.h"
#include "Misc/ByteSwap.h"
#include "RivermaxLog.h"
#include "RivermaxTypes.h"
#include "RivermaxUtils.h"


namespace UE::RivermaxCore::Private
{
	void CalculateStreamTime(const FRivermaxOutputStreamMemory& InStreamMemory, const FRivermaxOutputStreamData& InStreamData, const FRivermaxStreamOptions& InMediaOptions, const double InSampleRate, double& InOutTimeNs, double& OutTimestampTick)
	{
		using namespace UE::RivermaxCore::Private::Utils;

		double FrameIntervalNs = InStreamData.FrameFieldTimeIntervalNs;
		const bool bIsProgressive = true;//todo MediaConfiguration.IsProgressive() 
		const ERivermaxStreamType StreamType = ERivermaxStreamType::VIDEO_2110_20_STREAM; //todo
		if (bIsProgressive == false)
		{
			FrameIntervalNs *= 2;
		}
		const uint64 FrameAtTime = (uint64)(InOutTimeNs / FrameIntervalNs + 1);
		double FirstPacketStartTimeNs = FrameAtTime * FrameIntervalNs; //next alignment point calculation

		uint32 PacketsInFrameField = InStreamMemory.PacketsInFrameField;
		if (bIsProgressive == false)
		{
			PacketsInFrameField *= 2;
		}
		
		// See https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=8165971 for reference
		// To be revisited for non standard resolution/frame rate
		if (StreamType == ERivermaxStreamType::VIDEO_2110_20_STREAM)
		{
			double RActive;
			double TRODefaultMultiplier;
			if (bIsProgressive)
			{
				RActive = (1080.0 / 1125.0);
				if (InMediaOptions.Resolution.Y >= FullHDHeight)
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
				if (InMediaOptions.Resolution.Y >= FullHDHeight)
				{
					// As defined by SMPTE 2110-21 6.3.3
					RActive = (1080.0 / 1125.0);
					TRODefaultMultiplier = (22.0 / 1125.0);
				}
				else if (InMediaOptions.Resolution.Y >= 576)
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

			const double TRSNano = (FrameIntervalNs * RActive) / PacketsInFrameField;
			const double TROffset = (TRODefaultMultiplier * FrameIntervalNs) - (VideoTROModification * TRSNano);
			FirstPacketStartTimeNs += TROffset;
		}

		constexpr double OneSecondNs = 1 * 1E9;
		OutTimestampTick = FirstPacketStartTimeNs * (InSampleRate / OneSecondNs);
		InOutTimeNs = FirstPacketStartTimeNs;
	}
	
	uint32 FindPayloadSize(const FRivermaxStreamOptions& InOptions, uint32 InBytesPerLine)
	{
		using namespace UE::RivermaxCore::Private::Utils;

		// If stride per line is small then our minimal payload size, send one line per packet.
		if (InBytesPerLine <= MaxPayloadSize)
		{
			return InBytesPerLine;
		}

		uint32 PacketDivider = 2; //Start with 2 packets per line as 1 is too big already
		static constexpr uint32 MaxPacketCountPerLine = 50; 
		while (PacketDivider <= MaxPacketCountPerLine)
		{
			const uint32 TestPayloadSize = InBytesPerLine / PacketDivider;
			const bool bIsFlushInLine = InBytesPerLine % PacketDivider == 0;
			if (bIsFlushInLine && TestPayloadSize < MaxPayloadSize)
			{
				return TestPayloadSize;
			}

			++PacketDivider;
		}

		return 0;
	}

	uint32 FindChunksPerLine(const FRivermaxStreamOptions& InOptions)
	{
		uint32 ChunksPerLine = 1; //Will need to revisit the impact of that
		static constexpr uint32 MaxChunksPerLine = 10;
		while (ChunksPerLine <= MaxChunksPerLine)
		{
			if (InOptions.Resolution.Y % ChunksPerLine == 0)
			{
				return ChunksPerLine;
			}

			++ChunksPerLine;
		}

		return 0;
	}


	FRivermaxOutputStream::FRivermaxOutputStream()
		: bIsActive(false)
	{

	}

	FRivermaxOutputStream::~FRivermaxOutputStream()
	{
		Uninitialize();
	}

	bool FRivermaxOutputStream::Initialize(const FRivermaxStreamOptions& InOptions, IRivermaxOutputStreamListener& InListener)
	{
		IRivermaxCoreModule& RivermaxModule = FModuleManager::LoadModuleChecked<IRivermaxCoreModule>(TEXT("RivermaxCore"));
		if (RivermaxModule.GetRivermaxManager()->IsInitialized() == false)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't create Rivermax Output Stream. Library isn't initialized."));
			return false;
		}

		Options = InOptions;
		Listener = &InListener;

		Async(EAsyncExecution::TaskGraph, [this]()
		{
			//Create event to wait on when no frames are available to send
			ReadyToSendEvent = FPlatformProcess::GetSynchEventFromPool();

			TAnsiStringBuilder<2048> SDPDescription;
			UE::RivermaxCore::Private::Utils::StreamOptionsToSDPDescription(Options, SDPDescription);

			// Initialize video buffer memory we will be using
			InitializeBuffers();

			// Initialize internal memory and rivermax configuration 
			InitializeMemory();

			// Initialize rivermax output stream with desired config
			const uint32 NumberPacketsPerFrame = StreamMemory.PacketsInFrameField;
			const uint32 MediaBlockIndex = 0;
			rmax_stream_id NewId;
			rmax_qos_attr QOSAttributes = { 0, 0 }; //todo

			rmax_buffer_attr BufferAttributes;
			BufferAttributes.chunk_size_in_strides = StreamMemory.ChunkSizeInStrides;
			BufferAttributes.data_stride_size = StreamMemory.PayloadSize; //Stride between chunks. 
			BufferAttributes.app_hdr_stride_size = StreamMemory.HeaderStrideSize; 
			BufferAttributes.mem_block_array = StreamMemory.MemoryBlocks.GetData();
			BufferAttributes.mem_block_array_len = StreamMemory.MemoryBlocks.Num();

			rmax_status_t Status = rmax_out_create_stream(SDPDescription.GetData(), &BufferAttributes, &QOSAttributes, NumberPacketsPerFrame, MediaBlockIndex, &NewId);
			if (Status == RMAX_OK)
			{
				struct sockaddr_in SourceAddress;
				struct sockaddr_in DestinationAddress;
				Status = rmax_out_query_address(NewId, MediaBlockIndex, &SourceAddress, &DestinationAddress);
				if (Status == RMAX_OK)
				{
					StreamId = NewId;

					// Todo add event manager to avoid spinning

					StreamData.FrameFieldTimeIntervalNs = 1E9 / Options.FrameRate.AsDecimal();
					StreamData.SendTimeNs = (FPlatformTime::Seconds() + 1) * 1E9; //Current time + 1 sec todo why?
					CalculateStreamTime(StreamMemory, StreamData, Options, MediaClockSampleRate, StreamData.SendTimeNs, StreamData.InitialTimestampTick);
					StreamData.StartSendTimeNs = StreamData.SendTimeNs;

					bIsActive = true;
					RivermaxThread.Reset(FRunnableThread::Create(this, TEXT("Rmax OutputStream Thread"), 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask()));
				}
			}
			else
			{
				UE_LOG(LogRivermax, Warning, TEXT("Failed to create Rivermax output stream. Status: %d"), Status);
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
			
			ReadyToSendEvent->Trigger();
			RivermaxThread->Kill(true);
			
			RivermaxThread.Reset();


			FPlatformProcess::ReturnSynchEventToPool(ReadyToSendEvent);
			ReadyToSendEvent = nullptr;
			UE_LOG(LogRivermax, Log, TEXT("Rivermax Output stream has shutdown"));
		}

	}

	bool FRivermaxOutputStream::PushVideoFrame(const FRivermaxOutputVideoFrameInfo& NewFrame)
	{
		if (TSharedPtr<FRivermaxOutputFrame> AvailableFrame = GetNextAvailableFrame(NewFrame.FrameIdentifier))
		{
			FMemory::Memcpy(AvailableFrame->VideoBuffer, NewFrame.VideoBuffer, NewFrame.Height * Options.Stride);
			AvailableFrame->bIsVideoBufferReady = true;

			//If Frame ready to be sent
			if(AvailableFrame->IsReadyToBeSent())
			{
				const FString TraceName = FString::Format(TEXT("FRivermaxOutputStream::PushFrame {0}"), { AvailableFrame->FrameIndex });
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*TraceName);

				FScopeLock Lock(&FrameCriticalSection);
				
				AvailableFrames.Remove(AvailableFrame);
				FramesToSend.Add(MoveTemp(AvailableFrame));
				ReadyToSendEvent->Trigger();
			}

			return true;
		}
		
		return false;
	}

	void FRivermaxOutputStream::Process_AnyThread()
	{
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
				StreamData.SendTimeNs = StreamData.StartSendTimeNs + StreamData.FrameFieldTimeIntervalNs * StreamMemory.FramesFieldPerMemoryBlock * Stats.MemoryBlockSentCounter;
				WaitForNextRound(StreamData.SendTimeNs);
			}

			{
				if (CurrentFrame.IsValid())
				{
					//Release last frame sent. We keep hold to avoid overwriting it as rivermax is sending it
					CurrentFrame->Clear();
					{
						FScopeLock Lock(&FrameCriticalSection);
						AvailableFrames.Add(MoveTemp(CurrentFrame));
						CurrentFrame.Reset();
					}
				}

				while (CurrentFrame.IsValid() == false && bIsActive)
				{
					CurrentFrame = GetNextFrameToSend();
					if (!CurrentFrame.IsValid())
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(FRivermaxOutputStream::WaitForFrame);
						const double PreWaitTime = FPlatformTime::Seconds();
						ReadyToSendEvent->Wait();
						UE_LOG(LogRivermax, Verbose, TEXT("Outputstream waited %0.8f"), FPlatformTime::Seconds() - PreWaitTime);
					}
				}
			}

			if (bIsActive == false)
			{
				return;
			}

			const FString TraceName = FString::Format(TEXT("FRivermaxOutputStream::SendFrame {0}"), { CurrentFrame->FrameIndex });
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*TraceName);

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
					Stats.TotalStrides += StreamMemory.ChunkSizeInStrides;

					if ((++CurrentFrame->ChunkNumber) % StreamMemory.ChunksPerFrameField == 0)
					{
						StreamData.SendTimeNs += StreamData.FrameFieldTimeIntervalNs;
					}
				}

			} while (CurrentFrame->ChunkNumber < StreamMemory.ChunksPerMemoryBlock && bIsActive);
			

			Stats.MemoryBlockSentCounter++;
			Stats.TotalStrides += StreamMemory.ChunkSizeInStrides;
			StreamData.bHasFrameFirstChunkBeenFetched = false;
		}
	}

	void FRivermaxOutputStream::InitializeBuffers()
	{
		AvailableFrames.Reserve(Options.NumberOfBuffers);
		for (int32 Index = 0; Index < Options.NumberOfBuffers; ++Index)
		{
			TSharedPtr<FRivermaxOutputFrame> Frame = MakeShared<FRivermaxOutputFrame>(Index);
			Frame->VideoBuffer = static_cast<uint8*>(FMemory::Malloc(Options.Resolution.Y * Options.Stride));
			AvailableFrames.Add(MoveTemp(Frame));
		}
	}

	void FRivermaxOutputStream::InitializeMemory()
	{
		using namespace UE::RivermaxCore::Private::Utils;

		//2110 stream type
		//We need to use the fullframe allocated size to compute the payload size.

		const uint32 EffectiveBytesPerLine = Options.Stride;
		const uint32 BytesPerLine = Options.Stride;
		StreamMemory.PayloadSize = FindPayloadSize(Options, BytesPerLine); 
		StreamMemory.DataStrideSize = BytesPerLine;//todo align cache line?
		StreamMemory.LinesInChunk = FindChunksPerLine(Options);

		StreamMemory.PacketsInLine =  FMath::RoundToInt32(BytesPerLine / (float)StreamMemory.PayloadSize);
		StreamMemory.ChunkSizeInStrides = StreamMemory.LinesInChunk * StreamMemory.PacketsInLine;

		StreamMemory.FramesFieldPerMemoryBlock = 1;
		StreamMemory.PacketsInFrameField =  StreamMemory.PacketsInLine * Options.Resolution.Y;
		StreamMemory.PacketsPerMemoryBlock = StreamMemory.PacketsInFrameField * StreamMemory.FramesFieldPerMemoryBlock;
		StreamMemory.ChunksPerFrameField = StreamMemory.PacketsInFrameField / StreamMemory.ChunkSizeInStrides;
		StreamMemory.ChunksPerMemoryBlock = StreamMemory.FramesFieldPerMemoryBlock * StreamMemory.ChunksPerFrameField;
		StreamMemory.MemoryBlockCount = Options.NumberOfBuffers;
		StreamMemory.StridesPerMemoryBlock = StreamMemory.ChunkSizeInStrides * StreamMemory.ChunksPerMemoryBlock;

		// Setup arrays with the right sizes so we can give pointers to rivermax
		StreamMemory.RTPHeaders.SetNumZeroed(StreamMemory.PacketsPerMemoryBlock);
		StreamMemory.PayloadSizes.SetNumUninitialized(StreamMemory.PacketsPerMemoryBlock);
		StreamMemory.HeaderSizes.SetNumUninitialized(StreamMemory.PacketsPerMemoryBlock);
		StreamMemory.HeaderStrideSize = BytesPerHeader;
		for (int32 PayloadSizeIndex = 0; PayloadSizeIndex < StreamMemory.PayloadSizes.Num(); ++PayloadSizeIndex)
		{
			//Go through each chunk to have effective payload size to be sent (last one of each line could be smaller)
			if ((PayloadSizeIndex + 1) % StreamMemory.PacketsInLine == 0)
			{
				StreamMemory.PayloadSizes[PayloadSizeIndex] = EffectiveBytesPerLine - ((StreamMemory.PacketsInLine - 1) * StreamMemory.PayloadSize);
			}
			else
			{
				StreamMemory.PayloadSizes[PayloadSizeIndex] = StreamMemory.PayloadSize;
			}
			StreamMemory.HeaderSizes[PayloadSizeIndex] = StreamMemory.HeaderStrideSize;
		}
		StreamMemory.MemoryBlocks.SetNum(StreamMemory.MemoryBlockCount);
		for (uint32 BlockIndex = 0; BlockIndex < StreamMemory.MemoryBlockCount; ++BlockIndex)
		{
			rmax_mem_block& Block = StreamMemory.MemoryBlocks[BlockIndex];
			Block.chunks_num = StreamMemory.ChunksPerMemoryBlock;
			Block.app_hdr_size_arr = StreamMemory.HeaderSizes.GetData();
			Block.data_size_arr = StreamMemory.PayloadSizes.GetData();
			Block.data_ptr = AvailableFrames[BlockIndex]->VideoBuffer;
			Block.app_hdr_ptr = &StreamMemory.RTPHeaders[BlockIndex];
		}
	}

	void FRivermaxOutputStream::InitializeNextFrame(const TSharedPtr<FRivermaxOutputFrame>& NextFrame)
	{
		NextFrame->TimestampTicks = StreamData.InitialTimestampTick;
		NextFrame->LineNumber = 0;
		NextFrame->PacketCounter = 0;
		NextFrame->SRDOffset = 0;
		NextFrame->ChunkNumber = 0;
		NextFrame->PayloadPtr = nullptr;
		NextFrame->HeaderPtr = nullptr;
	}

	TSharedPtr<FRivermaxOutputFrame> FRivermaxOutputStream::GetNextFrameToSend()
	{
		TSharedPtr<FRivermaxOutputFrame> FrameToSend;

		FScopeLock Lock(&FrameCriticalSection);
		if (FramesToSend.IsEmpty() == false)
		{
			// Pick oldest frame of the lot. 
			// Todo Sort array when modified to always pick first item.
			uint32 OldestIdentifier = TNumericLimits<uint32>::Max();
			TArray<TSharedPtr<FRivermaxOutputFrame>>::TIterator It = FramesToSend.CreateIterator();
			for (It; It; ++It)
			{
				TSharedPtr<FRivermaxOutputFrame>& ItFrame = *It;
				if (ItFrame->FrameIdentifier < OldestIdentifier)
				{
					FrameToSend = *It;
					OldestIdentifier = FrameToSend->FrameIdentifier;
				}
			}

			if (ensure(FrameToSend))
			{
				FramesToSend.Remove(FrameToSend);
				InitializeNextFrame(FrameToSend);
			}
		}

		return FrameToSend;
	}

	TSharedPtr<FRivermaxOutputFrame> FRivermaxOutputStream::GetNextAvailableFrame(uint32 InFrameIdentifier)
	{
		TSharedPtr<FRivermaxOutputFrame> NextFrame;

		{
			FScopeLock Lock(&FrameCriticalSection);

			//Find matching frame identifier
			if(TSharedPtr<FRivermaxOutputFrame>* MatchingFrame = AvailableFrames.FindByPredicate([InFrameIdentifier](const TSharedPtr<FRivermaxOutputFrame>& Other){ return Other->FrameIdentifier == InFrameIdentifier; }))
			{
				NextFrame = *MatchingFrame;
			}
			else if(TSharedPtr<FRivermaxOutputFrame>* EmptyFrame = AvailableFrames.FindByPredicate([](const TSharedPtr<FRivermaxOutputFrame>& Other){ return Other->FrameIdentifier == FRivermaxOutputFrame::InvalidIdentifier; })) //Find an empty frame
			{
				NextFrame = *EmptyFrame;
				NextFrame->FrameIdentifier = InFrameIdentifier;
			}
			else
			{
				// Could reuse a frame not yet ready to be sent
				// Could also look into stealing a frame ready to be sent bot not sent yet
			}
		}

		return NextFrame;
	}

	void FRivermaxOutputStream::BuildRTPHeader(FRTPHeader& OutHeader) const
	{
		// build RTP header - 12 bytes
		/*
		0                   1                   2                   3
		0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		| V |P|X|  CC   |M|     PT      |            SEQ                |
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|                           timestamp                           |
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|                           ssrc                                |
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
		OutHeader.RawHeader[0] = 0x80;  // 10000000 - version2, no padding, no extension
		OutHeader.RawHeader[1] = 96; //todo payload type from sdp file
		OutHeader.RawHeader[2] = (StreamData.SequenceNumber >> 8) & 0xFF;  // sequence number
		OutHeader.RawHeader[3] = (StreamData.SequenceNumber) & 0xFF;  // sequence number
		*(uint32*)&OutHeader.RawHeader[4] = ByteSwap((uint32)CurrentFrame->TimestampTicks);

		//2110 specific header
		*(uint32*)&OutHeader.RawHeader[8] = 0x0eb51dbd;  // simulated ssrc

		if (StreamType == ERivermaxStreamType::VIDEO_2110_20_STREAM)
		{
			//SRD means Sample Row Data
			// build SRD header - 8-14 bytes
		   /* 0                   1                   2                   3
			0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			|    Extended Sequence Number   |           SRD Length          |
			+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			|F|     SRD Row Number          |C|         SRD Offset          |
			+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
			OutHeader.RawHeader[12] = (StreamData.SequenceNumber >> 24) & 0xff;  // high 16 bit of seq Extended Sequence Number
			OutHeader.RawHeader[13] = (StreamData.SequenceNumber >> 16) & 0xff;  // high 16 bit of seq Extended Sequence Number

			const uint16 CurrentPayloadSize = StreamMemory.PayloadSizes[StreamData.SequenceNumber % StreamMemory.PacketsInFrameField];
			*(uint16*)&OutHeader.RawHeader[14] = ByteSwap(CurrentPayloadSize);  // SRD Length
		
			uint16 number_of_rows = Options.Resolution.Y; //todo divide by 2 if interlaced
			uint16 srd_row_number = (CurrentFrame->LineNumber % number_of_rows);
			*(uint16*)&OutHeader.RawHeader[16] = ByteSwap(srd_row_number);
			OutHeader.RawHeader[16] |= (0 << 7); //todo when fields are sent for interlace

			// we never have continuation
			*(uint16*)&OutHeader.RawHeader[18] = ByteSwap(CurrentFrame->SRDOffset);  // SRD Offset
			uint16 group_size = (uint16)((StreamMemory.PayloadSize /*- 20*/) / 2.5); // todo: need to support pgroup definition per sampling format to fully be compliant
			CurrentFrame->SRDOffset = (CurrentFrame->SRDOffset + group_size) % (group_size * StreamMemory.PacketsInLine);
		}

		if (++CurrentFrame->PacketCounter == StreamMemory.PacketsInFrameField)
		{
			OutHeader.RawHeader[1] |= 0x80; // last packet in frame (Marker)
			// ST2210-20: the timestamp SHOULD be the same for each packet of the frame/field.
			const double ticks = (MediaClockSampleRate / (Options.FrameRate.AsDecimal()));
			//if (set.video_type != VIDEO_TYPE::PROGRESSIVE) {
			//	send_data.m_second_field = !send_data.m_second_field;
			//	ticks /= 2;
			//}
			CurrentFrame->TimestampTicks += ticks;
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
				FPlatformProcess::SleepNoStats(0.01f);
			}
		} while (Status == RMAX_ERR_BUSY);
	}

	void FRivermaxOutputStream::WaitForNextRound(double NextRoundTimeNs)
	{
		double NowTimeNs = FPlatformTime::Seconds() * 1E9;

		//If we are already late, no need to wait
		if (NextRoundTimeNs <= NowTimeNs)
		{
			return;
		}

		const double SleepTimeNs = NextRoundTimeNs - NowTimeNs;

		static const bool bShouldPoll = false;

		//Todo Should we have a threshold where we don't sleep?
		//Todo Should we sleep less when we had retries
		if (bShouldPoll)
		{
			while (NowTimeNs < SleepTimeNs)
			{
				NowTimeNs = FPlatformTime::Seconds() * 1E9;
			}
		}
		else
		{
			const double SleepTimeS = SleepTimeNs / 1E9;
			UE_LOG(LogRivermax, Verbose, TEXT("Outputstream sleeping for %0.5f"), SleepTimeS);
			FPlatformProcess::SleepNoStats(SleepTimeS);
		}
	}

	void FRivermaxOutputStream::GetNextChunk()
	{
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
			}
			else
			{
				UE_LOG(LogRivermax, Error, TEXT("Invalid error happened while trying to get next chunks. Status: %d"), Status);
				Listener->OnStreamError();
				bIsActive = false;
			}
		} while (Status != RMAX_OK && bIsActive);
	}

	void FRivermaxOutputStream::SetupRTPHeaders()
	{
		uint8* HeaderRawPtr = reinterpret_cast<uint8*>(CurrentFrame->HeaderPtr);
		check(HeaderRawPtr);
		for (uint32 StrideIndex = 0; StrideIndex < StreamMemory.ChunkSizeInStrides && CurrentFrame->PacketCounter < StreamMemory.PacketsInFrameField; ++StrideIndex)
		{
			uint8* NextHeaderRawPtr = HeaderRawPtr + (StrideIndex * StreamMemory.HeaderStrideSize);
			BuildRTPHeader(*reinterpret_cast<FRTPHeader*>(NextHeaderRawPtr));
			//todo only for video
			if (!((StrideIndex + 1) % StreamMemory.PacketsInLine))
			{
				CurrentFrame->LineNumber = (CurrentFrame->LineNumber + 1) % Options.Resolution.Y; //preparing line number for next iteration
			}
			++StreamData.SequenceNumber;
		}
	}

	void FRivermaxOutputStream::CommitNextChunks()
	{
		rmax_status_t Status;
		do
		{
			// todo configure timeout
			
			const uint64 Timeout = 0;
			const rmax_commit_flags_t CommitFlags{};
			Status = rmax_out_commit(StreamId, Timeout, CommitFlags);
			if (Status == RMAX_OK)
			{
				break;
			}
			else if (Status == RMAX_ERR_HW_SEND_QUEUE_FULL)
			{
				Stats.CommitRetries++;
			}
			else if(Status == RMAX_ERR_HW_COMPLETION_ISSUE)
			{
				UE_LOG(LogRivermax, Error, TEXT("Completion issue while trying to commit next round of chunks."));
				Listener->OnStreamError();
				bIsActive = false;
			}

		} while (Status != RMAX_OK && bIsActive);
	}

	bool FRivermaxOutputStream::Init()
	{
		return true;
	}

	uint32 FRivermaxOutputStream::Run()
	{
		while (bIsActive)
		{
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
}

