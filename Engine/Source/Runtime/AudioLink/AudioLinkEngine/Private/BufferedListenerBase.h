// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioDevice.h"
#include "SignalProcessing/Public/DSP/Dsp.h"

#include "IBufferedAudioOutput.h"
#include "Misc/Optional.h"
#include "Containers/ArrayView.h"

/** Common base class of Buffered Listener objects.
*/
class AUDIOLINKENGINE_API FBufferedListenerBase : public IBufferedAudioOutput
{
protected:
	FBufferedListenerBase(int32 InDefaultCircularBufferSize);
	virtual ~FBufferedListenerBase() = default;

	//~ Begin IBufferedAudioOutput
	bool PopBuffer(float* InBuffer, int32 InBufferSizeInSamples, int32& OutSamplesWritten) override;
	bool GetFormat(IBufferedAudioOutput::FBufferFormat& OutFormat) const override;
	void Reserve(int32 InNumSamplesToReserve, int32 InNumSamplesOfSilence) override;
	void SetFormatKnownDelegate(FOnFormatKnown InFormatKnownDelegate) override;
	void SetBufferStreamEndDelegate(FOnBufferStreamEnd) override {}
	//~ End IBufferedAudioOutput

	//* Common path to receive a new buffer, call from derived classes */
	void OnBufferReceived(const FBufferFormat& InFormat, TArrayView<const float> InBuffer);

	//* Reset the format of the buffer */
	void ResetFormat();

	//* Set the format of the buffer */
	void SetFormat(const FBufferFormat& InFormat);
	
	//* Ask if the started flag has been set. Note this is non-atomic, as it could change during the call */	
	bool IsStartedNonAtomic() const;
		
	//* Attempt to set our state to started. Not this can fail if we're already started. */	
	bool TrySetStartedFlag(); 
	
	//* Attempt to set started to false. This can fail if we're already stopped. */	
	bool TryUnsetStartedFlag();

private:	
	void PushSilence(int32 InNumSamplesOfSilence);

	/** Lock-less buffer to hold the data for the single source we're listening to, interleaved. */
	Audio::TCircularAudioBuffer<float> LocklessCircularBuffer;

	/** Read/Write slim lock protects format known optional */
	mutable FRWLock FormatKnownRwLock;

	/** Optional that holds the buffer format, if (and when) it's known. Protected by r/w Slim-lock */
	TOptional<FBufferFormat> KnownFormat;

	/** Delegate that fires when the format it known. Normally on the first buffer received. */
	FOnFormatKnown OnFormatKnown;

	/** Count for how much (initial) silence is still remaining in the Circular buffer, for logging purposes */
	std::atomic<int32> NumSilentSamplesRemaining;

	/** Atomic flag we've been started */
	std::atomic<bool> bStarted;
};
