// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "AssetDefinitionDefault.h"

#include "AssetDefinition_World.generated.h"

UCLASS()
class ENGINEASSETDEFINITIONS_API UAssetDefinition_World : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition Begin
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_World", "Level"); }
	virtual FLinearColor GetAssetColor() const override { return FAppStyle::Get().GetColor("LevelEditor.AssetColor"); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UWorld::StaticClass(); }
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;

	virtual TArray<FAssetData> PrepareToActivateAssets(const FAssetActivateArgs& ActivateArgs) const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	
	virtual FAssetSupportResponse CanRename(const FAssetData& InAsset) const override;
	virtual FAssetSupportResponse CanDuplicate(const FAssetData& InAsset) const override;

	virtual UThumbnailInfo* LoadThumbnailInfo(const FAssetData& InAsset) const override;
	// UAssetDefinition End
	
public:
	bool IsPartitionWorldInUse(const FAssetData& InAsset) const;
};
