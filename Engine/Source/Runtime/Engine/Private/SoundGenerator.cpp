// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundGenerator.h"

ISoundGenerator::ISoundGenerator()
{
}

ISoundGenerator::~ISoundGenerator()
{
}

int32 ISoundGenerator::GetNextBuffer(float* OutAudio, int32 NumSamples)
{
	PumpPendingMessages();

	return OnGenerateAudio(OutAudio, NumSamples);
}

void ISoundGenerator::SynthCommand(TUniqueFunction<void()> Command)
{
	CommandQueue.Enqueue(MoveTemp(Command));
}

void ISoundGenerator::PumpPendingMessages()
{
	TUniqueFunction<void()> Command;
	while (CommandQueue.Dequeue(Command))
	{
		Command();
	}
}


