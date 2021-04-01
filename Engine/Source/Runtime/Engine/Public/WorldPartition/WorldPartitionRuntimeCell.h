// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartitionRuntimeCell.generated.h"

USTRUCT()
struct FWorldPartitionRuntimeCellObjectMapping
{
	GENERATED_USTRUCT_BODY()

	FWorldPartitionRuntimeCellObjectMapping()
#if WITH_EDITORONLY_DATA
		: Package(NAME_None)
		, Path(NAME_None)
		, ContainerID(0)
		, ContainerTransform(FTransform::Identity)
		, ContainerPackage(NAME_None)
		, LoadedPath(NAME_None)
#endif
	{}

	FWorldPartitionRuntimeCellObjectMapping(FName InPackage, FName InPath, uint32 InContainerID, const FTransform& InContainerTransform, FName InContainerPackage)
#if WITH_EDITORONLY_DATA
		: Package(InPackage)
		, Path(InPath)
		, ContainerID(InContainerID)
		, ContainerTransform(InContainerTransform)
		, ContainerPackage(InContainerPackage)
		, LoadedPath(InPath)
#endif
	{}

#if WITH_EDITORONLY_DATA
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

	/**
	 * ID of the owning container instance
	 */
	UPROPERTY()
	uint32 ContainerID;

	/** 
	 * Transform of the owning container instance
	 */
	UPROPERTY()
	FTransform ContainerTransform;
		
	/**
	 * Package of the owning container instance
	 */
	UPROPERTY()
	FName ContainerPackage;

	/**
	* Loaded actor path (when cooking or pie)
	* 
	* Depending on if the actor was part of a container instance or the main partition this will be the path
	* of the loaded or duplicated actor before it is moved into its runtime cell.
	* 
	* If the actor was part of the world partition this path should match the Path property.
	*/
	UPROPERTY()
	FName LoadedPath;
#endif
};

/**
 * Serve as a generic container of type specific data that can be assigned to each runtime cell
 */
UCLASS(Abstract)
class UWorldPartitionRuntimeCellData : public UObject
{
	GENERATED_UCLASS_BODY()
};

class UActorDescContainer;

/**
 * Cell State
 */
UENUM(BlueprintType)
enum class EWorldPartitionRuntimeCellState : uint8
{
	Unloaded,
	Loaded,
	Activated
};

static_assert(EWorldPartitionRuntimeCellState::Unloaded < EWorldPartitionRuntimeCellState::Loaded && EWorldPartitionRuntimeCellState::Loaded < EWorldPartitionRuntimeCellState::Activated, "Streaming Query code is dependent on this being true");

/**
 * Represents a PIE/Game streaming cell which points to external actor/data chunk packages
 */
UCLASS(Abstract)
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
	void AddCellData(const UWorldPartitionRuntimeCellData* InCellData);
	virtual void AddActorToCell(const FWorldPartitionActorDescView& ActorDescView, uint32 InContainerID, const FTransform& InContainerTransform, const UActorDescContainer* InContainer) PURE_VIRTUAL(UWorldPartitionRuntimeCell::AddActorToCell,);
	virtual int32 GetActorCount() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetActorCount, return 0;);

	// Cook methods
	virtual bool PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageCookName) PURE_VIRTUAL(UWorldPartitionRuntimeCell::PopulateGeneratedPackageForCook, return false;);
	virtual void MoveAlwaysLoadedContentToPersistentLevel() PURE_VIRTUAL(UWorldPartitionRuntimeCell::MoveAlwaysLoadedContentToPersistentLevel);
	virtual void FinalizeGeneratedPackageForCook() {}
	virtual FString GetPackageNameToCreate() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetPackageNameToCreate, return FString(""););
#endif

	const UWorldPartitionRuntimeCellData* GetCellData(const TSubclassOf<UWorldPartitionRuntimeCellData> InCellDataClass) const;
	template <class T> inline const T* GetCellData() const { return Cast<const T>(GetCellData(T::StaticClass())); }
	template <class T> inline bool HasCellData() const { return GetCellData<T>() != nullptr; }

protected:
	UPROPERTY()
	bool bIsAlwaysLoaded;

private:
	UPROPERTY()
	TMap<const TSubclassOf<UWorldPartitionRuntimeCellData>, TObjectPtr<const UWorldPartitionRuntimeCellData>> CellDataMap;

	UPROPERTY()
	TArray<FName> DataLayers;
};
