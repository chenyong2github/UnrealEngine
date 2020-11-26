// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DataLayerSubsystem.generated.h"

/**
 * UDataLayerSubsystem
 */

UCLASS()
class ENGINE_API UDataLayerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDataLayerSubsystem();

	//~ Begin USubsystem Interface.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem Interface.

	//~ Begin Blueprint callable functions
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void ActivateDataLayer(const FActorDataLayer& InDataLayer, bool bInActivate);
	
	UFUNCTION(BlueprintCallable, Category = DataLayers)
	void ActivateDataLayerByLabel(const FName& InDataLayerLabel, bool bInActivate);

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	bool IsDataLayerActive(const FActorDataLayer& InDataLayer) const;

	UFUNCTION(BlueprintCallable, Category = DataLayers)
	bool IsDataLayerActiveByLabel(const FName& InDataLayerName) const;
	//~ End Blueprint callable functions

	UDataLayer* GetDataLayerFromLabel(const FName& InDataLayerLabel) const;
	UDataLayer* GetDataLayerFromName(const FName& InDataLayerName) const;
	void ActivateDataLayer(const UDataLayer* InDataLayer, bool bInActivate);
	void ActivateDataLayerByName(const FName& InDataLayerName, bool bInActivate);
	bool IsDataLayerActive(const UDataLayer* InDataLayer) const;
	bool IsDataLayerActiveByName(const FName& InDataLayerName) const;
	bool IsAnyDataLayerActive(const TArray<FName>& InDataLayerNames) const;

private:
	/** Console command used to toggle activation of a DataLayer */
	static class FAutoConsoleCommand ToggleDataLayerActivation;

	TSet<FName> ActiveDataLayerNames;
};