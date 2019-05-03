// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioEditorModule.h"


class FTimeSynthSoundWaveAssetActionExtender :	public ISoundWaveAssetActionExtensions,
												public TSharedFromThis<FTimeSynthSoundWaveAssetActionExtender>
{
public:
	virtual ~FTimeSynthSoundWaveAssetActionExtender() {}

	virtual void GetExtendedActions(const TArray<TWeakObjectPtr<USoundWave>>& InObjects, FMenuBuilder& MenuBuilder) override;

	void ExecuteCreateTimeSyncClip(TArray<TWeakObjectPtr<USoundWave>> Objects);
	void ExecuteCreateTimeSyncClipSet(TArray<TWeakObjectPtr<USoundWave>> SoundWaves);
};

