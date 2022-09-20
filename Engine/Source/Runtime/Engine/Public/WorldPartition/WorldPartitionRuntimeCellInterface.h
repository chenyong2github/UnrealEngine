// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UDataLayerInstance;
class UDataLayerAsset;

class IWorldPartitionCell
{
public:
	virtual TArray<const UDataLayerInstance*> GetDataLayerInstances() const = 0;
	virtual bool ContainsDataLayer(const UDataLayerAsset* DataLayerAsset) const = 0;
	virtual bool ContainsDataLayer(const UDataLayerInstance* DataLayerInstance) const = 0;
	virtual bool HasDataLayers() const = 0;
	virtual const TArray<FName>& GetDataLayers() const = 0;
	virtual bool HasAnyDataLayer(const TSet<FName>& InDataLayers) const = 0;
	virtual const FBox& GetContentBounds() const = 0;
};

