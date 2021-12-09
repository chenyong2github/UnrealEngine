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
	void Reserve(int32 InNumSamplesToReserve) override;
	void SetFormatKnownDelegate(FOnFormatKnown InFormatKnownDelegate) override;
	//~ End IBufferedAudioOutput

	//* Common path to receive a new buffer, call from derived classes */
	void OnBufferRecieved(const FBufferFormat& InFormat, TArrayView<const float> InBuffer);

	/** Lock-less buffer to hold the data for the single source we're listening to, interleaved. */
	Audio::TCircularAudioBuffer<float> LocklessCircularBuffer;

	/** Read/Write slim lock protects format known optional below. */
	mutable FRWLock FormatKnownRwLock;

	/** Optional that holds the buffer format, if (and when) it's known. Protected by r/w Slim-lock */
	TOptional<FBufferFormat> KnownFormat;

	/** Delegate that fires when the format it known. Normally on the first buffer received. */
	FOnFormatKnown OnFormatKnown;

	/** Atomic flag we're been started yet. */
	std::atomic<bool> bStarted;
};
