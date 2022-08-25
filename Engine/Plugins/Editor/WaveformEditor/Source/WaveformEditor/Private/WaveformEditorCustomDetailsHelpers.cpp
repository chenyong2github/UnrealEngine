// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorCustomDetailsHelpers.h"

void UWaveformTransformationsViewHelper::SetSoundWave(UObject* InSoundWave)
{
	SoundWave = InSoundWave;
}

const TWeakObjectPtr<UObject> UWaveformTransformationsViewHelper::GetSoundWave()
{
	return SoundWave;
}
