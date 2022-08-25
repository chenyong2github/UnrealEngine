// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "WaveformEditorCustomDetailsHelpers.generated.h"

// Simple helper classes for custom interfaces details tab 

UCLASS(MinimalAPI)
class UWaveformTransformationsViewHelper : public UObject
{
	GENERATED_BODY()
public:
	UWaveformTransformationsViewHelper() = default;

	void SetSoundWave(UObject* InSoundWave);
	const TWeakObjectPtr<UObject> GetSoundWave();
private:
	TWeakObjectPtr<UObject> SoundWave;
};