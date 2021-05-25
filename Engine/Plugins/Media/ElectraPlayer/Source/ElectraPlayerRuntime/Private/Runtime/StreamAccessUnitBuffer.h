// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "PlayerTime.h"
#include "StreamTypes.h"



namespace Electra
{


	class IAccessUnitMemoryProvider
	{
	public:
		enum class EDataType
		{
			AU,
			Payload,
			GenericData
		};
		virtual ~IAccessUnitMemoryProvider() = default;

		virtual void* AUAllocate(EDataType type, SIZE_T size, SIZE_T alignment = 0) = 0;
		virtual void AUDeallocate(EDataType type, void* pAddr) = 0;
	};



	/**
	 * Information into which buffer the AU data needs to be placed.
	 */
	struct FBufferSourceInfo
	{
		// Identifies the period and track (adaptation set) this data is originating from.
		FString		PeriodAdaptationSetID;
		// Partial track metadata. See FTrackMetadata.
		FString		Kind;
		FString		Language;
		// Internal hard index, used for multiplexed streams.
		int32		HardIndex = -1;
	};

	struct FAccessUnit
	{
		struct CodecData
		{
			TArray<uint8>					CodecSpecificData;
			TArray<uint8>					RawCSD;
			FStreamCodecInformation			ParsedInfo;
		};


		enum EDropState
		{
			None = 0,
			DtsTooEarly = 1,
			PtsTooEarly = 2,
			DtsTooLate = 4,
			PtsTooLate = 8
		};

		EStreamType					ESType;							//!< Type of elementary stream this is an access unit of.
		FTimeValue					PTS;							//!< PTS
		FTimeValue					DTS;							//!< DTS
		FTimeValue					Duration;						//!< Duration
		FTimeValue					OverlapAdjust;					//!< If set this indicates by how much the AU overlaps the desired time. Negative values indicate the AU is too early. Only set when DropState is zero.
		uint32						AUSize;							//!< Size of this access unit
		void*						AUData;							//!< Access unit data
		TSharedPtrTS<CodecData>		AUCodecData;					//!< If set, points to sideband data for this access unit.
		uint8						DropState;						//!< If set this access unit is not to be rendered. If possible also not to be decoded.
		bool						bIsFirstInSequence;				//!< true for the first AU in a segment
		bool						bIsLastInPeriod;				//!< true if this is the last AU in the playing period.
		bool						bIsSyncSample;					//!< true if this is a sync sample (keyframe)
		bool						bIsDummyData;					//!< True if the decoder must be reset before decoding this AU.
		bool						bTrackChangeDiscontinuity;		//!< True if this is the first AU after a track change.

		TSharedPtrTS<const FBufferSourceInfo>	BufferSourceInfo;
		TSharedPtrTS<const FPlayerLoopState>	PlayerLoopState;

		// Reserved for decoders.
		// FIXME: move this out of here and have the decoders track what they're doing with the AU.
		bool						bHasBeenPrepared;


		static FAccessUnit* Create(IAccessUnitMemoryProvider* MemProvider)
		{
			void* NewBuffer = MemProvider->AUAllocate(IAccessUnitMemoryProvider::EDataType::AU, sizeof(FAccessUnit));
			FAccessUnit* NewAU = new (NewBuffer) FAccessUnit;
			NewAU->AUMemoryProvider = MemProvider;
			return NewAU;
		}

		int64 TotalMemSize() const
		{
			return sizeof(FAccessUnit) + AUSize + (AUCodecData.IsValid() ? AUCodecData->CodecSpecificData.Num() : 0);
		}

		void AddRef()
		{
			FMediaInterlockedIncrement(RefCount);
		}

		void SetCodecSpecificData(const TArray<uint8>& Csd)
		{
			AUCodecData = MakeSharedTS<CodecData>();
			AUCodecData->CodecSpecificData = Csd;
		}

		void SetCodecSpecificData(const TSharedPtrTS<CodecData>& Csd)
		{
			AUCodecData = Csd;
		}

		void* AllocatePayloadBuffer(SIZE_T Num)
		{
			check(AUMemoryProvider);
			return AUMemoryProvider ? AUMemoryProvider->AUAllocate(IAccessUnitMemoryProvider::EDataType::Payload, Num) : nullptr;
		}

		void AdoptNewPayloadBuffer(void* Buffer, SIZE_T Num)
		{
			if (AUData)
			{
				AUMemoryProvider->AUDeallocate(IAccessUnitMemoryProvider::EDataType::Payload, AUData);
			}
			AUData = Buffer;
			AUSize = Num;
		}

		static void Release(FAccessUnit* AccessUnit)
		{
			if (AccessUnit)
			{
				check(AccessUnit->RefCount > 0);
				if (FMediaInterlockedDecrement(AccessUnit->RefCount) == 1)
				{
					IAccessUnitMemoryProvider* MemoryProvider = AccessUnit->AUMemoryProvider;
					AccessUnit->~FAccessUnit();
					MemoryProvider->AUDeallocate(IAccessUnitMemoryProvider::EDataType::AU, AccessUnit);
				}
			}
		}

	private:
		IAccessUnitMemoryProvider*		AUMemoryProvider;			//!< Interface to use to delete the allocated AU
		uint32							RefCount;

		FAccessUnit()
		{
			RefCount = 1;
			PTS.SetToInvalid();
			DTS.SetToInvalid();
			Duration.SetToInvalid();
			ESType = EStreamType::Unsupported;
			AUSize = 0;
			AUData = nullptr;
			AUMemoryProvider = nullptr;
			DropState = EDropState::None;
			bIsFirstInSequence = false;
			bIsLastInPeriod = false;
			bIsSyncSample = false;
			bIsDummyData = false;
			bTrackChangeDiscontinuity = false;
			bHasBeenPrepared = false;
		}

		~FAccessUnit()
		{
			if (AUData)
			{
				check(AUMemoryProvider);
				AUMemoryProvider->AUDeallocate(IAccessUnitMemoryProvider::EDataType::Payload, AUData);
				AUData = nullptr;
			}
		}
	};






	struct FAccessUnitBufferInfo
	{
		FAccessUnitBufferInfo()
		{
			Clear();
		}
		void Clear()
		{
			FrontDTS.SetToInvalid();
			PushedDuration.SetToZero();
			PlayableDuration.SetToZero();
			MaxDuration.SetToZero();
			MaxDataSize = 0;
			CurrentMemInUse = 0;
			NumCurrentAccessUnits = 0;
			bEndOfData = false;
			bLastPushWasBlocked = false;
		}

		FTimeValue			FrontDTS;
		FTimeValue			PushedDuration;
		FTimeValue			PlayableDuration;
		FTimeValue			MaxDuration;
		int64				MaxDataSize;
		int64				CurrentMemInUse;
		int64				NumCurrentAccessUnits;
		bool				bEndOfData;
		bool				bLastPushWasBlocked;
	};


	/**
	 * This class implements a decoder input data FIFO.
	**/
	class FAccessUnitBuffer
	{
	public:
		struct FConfiguration
		{
			FConfiguration(int64 InMaxByteSize = 0, double InMaxSeconds = 0.0)
				: MaxDataSize(InMaxByteSize)
			{
				MaxDuration.SetFromSeconds(InMaxSeconds);
			}
			FTimeValue	MaxDuration;
			int64		MaxDataSize;
		};

		struct FExternalBufferInfo
		{
			FTimeValue	Duration = FTimeValue::GetZero();
			int64		DataSize = 0;
		};

		FAccessUnitBuffer()
			: FrontDTS(FTimeValue::GetInvalid())
			, PushedDuration(FTimeValue::GetZero())
			, PlayableDuration(FTimeValue::GetZero())
			, CurrentMemInUse(0)
			, bEndOfData(false)
			, bLastPushWasBlocked(false)
		{
		}

		~FAccessUnitBuffer()
		{
			while(AccessUnits.Num())
			{
				FAccessUnit::Release(AccessUnits.Pop());
			}
		}

		//! Returns the number of access units currently in the FIFO.
		int64 Num() const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			return AccessUnits.Num();
		}

		//! Returns the amount of memory currently allocated.
		int64 AllocatedSize() const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			return CurrentMemInUse;
		}

		//! Returns the amount of playable duration.
		FTimeValue GetPlayableDuration() const
		{
			return PlayableDuration;
		}

		//! Returns all vital statistics.
		void GetStats(FAccessUnitBufferInfo& OutStats, const FConfiguration* Limit = nullptr) const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			Limit = Limit ? Limit : &Config;
			OutStats.FrontDTS = FrontDTS;
			OutStats.PushedDuration = PushedDuration;
			OutStats.PlayableDuration = PlayableDuration;
			OutStats.MaxDuration = Limit->MaxDuration;
			OutStats.MaxDataSize = Limit->MaxDataSize;
			OutStats.CurrentMemInUse = CurrentMemInUse;
			OutStats.NumCurrentAccessUnits = AccessUnits.Num();
			OutStats.bEndOfData = bEndOfData;
			OutStats.bLastPushWasBlocked = bLastPushWasBlocked;
		}

		//! Sets the maximum amount of memory/number of AUs this FIFO is allowed to allocate.
		void CapacitySet(const FConfiguration& InConfig)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			Flush();
			Config = InConfig;
		}

		//! Adds an access unit to the FIFO. Returns true if successful, false if the FIFO has insufficient free space.
		bool Push(FAccessUnit*& AU, const FConfiguration* Limit = nullptr, const FExternalBufferInfo* ExternalInfo = nullptr)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			// Pushing new data unconditionally clears the EOD flag even if the buffer is currently full.
			// The attempt to push implies there will be more data.
			bEndOfData = false;
			Limit = Limit ? Limit : &Config;
			ExternalInfo = ExternalInfo ? ExternalInfo : &ZeroExternalInfo;
			if (CanPush(AU, Limit, ExternalInfo))
			{
				bLastPushWasBlocked = false;
				AccessUnits.Push(AU);
				int64 memSize = AU->TotalMemSize();
				CurrentMemInUse += memSize;

				if (AU->DropState == FAccessUnit::EDropState::None)
				{
					if (!FrontDTS.IsValid())
					{
						FrontDTS = AU->DTS;
					}
					PlayableDuration += AU->Duration;
				}

				PushedDuration += AU->Duration;
				NumInSemaphore.Release();
				return true;
			}
			else
			{
				bLastPushWasBlocked = true;
				return false;
			}
		}

		//! "Pushes" an end-of-data marker signaling that no further data will be pushed. May be called more than once. Flushing or pushing new data clears the flag.
		void PushEndOfData()
		{
			bEndOfData = true;
		}

		//! Removes and returns the oldest access unit from the FIFO. Returns false if the FIFO is empty.
		bool Pop(FAccessUnit*& OutAU)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			if (Num())
			{
				OutAU = AccessUnits.Pop();
				int64 nMemSize = OutAU->TotalMemSize();
				CurrentMemInUse -= nMemSize;
				NumInSemaphore.TryToObtain();

				if (!AccessUnits.IsEmpty())
				{
					if (AccessUnits.FrontRef()->DropState == FAccessUnit::EDropState::None)
					{
						FrontDTS = AccessUnits.FrontRef()->DTS;
					}
					else
					{
						FrontDTS.SetToInvalid();
						for(uint32 i = 1; i < AccessUnits.Num(); ++i)
						{
							if (AccessUnits[i]->DropState == FAccessUnit::EDropState::None)
							{
								FrontDTS = AccessUnits[i]->DTS;
								break;
							}
						}
					}
					if (OutAU->DropState == FAccessUnit::EDropState::None)
					{
						PlayableDuration -= OutAU->Duration;
					}
					PushedDuration -= OutAU->Duration;
				}
				else
				{
					FrontDTS.SetToInvalid();
					PlayableDuration.SetToZero();
					PushedDuration.SetToZero();
				}
				return true;
			}
			else
			{
				OutAU = nullptr;
				return false;
			}
		}

		//!
		bool Peek(FAccessUnit*& OutAU)
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			if (Num())
			{
				OutAU = AccessUnits.Front();
				OutAU->AddRef();
				return true;
			}
			else
			{
				OutAU = nullptr;
				return false;
			}
		}

		//!
		bool ContainsPTS(const FTimeValue& InPTS) const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			if (AccessUnits.Num())
			{
				return AccessUnits.FrontRef()->PTS <= InPTS && InPTS < AccessUnits.BackRef()->PTS + AccessUnits.BackRef()->Duration;
			}
			return false;
		}

		bool ContainsFuturePTS(const FTimeValue& InPTS) const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			if (AccessUnits.Num())
			{
				return InPTS <= AccessUnits.BackRef()->PTS + AccessUnits.BackRef()->Duration;
			}
			return false;
		}


		//! Discards data that has both its DTS and PTS less than the provided ones.
		void DiscardUntil(const FTimeValue& NextValidDTS, const FTimeValue& NextValidPTS, FTimeValue& OutPoppedDTS, FTimeValue& OutPoppedPTS)
		{
			while(true)
			{
				FAccessUnit* NextAU = nullptr;
				if (Peek(NextAU))
				{
					if (NextAU)
					{
						bool bDTS = NextValidDTS.IsValid() ? NextAU->DTS < NextValidDTS : true;
						bool bPTS = NextValidPTS.IsValid() ? NextAU->PTS < NextValidPTS : true;
						if (bDTS && bPTS)
						{
							OutPoppedDTS = NextAU->DTS;
							OutPoppedPTS = NextAU->PTS;
							Pop(NextAU);
							FAccessUnit::Release(NextAU);
						}
						else
						{
							FAccessUnit::Release(NextAU);
							break;
						}
					}
					else
					{
						break;
					}
				}
				else
				{
					break;
				}
			}
		}

		//! Waits for data to arrive. Returns true if data is present. False if not and timeout expired.
		bool WaitForData(int64 waitForMicroseconds = -1)
		{
			bool bHave = NumInSemaphore.Obtain(waitForMicroseconds);
			if (bHave)
			{
				NumInSemaphore.Release();
			}
			return bHave;
		}

		//! Removes all elements from the FIFO
		void Flush()
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			while(AccessUnits.Num())
			{
				FAccessUnit::Release(AccessUnits.Pop());
				NumInSemaphore.TryToObtain();
			}
			CurrentMemInUse = 0;
			bEndOfData = false;
			bLastPushWasBlocked = false;
			FrontDTS.SetToInvalid();
			PushedDuration.SetToZero();
			PlayableDuration.SetToZero();
		}

		//! Checks if the buffer has reached the end-of-data marker (marker is set and no more data is in the buffer).
		bool IsEndOfData() const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			return bEndOfData && AccessUnits.IsEmpty();
		}

		// Checks if the end-of-data flag has been set. There may still be data in the buffer though!
		bool IsEODFlagSet() const
		{
			FMediaCriticalSection::ScopedLock Lock(AccessLock);
			return bEndOfData;
		}

		// Was the last push blocked because the buffer limits were reached?
		bool WasLastPushBlocked() const
		{
			return bLastPushWasBlocked;
		}

		// Helper class to lock the AU buffer
		class FScopedLock : private TMediaNoncopyable<FScopedLock>
		{
		public:
			explicit FScopedLock(const FAccessUnitBuffer& owner)
				: mOwner(owner)
			{
				mOwner.AccessLock.Lock();
			}
			~FScopedLock()
			{
				mOwner.AccessLock.Unlock();
			}
		private:
			FScopedLock();
			const FAccessUnitBuffer& mOwner;
		};

	private:
		//! Checks if an access unit can be pushed to the FIFO. Returns true if successful, false if the FIFO has insufficient free space.
		bool CanPush(const FAccessUnit* AU, const FConfiguration* Limit, const FExternalBufferInfo* ExternalInfo)
		{
			check(Limit->MaxDuration > FTimeValue::GetZero());
			check(AU->Duration.IsValid() && !AU->Duration.IsInfinity());
			if (ExternalInfo == nullptr)
			{
				// Memory ok?
				if (AU->TotalMemSize() + CurrentMemInUse <= Limit->MaxDataSize)
				{
					// Max allowed duration ok?
					return PlayableDuration.IsValid() && PlayableDuration + AU->Duration > Limit->MaxDuration ? false : true;
				}
			}
			else
			{
				// Memory ok?
				if (AU->TotalMemSize() + ExternalInfo->DataSize <= Limit->MaxDataSize)
				{
					// Max allowed duration ok?
					return AU->Duration + ExternalInfo->Duration > Limit->MaxDuration ? false : true;
				}
			}
			return false;
		}

		mutable FMediaCriticalSection				AccessLock;
		FConfiguration								Config;
		FExternalBufferInfo							ZeroExternalInfo;
		TMediaQueueDynamicNoLock<FAccessUnit*>		AccessUnits;
		FMediaSemaphore								NumInSemaphore;
		FTimeValue									FrontDTS;					//!< DTS of first AU in buffer
		FTimeValue									PushedDuration;
		FTimeValue									PlayableDuration;
		int64										CurrentMemInUse;
		bool										bEndOfData;
		bool										bLastPushWasBlocked;
	};



	/**
	 * A multi-track access unit buffer keeps access units from several tracks in individual buffers,
	 * one of which is selected to return AUs to the decoder from. The other unselected tracks will
	 * discard their AUs as the play position progresses.
	 */
	class FMultiTrackAccessUnitBuffer
	{
	public:
		FMultiTrackAccessUnitBuffer();
		~FMultiTrackAccessUnitBuffer();
		void SetParallelTrackMode();
		void CapacitySet(const FAccessUnitBuffer::FConfiguration& Config);
		void SelectTrackWhenAvailable(TSharedPtrTS<FBufferSourceInfo> InBufferSourceInfo);
		void AddUpcomingBuffer(TSharedPtrTS<FBufferSourceInfo> InBufferSourceInfo);
		void Activate();
		void Deselect();
		bool Push(FAccessUnit*& AU);
		void PushEndOfDataFor(TSharedPtrTS<const FBufferSourceInfo> InStreamSourceInfo);
		void PushEndOfDataAll();
		void Flush();
		void GetStats(FAccessUnitBufferInfo& OutStats);
		bool IsDeselected();
		FTimeValue GetLastPoppedPTS();
		TSharedPtrTS<FBufferSourceInfo> GetActiveOutputBufferInfo() const
		{ return ActiveOutputBufferInfo; }

		// Helper class to lock the AU buffer
		class FScopedLock : private TMediaNoncopyable<FScopedLock>
		{
		public:
			explicit FScopedLock(const FMultiTrackAccessUnitBuffer& Self)
				: LockedSelf(Self)
			{
				LockedSelf.AccessLock.Lock();
			}
			~FScopedLock()
			{
				LockedSelf.AccessLock.Unlock();
			}
		private:
			FScopedLock();
			const FMultiTrackAccessUnitBuffer& LockedSelf;
		};

		bool Pop(FAccessUnit*& OutAU);
		void PopDiscardUntil(FTimeValue UntilTime);
		bool IsEODFlagSet();
		int32 Num();
		bool WasLastPushBlocked();

	private:

		struct FQueuedBuffer
		{
			TSharedPtrTS<FBufferSourceInfo> Info;
			TSharedPtrTS<FAccessUnitBuffer> Buffer;
		};

		void PurgeAll();
		void Clear();

		TSharedPtrTS<FAccessUnitBuffer> CreateNewBuffer();
		TSharedPtrTS<FAccessUnitBuffer> GetSelectedTrackBuffer();
		void ActivateBuffer(bool bIsPushing);
		void ChangeOver();
		void RemoveUnusedBuffers();
		void GetEnqueuedBufferInfo(FAccessUnitBuffer::FExternalBufferInfo& OutInfo, bool bForSwitchOverChain);

		FMediaCriticalSection							AccessLock;
		FAccessUnitBuffer::FConfiguration				BufferConfiguration;				//!< Buffer configuration.
		TMap<FString, TSharedPtrTS<FAccessUnitBuffer>>	TrackBuffers;						//!< Map of track buffers. One per track ID.
		TArray<FQueuedBuffer>							UpcomingBufferChain;
		TArray<FQueuedBuffer>							SwitchOverBufferChain;
		TSharedPtrTS<FAccessUnitBuffer>					EmptyBuffer;						//!< An empty buffer
		TSharedPtrTS<FBufferSourceInfo>					ActiveOutputBufferInfo;
		TSharedPtrTS<FBufferSourceInfo>					LastPoppedBufferInfo;
		FTimeValue										LastPoppedDTS;
		FTimeValue										LastPoppedPTS;
		bool											bIsDeselected;
		bool											bEndOfData;
		bool											bLastPushWasBlocked;
		bool											bIsParallelTrackMode;
	};




	/**
	 * Base class for any decoder receiving data in "access units".
	 * An access unit (AU) is considered a data packet that can be sent into a decoder without
	 * the decoder stalling. For a h.264 decoder this is usually a single NALU.
	**/
	class IAccessUnitBufferInterface
	{
	public:
		virtual ~IAccessUnitBufferInterface() = default;

		enum class EAUpushResult
		{
			Ok,
			Full,
			Error,
		};
		//! Attempts to push an access unit to the decoder. Ownership of the access unit is transferred if the push is successful.
		virtual EAUpushResult AUdataPushAU(FAccessUnit* AccessUnit) = 0;
		//! Notifies the decoder that there will be no further access units.
		virtual void AUdataPushEOD() = 0;
		//! Instructs the decoder to flush all pending input and all already decoded output.
		virtual void AUdataFlushEverything() = 0;
	};



	//---------------------------------------------------------------------------------------------------------------------
	/**
	 * A decoder input buffer listener callback to monitor the current state of decoder input buffer levels.
	 *
	 * The decoder will invoke this listener right before it wants to get an access unit from its input buffer,
	 * whether the buffer already contains data or is empty.
	**/
	class IAccessUnitBufferListener
	{
	public:
		virtual ~IAccessUnitBufferListener() = default;

		struct FBufferStats
		{
			FBufferStats()
			{
				Clear();
			}
			void Clear()
			{
				NumAUsAvailable = 0;
				NumBytesInBuffer = 0;
				MaxBytesOfBuffer = 0;
				bEODSignaled = false;
				bEODReached = false;
			}
			int64	NumAUsAvailable;		//!< Number of AUs currently in the buffer.
			int64	NumBytesInBuffer;		//!< Number of bytes currently in the buffer.
			int64	MaxBytesOfBuffer;		//!< Maximum byte capacity this buffer is allowed to store.
			bool	bEODSignaled;			//!< Set after PushEndOfData() has been called
			bool	bEODReached;			//!< Set after PushEndOfData() has been called AND the last AU was taken from the buffer.
		};

		//! Called right before the decoder wants to get an access unit from its input buffer, regardless if it already has data or not.
		virtual void DecoderInputNeeded(const FBufferStats& CurrentInputBufferStats) = 0;
	};



	//---------------------------------------------------------------------------------------------------------------------
	/**
	 * A decoder ready listener callback to monitor the decoder activity.
	 *
	 * The decoder will invoke this listener right before it needs a buffer from its output buffer prior to decoding.
	 * This is called whether or not the output buffer has room for new decoded data or not.
	**/
	class IDecoderOutputBufferListener
	{
	public:
		virtual ~IDecoderOutputBufferListener() = default;

		struct FDecodeReadyStats
		{
			FDecodeReadyStats()
			{
				Clear();
			}
			void Clear()
			{
				ReadyDuration.SetToInvalid();
				NumDecodedElementsReady = 0;
				MaxDecodedElementsReady = 0;
				NumElementsInDecoder = 0;
				bOutputStalled = false;
				bEODreached = false;
			}
			FTimeValue				ReadyDuration;				//!< Duration of the ready material
			int64					NumDecodedElementsReady;	//!< Number of decoded elements ready for rendering
			int64					MaxDecodedElementsReady;	//!< Maximum number of decoded elements.
			int64					NumElementsInDecoder;		//!< Number of elements currently in the decoder pipeline
			bool					bOutputStalled;				//!< true if the output is full and decoding is delayed until there's room again.
			bool					bEODreached;				//!< true when the final decoded element has been passed on.
		};

		virtual void DecoderOutputReady(const FDecodeReadyStats& CurrentReadyStats) = 0;
	};



} // namespace Electra


