// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartitionRuntimeCell.generated.h"

USTRUCT()
struct FWorldPartitionRuntimeCellObjectMapping
{
	GENERATED_USTRUCT_BODY()

	FWorldPartitionRuntimeCellObjectMapping()
		: Package(NAME_None)
		, Path(NAME_None)
	{}

	FWorldPartitionRuntimeCellObjectMapping(FName InPackage, FName InPath)
		: Package(InPackage)
		, Path(InPath)
	{}

	/** 
	 * The name of the package to load to resolve on disk (can contain a single actor or a data chunk)
	 */
	UPROPERTY()
	FName Package;

	/** 
	 * The complete name path of the contained object
	 */
	UPROPERTY()
	FName Path;
};

/**
 * Serve as a generic container of type specific data that can be assigned to each runtime cell
 */
UCLASS(Abstract)
class UWorldPartitionRuntimeCellData : public UObject
{
	GENERATED_UCLASS_BODY()
};

/**
 * Represents a PIE/Game streaming cell which points to external actor/data chunk packages
 */
UCLASS()
class UWorldPartitionRuntimeCell : public UObject
{
	GENERATED_UCLASS_BODY()

	virtual FLinearColor GetDebugColor() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetDebugColor, return FLinearColor::Black;);
	virtual bool IsAlwaysLoaded() const { return bIsAlwaysLoaded; }
	virtual void SetIsAlwaysLoaded(bool bInIsAlwaysLoaded) { bIsAlwaysLoaded = bInIsAlwaysLoaded; }
	bool HasDataLayers() const { return !DataLayers.IsEmpty(); }
	const TArray<FName>& GetDataLayers() const { return DataLayers; }

#if WITH_EDITOR
	void SetDataLayers(const TArray<const UDataLayer*> InDataLayers);
	virtual void AddActorToCell(FName Package, FName Path) PURE_VIRTUAL(UWorldPartitionRuntimeCell::AddActorToCell,);
	virtual bool CreateCellForCook() PURE_VIRTUAL(UWorldPartitionRuntimeCell::CreateCellForCook, return false;);
	virtual int32 GetActorCount() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetActorCount, return 0;);

	void AddCellData(const UWorldPartitionRuntimeCellData* InCellData);
#endif

	const UWorldPartitionRuntimeCellData* GetCellData(const TSubclassOf<UWorldPartitionRuntimeCellData> InCellDataClass) const;
	template <class T> inline const T* GetCellData() const { return Cast<const T>(GetCellData(T::StaticClass())); }

protected:
	UPROPERTY()
	bool bIsAlwaysLoaded;

private:
	UPROPERTY()
	TMap<const TSubclassOf<UWorldPartitionRuntimeCellData>, const UWorldPartitionRuntimeCellData*> CellDataMap;

	UPROPERTY()
	TArray<FName> DataLayers;
};
