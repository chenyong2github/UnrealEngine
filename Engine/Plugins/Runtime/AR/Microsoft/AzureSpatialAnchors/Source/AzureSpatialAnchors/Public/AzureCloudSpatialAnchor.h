// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AzureSpatialAnchorsTypes.h"
#include "ARPin.h"

#include "AzureCloudSpatialAnchor.generated.h"


UCLASS(BlueprintType, Experimental, Category="AR AugmentedReality")
class AZURESPATIALANCHORS_API UAzureCloudSpatialAnchor : public UObject
{
	GENERATED_BODY()
	
public:
	/**
	 * Return the AppProperties of this cloud anchor.
	 */
	UFUNCTION(BlueprintPure, Category = "AR AugmentedReality|Pin")
	TMap<FString, FString> GetAppProperties() const;
	
	/**
	 * Return the expiration time of this cloud anchor.
	 */
	UFUNCTION(BlueprintPure, Category = "AR AugmentedReality|Pin")
	FDateTime GetExpiration() const;

	/**
	 * Return the cloud identifier of this cloud anchor.
	 */
	UFUNCTION(BlueprintPure, Category = "AR AugmentedReality|Pin")
	FString GetIdentifier() const;


	UPROPERTY(BlueprintReadOnly, Category = "AR AugmentedReality|Pin")
	UARPin* ARPin;
};
