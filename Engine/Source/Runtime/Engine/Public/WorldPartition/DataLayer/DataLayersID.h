// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/DataLayer/DataLayer.h"

#if WITH_EDITOR
class FDataLayersID
{
public:
	FDataLayersID();
	FDataLayersID(const TArray<const UDataLayer*>& InDataLayers);

	bool operator==(const FDataLayersID& Other) const
	{
		return Hash == Other.Hash;
	}

	bool operator!=(const FDataLayersID& Other) const
	{
		return !(*this == Other);
	}

	uint32 GetHash() const { return Hash; }

private:
	uint32 Hash;
};
#endif
