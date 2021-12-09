// Copyright Epic Games, Inc. All Rights Reserved.

#include "BufferedListenerBase.h"
#include "AudioLinkLog.h"

FBufferedListenerBase::FBufferedListenerBase(int32 InDefaultCircularBufferSize)
{
	LocklessCircularBuffer.SetCapacity(InDefaultCircularBufferSize);
}

/** CONSUMER Thread. This will be called by the consuming data of the buffers. */
bool FBufferedListenerBase::PopBuffer(float* InBuffer, int32 InBufferSizeInSamples, int32& OutSamplesWritten)
{
	OutSamplesWritten = LocklessCircularBuffer.Pop(InBuffer, InBufferSizeInSamples);
	return bStarted;
}

/** CONSUMER Thread. This will be called by the consuming data of the buffers. */
bool FBufferedListenerBase::GetFormat(IBufferedAudioOutput::FBufferFormat& OutFormat) const
{
	FReadScopeLock ReadLock(FormatKnownRwLock);
	if (KnownFormat)
	{
		OutFormat = *KnownFormat;
		return true; // success.
	}
	return false;
}

void FBufferedListenerBase::SetFormatKnownDelegate(FOnFormatKnown InFormatKnownDelegate)
{
	OnFormatKnown = InFormatKnownDelegate;
}

/** AUDIO MIXER THREAD. */
void FBufferedListenerBase::OnBufferRecieved(const FBufferFormat& InFormat, TArrayView<const float> InBuffer)
{
	// Keep track of if we need to fire the delegate, so we can do it outside of a lock.
	bool bFireFormatKnownDelegate = false;

	// Do we know the format yet? (do this under a read-only lock, unless we need to change it).
	{
		// Read lock to check if we know the state.
		FRWScopeLock Lock(FormatKnownRwLock, SLT_ReadOnly);

		// Format known?
		if (!KnownFormat)
		{
			{
				// Upgrade lock to Write lock. (Safe here as we are the only writer, and only readers can race between.).
				Lock.ReleaseReadOnlyLockAndAcquireWriteLock_USE_WITH_CAUTION();

				// Write this format as our known format.
				KnownFormat = InFormat;

				// Remember to fire the delegate.
				bFireFormatKnownDelegate = true;
			}
		}
		else
		{
			// Sanity check they haven't changed on the source since it started.
			ensure(InFormat == *KnownFormat);
		}
	}

	// Fire format known delegate. (important this is done outside of the read/write lock above as it calls GetFormat, which needs a read lock).
	if (bFireFormatKnownDelegate)
	{
		// Broadcast to consumer that we know our format and block rate.
		OnFormatKnown.ExecuteIfBound(InFormat);
	}

	// Push the data into the circular buffer.
	int32 SamplesPushed = LocklessCircularBuffer.Push(InBuffer.GetData(), InBuffer.Num());

	// Warn of not enough space in circular buffer
	if (SamplesPushed != InBuffer.Num())
	{
		// Prevent log spam by limiting to 1:100 logs.
		static const int32 NumLogMessagesToSkip = 100;
		static int32 LogPacifier = 0;
		UE_CLOG(LogPacifier++ % NumLogMessagesToSkip == 0, LogAudioLink, Warning, TEXT("Overflow by '%d' Samples in Buffer Listener"), InBuffer.Num() - SamplesPushed);
	}
}

void FBufferedListenerBase::Reserve(int32 InNumSamplesToReserve)
{
	LocklessCircularBuffer.SetCapacity(InNumSamplesToReserve);
}