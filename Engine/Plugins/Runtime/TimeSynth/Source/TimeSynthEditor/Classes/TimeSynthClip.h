// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetToolsModule.h"
#include "AssetTypeActions_Base.h"
#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Stats/Stats.h"
#include "TimeSynthClip.generated.h"

class USoundWave;

class FAssetTypeActions_TimeSynthClip : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_TimeSynthClip", "Time Synth Clip"); }
	virtual FColor GetTypeColor() const override { return FColor(155, 175, 100); }
	virtual const TArray<FText>& GetSubMenus() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
};

UCLASS(MinimalAPI, hidecategories = Object)
class UTimeSynthClipFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface	

	TArray<TWeakObjectPtr<USoundWave>> SoundWaves;
};
