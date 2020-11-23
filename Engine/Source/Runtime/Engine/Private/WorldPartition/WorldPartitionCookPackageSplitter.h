// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CookPackageSplitter.h"
#include "Engine/World.h"

class FWorldPartitionCookPackageSplitter : public ICookPackageSplitter
{
public:
	//~ Begin of ICookPackageSplitter
	static bool ShouldSplit(UObject* SplitData);
	virtual ~FWorldPartitionCookPackageSplitter() {}
	virtual void SetDataObject(UObject* SplitData) override;
	virtual TArray<ICookPackageSplitter::FGeneratedPackage> GetGenerateList() override;
	virtual bool TryPopulatePackage(UPackage* GeneratedPackage, const FStringView& RelativePath, const FStringView& GeneratedPackageCookName) override;
	virtual void FinalizeGeneratorPackage() override;
	//~ End of ICookPackageSplitter

private:
	UWorld* PartitionedWorld;
};

#endif