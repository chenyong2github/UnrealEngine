// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimSequence.h"
#include "AssetDefinition_AnimationAsset.h"

#include "AssetDefinition_AnimSequence.generated.h"

UCLASS()
class UAssetDefinition_AnimSequence : public UAssetDefinition_AnimationAsset
{
	GENERATED_BODY()

public:
	// UAssetDefinition Begin
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AnimSequence", "Animation Sequence"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(237, 28, 36)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UAnimSequence::StaticClass(); }
	virtual bool CanImport() const override { return true; }
	// UAssetDefinition End
};
