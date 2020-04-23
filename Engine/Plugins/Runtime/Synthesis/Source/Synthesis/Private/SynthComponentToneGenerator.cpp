// Copyright Epic Games, Inc. All Rights Reserved.


#include "SynthComponents/SynthComponentToneGenerator.h"

FToneGenerator::FToneGenerator(int32 InSampleRate, int32 InNumChannels, int32 InFrequency, float InVolume)
	: NumChannels(InNumChannels)
{
	SineOsc.Init(InSampleRate, InFrequency, InVolume);
}

FToneGenerator::~FToneGenerator()
{
}

void FToneGenerator::SetFrequency(float InFrequency)
{
	SynthCommand([this, InFrequency]()
	{
		SineOsc.SetFrequency(InFrequency);
	});
}

void FToneGenerator::SetVolume(float InVolume)
{
	SynthCommand([this, InVolume]()
	{
		SineOsc.SetScale(InVolume);
	});
}

int32 FToneGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	check(NumChannels != 0);

	int32 NumFrames = NumSamples / NumChannels;
	int32 SampleIndex = 0;

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		float Sample = SineOsc.ProcessAudio() * 0.5f;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			OutAudio[SampleIndex++] = Sample;
		}
	}
	return NumSamples;
}

USynthComponentToneGenerator::USynthComponentToneGenerator(const FObjectInitializer& ObjInitializer)
	: Super(ObjInitializer)
{
	Frequency = 440.0f;
	Volume = 0.5f;
	NumChannels = 1;
}

USynthComponentToneGenerator::~USynthComponentToneGenerator()
{
}

void USynthComponentToneGenerator::SetFrequency(float InFrequency)
{
	Frequency = InFrequency;
	if (ToneGenerator.IsValid())
	{
		FToneGenerator* ToneGen = static_cast<FToneGenerator*>(ToneGenerator.Get());
		ToneGen->SetFrequency(InFrequency);
	}
}

void USynthComponentToneGenerator::SetVolume(float InVolume)
{
	Volume = InVolume;
	if (ToneGenerator.IsValid())
	{
		FToneGenerator* ToneGen = static_cast<FToneGenerator*>(ToneGenerator.Get());
		ToneGen->SetVolume(InVolume);
	}
}

ISoundGeneratorPtr USynthComponentToneGenerator::CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels)
{
	return ToneGenerator = ISoundGeneratorPtr(new FToneGenerator(InSampleRate, InNumChannels, Frequency, Volume));
}