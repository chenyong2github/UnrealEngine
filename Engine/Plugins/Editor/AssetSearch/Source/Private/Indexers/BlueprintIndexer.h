// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetSearchModule.h"

class FBlueprintIndexer : public IAssetIndexer
{
	virtual FString GetName() const override { return TEXT("Blueprint"); }
	virtual int32 GetVersion() const override;
	virtual void IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer) override;
};