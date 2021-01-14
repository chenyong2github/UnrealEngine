// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GameFeatureAction_DataRegistrySource.generated.h"

class UDataTable;

USTRUCT()
struct FDataRegistrySourceToAdd
{
	GENERATED_BODY()

	FDataRegistrySourceToAdd()
		: AssetPriority(0)
		, bClientSource(false)
		, bServerSource(false)
	{}

	// Name of the registry to add to
	UPROPERTY(EditAnywhere, Category="Registry Data")
	FName RegistryToAddTo;

	// Priority to use when adding to the registry.  Higher priorities are searched first
	UPROPERTY(EditAnywhere, Category = "Registry Data")
	int32 AssetPriority;

	// Should this component be added for clients
	UPROPERTY(EditAnywhere, Category = "Registry Data")
	uint8 bClientSource : 1;

	// Should this component be added on servers
	UPROPERTY(EditAnywhere, Category = "Registry Data")
	uint8 bServerSource : 1;

	// Link to the data table to add to the registry
	UPROPERTY(EditAnywhere, Category = "Registry Data")
	TSoftObjectPtr<UDataTable> DataTableToAdd;

	// TODO: Should this also support curve tables?
	UPROPERTY(EditAnywhere, Category = "Registry Data")
	TSoftObjectPtr<UCurveTable> CurveTableToAdd;
};

UCLASS(MinimalAPI, meta = (DisplayName = "Add Data Registry Source"))
class UGameFeatureAction_DataRegistrySource : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	virtual void OnGameFeatureActivating() override;

	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) override;
#endif

	//~UObject interface
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(TArray<FText>& ValidationErrors) override;
#endif
	//~End of UObject interface

private:
	UPROPERTY(EditAnywhere, Category = "Registry Data")
	TArray<FDataRegistrySourceToAdd> SourcesToAdd;
};