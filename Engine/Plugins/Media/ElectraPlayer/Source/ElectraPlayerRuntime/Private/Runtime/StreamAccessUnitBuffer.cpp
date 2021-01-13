// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamAccessUnitBuffer.h"


namespace Electra
{



	FMultiTrackAccessUnitBuffer::FMultiTrackAccessUnitBuffer()
	{
		EmptyBuffer = CreateNewBuffer();
		ActiveOutputID = -1;
		LastPoppedBufferID = -1;
		LastPoppedDTS.SetToInvalid();
		LastPoppedPTS.SetToInvalid();
		bAutoselectFirstTrack = false;
		bIsDeselected = false;
		bEndOfData = false;
		bLastPushWasBlocked = false;
	}

	FMultiTrackAccessUnitBuffer::FAccessUnitBufferPtr FMultiTrackAccessUnitBuffer::CreateNewBuffer()
	{
		// All buffers here are internally unbounded as we need them to accept all incoming data.
		// Limits are used on the active buffer only.
		FAccessUnitBufferPtr NewBuffer = MakeShared<FAccessUnitBuffer, ESPMode::ThreadSafe>();
		NewBuffer->CapacitySet(FAccessUnitBuffer::FConfiguration(1024 << 20, 3600.0));
		return NewBuffer;
	}

	FMultiTrackAccessUnitBuffer::~FMultiTrackAccessUnitBuffer()
	{
		PurgeAll();
	}

	void FMultiTrackAccessUnitBuffer::CapacitySet(const FAccessUnitBuffer::FConfiguration& Config)
	{
		PrimaryBufferConfiguration = Config;
	}

	bool FMultiTrackAccessUnitBuffer::Push(FAccessUnit*& AU)
	{
		uint32 TrackID = 0;
		if (AU->StreamSourceInfo.IsValid())
		{
			TrackID = AU->StreamSourceInfo->NumericTrackID;
		}
		// Do we have a buffer for this track ID yet?
		AccessLock.Lock();
		if (!TrackBuffers.Contains(TrackID))
		{
			TrackBuffers.Emplace(TrackID, CreateNewBuffer());
		}
		FAccessUnitBufferPtr TrackBuffer = TrackBuffers[TrackID];

		if (bAutoselectFirstTrack)
		{
			bAutoselectFirstTrack = false;
			ActiveOutputID = (int32)TrackID;
		}

		// Pushing data to either buffer means that there is data and we are not at EOD here any more.
		// This does not necessarily mean that the selected track is not at EOD. Just that some track is not.
		bEndOfData = false;
		AccessLock.Unlock();
		// If this is the active buffer or no active buffer has been set yet we use the configured buffer limit in pushing.
		const FAccessUnitBuffer::FConfiguration* PushConfig = ActiveOutputID == -1 || ActiveOutputID == (int32)TrackID ? &PrimaryBufferConfiguration : nullptr;
		bool bWasPushed = TrackBuffer->Push(AU, PushConfig);
		bLastPushWasBlocked = TrackBuffer->WasLastPushBlocked();
		return bWasPushed;
	}

	void FMultiTrackAccessUnitBuffer::PushEndOfDataFor(TSharedPtr<const FStreamSourceInfo, ESPMode::ThreadSafe> InStreamSourceInfo)
	{
		uint32 TrackID = 0;
		if (InStreamSourceInfo.IsValid())
		{
			TrackID = InStreamSourceInfo->NumericTrackID;
		}
		// Do we have a buffer for this track ID yet?
		AccessLock.Lock();
		if (!TrackBuffers.Contains(TrackID))
		{
			TrackBuffers.Emplace(TrackID, CreateNewBuffer());
		}
		FAccessUnitBufferPtr TrackBuffer = TrackBuffers[TrackID];

		if (bAutoselectFirstTrack)
		{
			bAutoselectFirstTrack = false;
			ActiveOutputID = (int32)TrackID;
		}

		AccessLock.Unlock();
		TrackBuffer->PushEndOfData();
	}

	void FMultiTrackAccessUnitBuffer::PushEndOfDataAll()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		bEndOfData = true;
		// Push an end-of-data into all tracks.
		for (auto& It : TrackBuffers)
		{
			It.Value->PushEndOfData();
		}
	}

	void FMultiTrackAccessUnitBuffer::Flush()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		// Flush all existing buffers but keep them in the track map.
		for (auto& It : TrackBuffers)
		{
			It.Value->Flush();
		}
		LastPoppedDTS.SetToInvalid();
		LastPoppedPTS.SetToInvalid();
		bEndOfData = false;
		bLastPushWasBlocked = false;
		LastPoppedBufferID = -1;
	}

	void FMultiTrackAccessUnitBuffer::PurgeAll()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		// Get rid of all buffers
		TrackBuffers.Empty();
		LastPoppedDTS.SetToInvalid();
		LastPoppedPTS.SetToInvalid();
		bEndOfData = false;
		bLastPushWasBlocked = false;
		LastPoppedBufferID = -1;
	}

	void FMultiTrackAccessUnitBuffer::AutoselectFirstTrack()
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		bAutoselectFirstTrack = true;
		bIsDeselected = false;
	}


	void FMultiTrackAccessUnitBuffer::SelectTrackByID(int32 TrackID)
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		if (TrackID != ActiveOutputID)
		{
			ActiveOutputID = TrackID;
		}
		bAutoselectFirstTrack = false;
		bIsDeselected = false;
	}

	void FMultiTrackAccessUnitBuffer::Deselect()
	{
		bIsDeselected = true;
		// Note: We do not change ActiveOutputID or bAutoselectFirstTrack.
		//       The buffer actually remains selected in order to track its fullness etc.
		//       Deselected only means that the access unit may not get sent to the decoder
		//       or be replaced by one that produces no output.
	}

	bool FMultiTrackAccessUnitBuffer::IsDeselected() const
	{
		return bIsDeselected;
	}

	FTimeValue FMultiTrackAccessUnitBuffer::GetLastPoppedPTS() const
	{
		FMediaCriticalSection::ScopedLock lock(AccessLock);
		return LastPoppedPTS;
	}


	TSharedPtr<FAccessUnitBuffer, ESPMode::ThreadSafe> FMultiTrackAccessUnitBuffer::GetSelectedTrackBuffer()
	{
		FAccessUnitBufferPtr TrackBuffer;
		AccessLock.Lock();
		if (ActiveOutputID != -1 && TrackBuffers.Contains(ActiveOutputID))
		{
			TrackBuffer = TrackBuffers[ActiveOutputID];
		}
		else
		{
			TrackBuffer = EmptyBuffer;
		}
		AccessLock.Unlock();
		return TrackBuffer;
	}

	TSharedPtr<const FAccessUnitBuffer, ESPMode::ThreadSafe> FMultiTrackAccessUnitBuffer::GetSelectedTrackBuffer() const
	{
		FAccessUnitBufferPtr TrackBuffer;
		AccessLock.Lock();
		if (ActiveOutputID != -1 && TrackBuffers.Contains(ActiveOutputID))
		{
			TrackBuffer = TrackBuffers[ActiveOutputID];
		}
		else
		{
			TrackBuffer = EmptyBuffer;
		}
		AccessLock.Unlock();
		return TrackBuffer;
	}

	void FMultiTrackAccessUnitBuffer::GetStats(FAccessUnitBufferInfo& OutStats)
	{
		// Stats are always returned with our configuration.
		TSharedPtr<const FAccessUnitBuffer, ESPMode::ThreadSafe> Buf = GetSelectedTrackBuffer();
		Buf->GetStats(OutStats, &PrimaryBufferConfiguration);
		if (Buf == EmptyBuffer)
		{
			OutStats.bEndOfData = bEndOfData;
		}
	}

	bool FMultiTrackAccessUnitBuffer::Pop(FAccessUnit*& OutAU)
	{
		// Note: We assume the access lock is held by the caller!
		TSharedPtr<FAccessUnitBuffer, ESPMode::ThreadSafe> Buf = FMultiTrackAccessUnitBuffer::GetSelectedTrackBuffer();
		if (Buf->Num())
		{
			bool bDidPop = Buf->Pop(OutAU);
			if (bDidPop && OutAU)
			{
				// Did we just pop from a different buffer than last time?
				if (LastPoppedBufferID != -1 && LastPoppedBufferID != ActiveOutputID)
				{
					// FIXME: We may need to discard everything that is not tagged as a sync sample to ensure proper stream switching.
					OutAU->bTrackChangeDiscontinuity = true;
				}
				// Remember from which buffer we popped.
				LastPoppedBufferID = ActiveOutputID;
				LastPoppedDTS = OutAU->DTS;
				LastPoppedPTS = OutAU->PTS;
				// With this AU being popped off now we will also pop off all AUs that are now obsolete in the other tracks.
				for (auto& It : TrackBuffers)
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
		for (auto& It : TrackBuffers)
		{
			FAccessUnitBufferPtr Buf = It.Value;
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


	bool FMultiTrackAccessUnitBuffer::IsEODFlagSet() const
	{
		// Note: We assume the access lock is held by the caller!
		TSharedPtr<const FAccessUnitBuffer, ESPMode::ThreadSafe> Buf = GetSelectedTrackBuffer();
		return Buf == EmptyBuffer ? bEndOfData : Buf->IsEODFlagSet();
	}

	int32 FMultiTrackAccessUnitBuffer::Num() const
	{
		// Note: We assume the access lock is held by the caller!
		return (int32)GetSelectedTrackBuffer()->Num();
	}

	bool FMultiTrackAccessUnitBuffer::WasLastPushBlocked() const
	{
		return bLastPushWasBlocked;
	}


} // namespace Electra


