// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"
#include "Engine/Blueprint.h"

#include "AssetDefinition_Blueprint.generated.h"

struct FToolMenuContext;
struct FAssetData;
class UFactory;

UCLASS()
class UAssetDefinition_Blueprint : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition Implementation
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_Blueprint", "Blueprint Class"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor( 63, 126, 255 )); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UBlueprint::StaticClass(); }
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override
	{
		static const auto Categories = { EAssetCategoryPaths::Basic };
		return Categories;
	}
	
public:
	virtual UFactory* GetFactoryForBlueprintType(UBlueprint* InBlueprint) const;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
