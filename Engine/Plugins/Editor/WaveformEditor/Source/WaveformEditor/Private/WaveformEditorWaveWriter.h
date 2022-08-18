// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sound/SampleBufferIO.h"

class USoundWave;

class FWaveformEditorWaveWriter
{
public:
	explicit FWaveformEditorWaveWriter(USoundWave* InSoundWave);

	bool CanCreateSoundWaveAsset() const;
	void ExportTransformedWaveform();

private: 
	Audio::TSampleBuffer<> GenerateSampleBuffer() const;

	USoundWave* SourceSoundWave = nullptr;
	TUniquePtr<Audio::FSoundWavePCMWriter> WaveWriter = nullptr;
};