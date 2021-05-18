// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CookPackageSplitter.h"
#include "Engine/World.h"
#include "UObject/GCObject.h"

class FWorldPartitionCookPackageSplitter : public ICookPackageSplitter, public FGCObject
{
public:
	//~ Begin of ICookPackageSplitter
	static bool ShouldSplit(UObject* SplitData);
	virtual bool UseDeferredPopulate() { return false; }
	virtual ~FWorldPartitionCookPackageSplitter() {}
	virtual TArray<ICookPackageSplitter::FGeneratedPackage> GetGenerateList(const UPackage* OwnerPackage, const UObject* OwnerObject) override;
	virtual bool TryPopulatePackage(const UPackage* OwnerPackage, const UObject* OwnerObject,
		const ICookPackageSplitter::FGeneratedPackageForPopulate& GeneratedPackage) override;
	virtual void PreSaveGeneratorPackage(UPackage* OwnerPackage, UObject* OwnerObject,
		const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& GeneratedPackages) override;
	//~ End of ICookPackageSplitter

	//~ Begin of FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector);
	virtual FString GetReferencerName() const
	{
		return TEXT("FWorldPartitionCookPackageSplitter");
	}
	//~ End of FGCObject

private:
	const UWorld* ValidateDataObject(const UObject* SplitData);
	UWorld* ValidateDataObject(UObject* SplitData);

	UWorld* ReferencedWorld = nullptr;
};

#endif