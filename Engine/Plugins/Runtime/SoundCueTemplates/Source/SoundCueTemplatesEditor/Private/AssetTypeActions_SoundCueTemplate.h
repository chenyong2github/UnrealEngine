// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions/AssetTypeActions_SoundBase.h"

class USoundCueTemplate;

class FAssetTypeActions_SoundCueTemplate : public FAssetTypeActions_SoundBase
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundCueTemplate", "Sound Cue Template"); }
	virtual FColor GetTypeColor() const override { return FColor::Red; }
	virtual UClass* GetSupportedClass() const override;
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
	virtual bool CanFilter() override { return true; }

protected:
	/** Converts the provided SoundCue Template to a fully-modifiable SoundCue */
	void ExecuteCopyToSoundCue(TArray<TWeakObjectPtr<USoundCueTemplate>> Objects);
};

class FAssetActionExtender_SoundCueTemplate
{
public:
	static void RegisterMenus();
	static void ExecuteCreateSoundCueTemplate(const struct FToolMenuContext& MenuContext);
};