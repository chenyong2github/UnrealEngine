// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/UserDefinedStruct.h"
#include "AssetDefinitionDefault.h"

#include "AssetDefinition_UserDefinedStruct.generated.h"

UCLASS()
class ENGINEASSETDEFINITIONS_API UAssetDefinition_UserDefinedStruct : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition Begin
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_Struct", "Structure"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(103, 206, 218)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UUserDefinedStruct::StaticClass(); }
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = { EAssetCategoryPaths::Blueprint };
		return Categories;
	}
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	// UAssetDefinition End
};
