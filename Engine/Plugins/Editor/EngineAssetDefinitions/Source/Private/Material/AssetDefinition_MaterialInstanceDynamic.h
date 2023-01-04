// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AssetDefinition_MaterialInterface.h"

#include "AssetDefinition_MaterialInstanceDynamic.generated.h"

UCLASS()
class UAssetDefinition_MaterialInstanceDynamic : public UAssetDefinition_MaterialInterface
{
	GENERATED_BODY()

public:
	// UAssetDefinition Begin
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_MaterialInstanceDynamic", "Material Instance Dynamic"); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UMaterialInstanceDynamic::StaticClass(); }
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	// UAssetDefinition End
};
