// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Templates/Function.h"

class ENGINE_API ISoundGenerator
{
public:
	ISoundGenerator();
	virtual ~ISoundGenerator();

	// Called when a new buffer is required. 
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) = 0;

	// Returns the number of samples to render per callback
	virtual int32 GetDesiredNumSamplesToRenderPerCallback() const { return 1024; }

	// Optional. Called on audio generator thread right when the generator begins generating.
	virtual void OnBeginGenerate() {}

	// Optional. Called on audio generator thread right when the generator ends generating.
	virtual void OnEndGenerate() {}

	// Retrieves the next buffer of audio from the generator, called from the audio mixer
	int32 GetNextBuffer(float* OutAudio, int32 NumSamples, bool bRequireNumberSamples = false);

protected:

	// Protected method to execute lambda in audio render thread
	// Used for conveying parameter changes or events to the generator thread.
	void SynthCommand(TUniqueFunction<void()> Command);

private:

	void PumpPendingMessages();

	// The command queue used to convey commands from game thread to generator thread 
	TQueue<TUniqueFunction<void()>> CommandQueue;

	friend class USynthComponent;
};

// Null implementation of ISoundGenerator which no-ops audio generation
class ENGINE_API FSoundGeneratorNull : public ISoundGenerator
{
public:
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
	{
		return NumSamples;
	}
};

typedef TSharedPtr<ISoundGenerator, ESPMode::ThreadSafe> ISoundGeneratorPtr;