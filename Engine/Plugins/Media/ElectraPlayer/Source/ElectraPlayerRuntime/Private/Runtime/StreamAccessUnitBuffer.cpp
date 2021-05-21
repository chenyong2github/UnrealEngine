// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamAccessUnitBuffer.h"

namespace Electra
{
	FMultiTrackAccessUnitBuffer::FMultiTrackAccessUnitBuffer()
	{
		EmptyBuffer = CreateNewBuffer();
		LastPoppedDTS.SetToInvalid();
		LastPoppedPTS.SetToInvalid();
		bIsDeselected = false;
		bEndOfData = false;
		bLastPushWasBlocked = false;
		bIsParallelTrackMode = false;
	}

	TSharedPtrTS<FAccessUnitBuffer> FMultiTrackAccessUnitBuffer::CreateNewBuffer()
	{
		// All buffers here are internally unbounded as we need them to accept all incoming data.
		// Limits are used on the active buffer only.
		TSharedPtrTS<FAccessUnitBuffer> NewBuffer = MakeSharedTS<FAccessUnitBuffer>();
		NewBuffer->CapacitySet(FAccessUnitBuffer::FConfiguration(1024 << 20, 3600.0));
		return NewBuffer;
	}

	FMultiTrackAccessUnitBuffer::~FMultiTrackAccessUnitBuffer()
	{
		PurgeAll();
	}

	void FMultiTrackAccessUnitBuffer::SetParallelTrackMode()
	{
		bIsParallelTrackMode = true;
	}


	void FMultiTrackAccessUnitBuffer::CapacitySet(const FAccessUnitBuffer::FConfiguration& Config)
	{
		BufferConfiguration = Config;
	}

	bool FMultiTrackAccessUnitBuffer::Push(FAccessUnit*& AU)
	{
		check(AU->BufferSourceInfo.IsValid());

		FString TrackID = AU->BufferSourceInfo->PeriodAdaptationSetID;
		// Do we have a buffer for this track ID yet?
		AccessLock.Lock();
		if (!TrackBuffers.Contains(TrackID))
		{
			TrackBuffers.Emplace(TrackID, CreateNewBuffer());
		}
		TSharedPtrTS<FAccessUnitBuffer> TrackBuffer = TrackBuffers[TrackID];

		ActivateBuffer(true);

		// Pushing data to either buffer means that there is data and we are not at EOD here any more.
		// This does not necessarily mean that the selected track is not at EOD. Just that some track is not.
		bEndOfData = false;


		bool bIsSwitchOverAU = false;
		for(auto &SwitchBuf : SwitchOverBufferChain)
		{
			if (SwitchBuf.Info->PeriodAdaptationSetID.Equals(TrackID))
			{
				bIsSwitchOverAU = true;
				break;
			}
		}

		// Get the combined buffer utilization of enqueued buffers.
		FAccessUnitBuffer::FExternalBufferInfo EnqueuedBufferInfo;
		GetEnqueuedBufferInfo(EnqueuedBufferInfo, bIsSwitchOverAU);

		AccessLock.Unlock();

		bool bWasPushed = TrackBuffer->Push(AU, &BufferConfiguration, &EnqueuedBufferInfo);
		bLastPushWasBlocked = TrackBuffer->WasLastPushBlocked();

		return bWasPushed;
	}

	void FMultiTrackAccessUnitBuffer::PushEndOfDataFor(TSharedPtrTS<const FBufferSourceInfo> InStreamSourceInfo)
	{
		check(InStreamSourceInfo.IsValid());
		
		FString TrackID = InStreamSourceInfo->PeriodAdaptationSetID;
		// Do we have a buffer for this track ID yet?
		AccessLock.Lock();
		if (!TrackBuffers.Contains(TrackID))
		{
			TrackBuffers.Emplace(TrackID, CreateNewBuffer());
		}
		TSharedPtrTS<FAccessUnitBuffer> TrackBuffer = TrackBuffers[TrackID];

		ActivateBuffer(true);

		AccessLock.Unlock();
		TrackBuffer->PushEndOfData();
	}

	void FMultiTrackAccessUnitBuffer::PushEndOfDataAll()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		bEndOfData = true;
		// Push an end-of-data into all tracks.
		for(auto& It : TrackBuffers)
		{
			It.Value->PushEndOfData();
		}
	}

	void FMultiTrackAccessUnitBuffer::Clear()
	{
		LastPoppedDTS.SetToInvalid();
		LastPoppedPTS.SetToInvalid();
		bEndOfData = false;
		bLastPushWasBlocked = false;
		ActiveOutputBufferInfo.Reset();
		LastPoppedBufferInfo.Reset();
		UpcomingBufferChain.Empty();
		SwitchOverBufferChain.Empty();
	}

	void FMultiTrackAccessUnitBuffer::Flush()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		// Flush all existing buffers but keep them in the track map.
		for(auto& It : TrackBuffers)
		{
			It.Value->Flush();
		}
		Clear();
	}

	void FMultiTrackAccessUnitBuffer::PurgeAll()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		// Get rid of all buffers
		TrackBuffers.Empty();
		Clear();
	}


	void FMultiTrackAccessUnitBuffer::SelectTrackWhenAvailable(TSharedPtrTS<FBufferSourceInfo> InBufferSourceInfo)
	{
		if (InBufferSourceInfo.IsValid())
		{
			FMediaCriticalSection::ScopedLock lock(AccessLock);
			if (SwitchOverBufferChain.Num() == 0)
			{
				FQueuedBuffer Next;
				Next.Info = MoveTemp(InBufferSourceInfo);
				SwitchOverBufferChain.Empty();
				SwitchOverBufferChain.Emplace(MoveTemp(Next));
			}
			else if (!SwitchOverBufferChain.Last().Info->PeriodAdaptationSetID.Equals(InBufferSourceInfo->PeriodAdaptationSetID))
			{
				/*
					We have to check if what we are to switch to is what is already active.
					This can happen on quick track changes where we are currently on track1, then a change to track2 was issued
					but immediately chaged back to track1. So: track1 -> track2 -> track1
					In this case we would remove the request for track2 and see track1 -> track1. Since the ID of this track is
					still the same we would append the data coming in from the new track1 request to the already existing track1
					buffer. This is bad because segments are streamed in starting at their beginning and we would thus put data
					into the buffer that is already there.
					To not make things overly complex we will do the following if the switch-to track is the currently active one:
					We rename the active one by appending some suffix. This works because the current stream download was already
					canceled and no new data is coming in from that stream. We need to make sure we change the ID in the current
					track map (the key) as well as the source info. The source info is maintained as a shared pointer, so changing
					it there means it is automatically changed everywhere that pointer is shared!
				*/

				if (ActiveOutputBufferInfo.IsValid() && ActiveOutputBufferInfo->PeriodAdaptationSetID.Equals(InBufferSourceInfo->PeriodAdaptationSetID))
				{
					// The ID may be the same but the TSharedPtr MUST NOT be.
					// If it were the same pointer we would be renaming the new segment buffer and info as well!
					check(ActiveOutputBufferInfo != InBufferSourceInfo);

					// First let's rename the buffer in the track buffer map. Double check that it is actually there.
					FString NewName = InBufferSourceInfo->PeriodAdaptationSetID + TEXT("$1");
					if (TrackBuffers.Contains(InBufferSourceInfo->PeriodAdaptationSetID))
					{
						TSharedPtrTS<FAccessUnitBuffer> TrackBuffer = TrackBuffers[ActiveOutputBufferInfo->PeriodAdaptationSetID];
						TrackBuffers.Remove(ActiveOutputBufferInfo->PeriodAdaptationSetID);
						TrackBuffers.Emplace(NewName, TrackBuffer);
					}
					// Then change in the name in the active output buffer info.
					ActiveOutputBufferInfo->PeriodAdaptationSetID = NewName;

					// Finally, if the LastPoppedBufferInfo is set the it SHOULD be the same as the ActiveOutputBufferInfo,
					// ie: check(LastPoppedBufferInfo == ActiveOutputBufferInfo);
					// But in case it is not (due to future code changes) let's update it as well.
					if (LastPoppedBufferInfo.IsValid() && LastPoppedBufferInfo->PeriodAdaptationSetID.Equals(InBufferSourceInfo->PeriodAdaptationSetID))
					{
						LastPoppedBufferInfo->PeriodAdaptationSetID = NewName;
					}
				}

				FQueuedBuffer Next;
				Next.Info = MoveTemp(InBufferSourceInfo);
				SwitchOverBufferChain.Empty();
				SwitchOverBufferChain.Emplace(MoveTemp(Next));
			}
		}
	}

	void FMultiTrackAccessUnitBuffer::AddUpcomingBuffer(TSharedPtrTS<FBufferSourceInfo> InBufferSourceInfo)
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		// Add to the current or the switch-over chain?
		if (SwitchOverBufferChain.Num())
		{
			if (!SwitchOverBufferChain.Last().Info->PeriodAdaptationSetID.Equals(InBufferSourceInfo->PeriodAdaptationSetID))
			{
				FQueuedBuffer Next;
				Next.Info = MoveTemp(InBufferSourceInfo);
				SwitchOverBufferChain.Emplace(MoveTemp(Next));
			}
		}
		else
		{
			if (UpcomingBufferChain.Num() == 0 || !UpcomingBufferChain.Last().Info->PeriodAdaptationSetID.Equals(InBufferSourceInfo->PeriodAdaptationSetID))
			{
				FQueuedBuffer Next;
				Next.Info = MoveTemp(InBufferSourceInfo);
				UpcomingBufferChain.Emplace(MoveTemp(Next));
			}
		}
	}


	void FMultiTrackAccessUnitBuffer::Activate()
	{
		bIsDeselected = false;
	}

	void FMultiTrackAccessUnitBuffer::Deselect()
	{
		// The buffer remains selected in order to track its fullness etc.
		// Deselected only means that the access unit may not get sent to the decoder
		// or be replaced by one that produces no output.
		bIsDeselected = true;
	}

	bool FMultiTrackAccessUnitBuffer::IsDeselected()
	{
		return bIsDeselected;
	}

	FTimeValue FMultiTrackAccessUnitBuffer::GetLastPoppedPTS()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		return LastPoppedPTS;
	}


	TSharedPtrTS<FAccessUnitBuffer> FMultiTrackAccessUnitBuffer::GetSelectedTrackBuffer()
	{
		TSharedPtrTS<FAccessUnitBuffer> TrackBuffer;
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		if (ActiveOutputBufferInfo.IsValid() && TrackBuffers.Contains(ActiveOutputBufferInfo->PeriodAdaptationSetID))
		{
			TrackBuffer = TrackBuffers[ActiveOutputBufferInfo->PeriodAdaptationSetID];
		}
		else
		{
			TrackBuffer = EmptyBuffer;
		}
		return TrackBuffer;
	}

	void FMultiTrackAccessUnitBuffer::GetStats(FAccessUnitBufferInfo& OutStats)
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		// Stats are always returned with the configuration for one buffer, not
		// any summation of enqueued buffers.
		TSharedPtrTS<const FAccessUnitBuffer> Buf = GetSelectedTrackBuffer();
		Buf->GetStats(OutStats, &BufferConfiguration);
		if (Buf == EmptyBuffer)
		{
			OutStats.bEndOfData = bEndOfData;
		}
		else
		{
			// Add the stats of any enqueued buffers
			for(int32 i=0; i<UpcomingBufferChain.Num(); ++i)
			{
				if (UpcomingBufferChain[i].Buffer.IsValid())
				{
					FAccessUnitBufferInfo NextStats;
					UpcomingBufferChain[i].Buffer->GetStats(NextStats);
					OutStats.PushedDuration += NextStats.PushedDuration;
					OutStats.PlayableDuration += NextStats.PlayableDuration;
					OutStats.CurrentMemInUse += NextStats.CurrentMemInUse;
					OutStats.NumCurrentAccessUnits += NextStats.NumCurrentAccessUnits;
					// With chained buffers the end of data and last push blocked flags are only
					// relevant on the last buffer in the chain.
					OutStats.bEndOfData = NextStats.bEndOfData;
					OutStats.bLastPushWasBlocked = NextStats.bLastPushWasBlocked;
				}
			}
		}
	}

	void FMultiTrackAccessUnitBuffer::ChangeOver()
	{
		ActiveOutputBufferInfo = SwitchOverBufferChain[0].Info;
		SwitchOverBufferChain.RemoveAt(0);
		UpcomingBufferChain = MoveTemp(SwitchOverBufferChain);
		RemoveUnusedBuffers();
	}

	void FMultiTrackAccessUnitBuffer::RemoveUnusedBuffers()
	{
		if (!bIsParallelTrackMode)
		{
			auto IsReferencesInChain = [&](const FString& ID, TArray<FQueuedBuffer>& BufferChain) -> bool
			{
				for(auto &Next : BufferChain)
				{
					if (Next.Info.IsValid() && Next.Info->PeriodAdaptationSetID.Equals(ID))
					{
						return true;
					}
				}
				return false;
			};

			TArray<FString> UnusedBuffers;
			for(auto &Buffer : TrackBuffers)
			{
				const FString& ID = Buffer.Key;
				bool bIsReferenced = (ActiveOutputBufferInfo.IsValid() && ActiveOutputBufferInfo->PeriodAdaptationSetID.Equals(ID)) || IsReferencesInChain(ID, UpcomingBufferChain) || IsReferencesInChain(ID, SwitchOverBufferChain);
				if (!bIsReferenced)
				{
					UnusedBuffers.Emplace(ID);
				}
			}
			for(auto &Buffer : UnusedBuffers)
			{
				TrackBuffers.Remove(Buffer);
			}
		}
	}

	void FMultiTrackAccessUnitBuffer::ActivateBuffer(bool bIsPushing)
	{
		auto UpdateBufferChainBuffers = [&](TArray<FQueuedBuffer>& BufferChain) -> void
		{
			for(auto &Next : BufferChain)
			{
				if (!Next.Buffer.IsValid() && TrackBuffers.Contains(Next.Info->PeriodAdaptationSetID))
				{
					Next.Buffer = TrackBuffers[Next.Info->PeriodAdaptationSetID];
				}
			}
		};


		// Update the upcoming buffer chains for GetStats().
		UpdateBufferChainBuffers(UpcomingBufferChain);
		UpdateBufferChainBuffers(SwitchOverBufferChain);

		if (!ActiveOutputBufferInfo.IsValid())
		{
			check(UpcomingBufferChain.Num() == 0);
			if (SwitchOverBufferChain.Num())
			{
				ChangeOver();
			}
		}
		// Upon pushing data to the buffer we only need to activate the track and update the buffer chains
		// for statistics. Buffer switching is handled when popping data.
		if (bIsPushing)
		{
			return;
		}

		if (SwitchOverBufferChain.Num())
		{
			// In parallel track mode we switch immediately as the individual buffers are
			// time synchronized already.
			if (bIsParallelTrackMode)
			{
				ChangeOver();
			}
			else
			{
				FTimeValue PoppedDTS, PoppedPTS;
				FTimeValue PopUntilDTS, PopUntilPTS;
				PopUntilDTS = LastPoppedDTS;
				PopUntilPTS = LastPoppedPTS;
				bool bSwitch = false;
				if (PopUntilDTS.IsValid() && PopUntilPTS.IsValid())
				{
					for(int32 nBuf=0; nBuf<SwitchOverBufferChain.Num(); ++nBuf)
					{
						if (SwitchOverBufferChain[nBuf].Buffer.IsValid())
						{
							// Remove AUs from the buffer that are outdated.
							SwitchOverBufferChain[nBuf].Buffer->DiscardUntil(PopUntilDTS, PopUntilPTS, PoppedDTS, PoppedPTS);

							// Does it contain the time we want to switch at?
							if (SwitchOverBufferChain[nBuf].Buffer->ContainsPTS(PopUntilPTS))
							{
								bSwitch = true;
								break;
							}
							// If the buffer is now empty with another non-empty one following we can remove this one.
							if (SwitchOverBufferChain[nBuf].Buffer->Num() == 0 && nBuf+1 < SwitchOverBufferChain.Num() && SwitchOverBufferChain[nBuf+1].Buffer.IsValid() && SwitchOverBufferChain[nBuf+1].Buffer->Num())
							{
								SwitchOverBufferChain.RemoveAt(nBuf);
								--nBuf;
							}
						}
						else
						{
							// If this buffer is not valid (yet) then the following ones are not either.
							break;
						}
					}
				}
				else
				{
					// When no data was popped from the buffer yet do an immediate switch.
					bSwitch = true;
				}
				if (bSwitch)
				{
					ChangeOver();
				}
			}
		}

		// Moving into the next enqueued buffer?
		if (UpcomingBufferChain.Num())
		{
			TSharedPtrTS<FAccessUnitBuffer> ActiveBuffer = GetSelectedTrackBuffer();
			if (ActiveBuffer->Num() == 0 && UpcomingBufferChain[0].Buffer.IsValid() && UpcomingBufferChain[0].Buffer->Num())
			{
				ActiveOutputBufferInfo = UpcomingBufferChain[0].Info;
				UpcomingBufferChain.RemoveAt(0);
				RemoveUnusedBuffers();
			}
		}
	}


	bool FMultiTrackAccessUnitBuffer::Pop(FAccessUnit*& OutAU)
	{
		// Note: We assume the access lock is held by the caller!
		ActivateBuffer(false);

		TSharedPtrTS<FAccessUnitBuffer> Buf = FMultiTrackAccessUnitBuffer::GetSelectedTrackBuffer();
		if (Buf->Num())
		{
			bool bDidPop = Buf->Pop(OutAU);
			if (bDidPop && OutAU)
			{
				// Did we just pop from a different buffer than last time?
				if (LastPoppedBufferInfo != ActiveOutputBufferInfo)
				{
					// FIXME: We may need to discard everything that is not tagged as a sync sample to ensure proper stream switching.
					OutAU->bTrackChangeDiscontinuity = true;

					// Remember from which buffer we popped.
					LastPoppedBufferInfo = ActiveOutputBufferInfo;
				}
				LastPoppedDTS = OutAU->DTS;
				LastPoppedPTS = OutAU->PTS;
				// With this AU being popped off now we will also pop off all AUs that are now obsolete in the other tracks.
				for(auto& It : TrackBuffers)
				{
					if (It.Value != Buf)
					{
						FTimeValue PoppedDTS, PoppedPTS;
						It.Value->DiscardUntil(LastPoppedDTS, LastPoppedPTS, PoppedDTS, PoppedPTS);
					}
				}
			}
			return bDidPop;
		}
		else
		{
			OutAU = nullptr;
			return false;
		}
	}

	void FMultiTrackAccessUnitBuffer::PopDiscardUntil(FTimeValue UntilTime)
	{
		// Note: We assume the access lock is held by the caller!
		for(auto& It : TrackBuffers)
		{
			TSharedPtrTS<FAccessUnitBuffer> Buf = It.Value;
			FTimeValue PoppedDTS, PoppedPTS;
			Buf->DiscardUntil(UntilTime, UntilTime, PoppedDTS, PoppedPTS);
			if (PoppedDTS.IsValid() && LastPoppedDTS.IsValid() && PoppedDTS > LastPoppedDTS)
			{
				LastPoppedDTS = PoppedDTS;
			}
			if (PoppedPTS.IsValid() && LastPoppedPTS.IsValid() && PoppedPTS > LastPoppedPTS)
			{
				LastPoppedPTS = PoppedPTS;
			}
		}
	}


	bool FMultiTrackAccessUnitBuffer::IsEODFlagSet()
	{
		// Note: We assume the access lock is held by the caller!
		TSharedPtrTS<FAccessUnitBuffer> Buf = GetSelectedTrackBuffer();
		return Buf == EmptyBuffer ? bEndOfData : Buf->IsEODFlagSet();
	}

	int32 FMultiTrackAccessUnitBuffer::Num()
	{
		// Note: We assume the access lock is held by the caller!
		int64 TotalNum = GetSelectedTrackBuffer()->Num();
		// Add the enqueued buffers
		for(int32 i=0; i<UpcomingBufferChain.Num(); ++i)
		{
			if (UpcomingBufferChain[i].Buffer.IsValid())
			{
				TotalNum += UpcomingBufferChain[i].Buffer->Num();
			}
		}
		return (int32) TotalNum;
	}

	void FMultiTrackAccessUnitBuffer::GetEnqueuedBufferInfo(FAccessUnitBuffer::FExternalBufferInfo& OutInfo, bool bForSwitchOverChain)
	{
		if (!bForSwitchOverChain)
		{
			TSharedPtrTS<FAccessUnitBuffer> Buf = GetSelectedTrackBuffer();
			OutInfo.DataSize = Buf->AllocatedSize();
			OutInfo.Duration = Buf->GetPlayableDuration();
			for(int32 i=0; i<UpcomingBufferChain.Num(); ++i)
			{
				if (UpcomingBufferChain[i].Buffer.IsValid())
				{
					OutInfo.DataSize += UpcomingBufferChain[i].Buffer->AllocatedSize();
					OutInfo.Duration += UpcomingBufferChain[i].Buffer->GetPlayableDuration();
				}
			}
		}
		else
		{
			for(auto &SwitchBuf : SwitchOverBufferChain)
			{
				if (SwitchBuf.Buffer.IsValid())
				{
					OutInfo.DataSize += SwitchBuf.Buffer->AllocatedSize();
					OutInfo.Duration += SwitchBuf.Buffer->GetPlayableDuration();
				}
			}
		}
	}

	bool FMultiTrackAccessUnitBuffer::WasLastPushBlocked()
	{
		return bLastPushWasBlocked;
	}


} // namespace Electra


