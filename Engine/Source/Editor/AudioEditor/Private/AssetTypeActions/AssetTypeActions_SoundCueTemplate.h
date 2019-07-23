// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions/AssetTypeActions_SoundBase.h"

class FMenuBuilder;
class USoundCueTemplate;

class FAssetTypeActions_SoundCueTemplate : public FAssetTypeActions_SoundBase
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundCueTemplate", "Sound Cue Template"); }
	virtual FColor GetTypeColor() const override { return FColor(255, 255, 255); }
	virtual UClass* GetSupportedClass() const override;
	virtual void GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder) override;
	virtual bool CanFilter() override { return true; }

protected:
	/** Converts the provided SoundCue Template to a fully-modifiable SoundCue */
	void ExecuteCopyToSoundCue(TArray<TWeakObjectPtr<USoundCueTemplate>> Objects);
};

