// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CookPackageSplitter.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FWorldPartitionCookPackageSplitter : public ICookPackageSplitter
{
public:
	//~ Begin of ICookPackageSplitter
	static bool ShouldSplit(UObject* SplitData);

	FWorldPartitionCookPackageSplitter();
	virtual ~FWorldPartitionCookPackageSplitter();

	virtual TArray<ICookPackageSplitter::FGeneratedPackage> GetGenerateList(const UPackage* OwnerPackage, const UObject* OwnerObject) override;
	virtual bool TryPopulatePackage(const UPackage* OwnerPackage, const UObject* OwnerObject,
		const ICookPackageSplitter::FGeneratedPackageForPopulate& GeneratedPackage, bool bWasOwnerReloaded) override;
	virtual void PreSaveGeneratorPackage(UPackage* OwnerPackage, UObject* OwnerObject,
		const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& GeneratedPackages) override;
	//~ End of ICookPackageSplitter

private:
	const UWorld* ValidateDataObject(const UObject* SplitData);
	UWorld* ValidateDataObject(UObject* SplitData);
	void PreGarbageCollect();
	void TeardownWorldPartition();

	TWeakObjectPtr<UWorld> ReferencedWorld;
	bool bWorldPartitionNeedsTeardown = false;
};

#endif