// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CookPackageSplitter.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FWorldPartitionCookPackageSplitter : public FGCObject, public ICookPackageSplitter
{
public:
	//~ Begin of ICookPackageSplitter
	static bool ShouldSplit(UObject* SplitData);

	FWorldPartitionCookPackageSplitter();
	virtual ~FWorldPartitionCookPackageSplitter();

	virtual void Teardown(ETeardown Status) override;
	virtual bool UseInternalReferenceToAvoidGarbageCollect() override { return true; }
	virtual TArray<ICookPackageSplitter::FGeneratedPackage> GetGenerateList(const UPackage* OwnerPackage, const UObject* OwnerObject) override;
	virtual bool TryPopulatePackage(const UPackage* OwnerPackage, const UObject* OwnerObject,
		const ICookPackageSplitter::FGeneratedPackageForPopulate& GeneratedPackage, bool bWasOwnerReloaded) override;
	virtual void PreSaveGeneratorPackage(UPackage* OwnerPackage, UObject* OwnerObject,
		const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& GeneratedPackages) override;
	//~ End of ICookPackageSplitter

private:
	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

	const UWorld* ValidateDataObject(const UObject* SplitData);
	UWorld* ValidateDataObject(UObject* SplitData);
	
	TObjectPtr<UWorld> ReferencedWorld;

	bool bInitializedWorldPartition = false;
	bool bForceInitializedWorld = false;
	bool bInitializedPhysicsSceneForSave = false;
};

#endif