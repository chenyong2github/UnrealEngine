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
	 * The cloud identifier of this cloud anchor.  Empty if the anchor has not been saved or loaded.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "AR AugmentedReality|Pin")
	FString CloudIdentifier;


	/**
	 * The ARPin with which this cloud anchor is associated, or null.  Null could mean we are still trying to load the anchor by ID.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "AR AugmentedReality|Pin")
	UARPin* ARPin;
};
