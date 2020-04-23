// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers\Queue.h"
#include "Templates\Function.h"

enum class EGeneratorState : uint8
{
	IsIdle,
	IsGenerating,
};

class ENGINE_API ISoundGenerator
{
public:
	ISoundGenerator();
	virtual ~ISoundGenerator();

	// Called when a new buffer is required. 
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) = 0;

	// Optional. Called on audio generator thread right when the generator begins generating.
	virtual void OnBeginGenerate() {}

	// Optional. Called on audio generator thread right when the generator ends generating.
	virtual void OnEndGenerate() {}

	// Retrieves the next buffer of audio from the generator, called from the audio mixer
	int32 GetNextBuffer(float* OutAudio, int32 NumSamples);

	// Returns the current state of the sound generator
	EGeneratorState GetGeneratorState() const { return GeneratorState; }

protected:

	// Protected method to execute lambda in audio render thread
	// Used for conveying parameter changes or events to the generator thread.
	void SynthCommand(TUniqueFunction<void()> Command);

private:

	void PumpPendingMessages();

	// The current state of the generator
	EGeneratorState GeneratorState;

	// The command queue used to convey commands from game thread to generator thread 
	TQueue<TUniqueFunction<void()>> CommandQueue;

	friend class USynthComponent;
};

typedef TSharedPtr<ISoundGenerator, ESPMode::ThreadSafe> ISoundGeneratorPtr;