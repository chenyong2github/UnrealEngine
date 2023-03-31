// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "Misc/Optional.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_EDITOR
class AActor;
class IStreamingGenerationErrorHandler;
class UActorDescContainer;
class UWorldPartition;
struct FWorldPartitionActorFilter;

/**
 * A view on top of an actor desc, used to cache information that can be (potentially) different than the actor desc
 * itself due to streaming generation logic, etc.
 */
class ENGINE_API FWorldPartitionActorDescView
{
	friend class UWorldPartitionRuntimeHash;

public:
	FWorldPartitionActorDescView();
	FWorldPartitionActorDescView(const FWorldPartitionActorDesc* InActorDesc);

	const FGuid& GetGuid() const;
	FTopLevelAssetPath GetBaseClass() const;
	FTopLevelAssetPath GetNativeClass() const;
	UClass* GetActorNativeClass() const;
	UE_DEPRECATED(5.2, "GetOrigin is deprecated.")
	FVector GetOrigin() const;
	FName GetRuntimeGrid() const;
	bool GetIsSpatiallyLoaded() const;
	bool GetActorIsEditorOnly() const;
	bool GetActorIsRuntimeOnly() const;
	bool GetActorIsHLODRelevant() const;
	FName GetHLODLayer() const;
	const TArray<FName>& GetDataLayerInstanceNames() const;
	const TArray<FName>& GetRuntimeDataLayerInstanceNames() const;
	const TArray<FName>& GetTags() const;
	FName GetActorPackage() const;
	
	FSoftObjectPath GetActorSoftPath() const;
	FName GetActorLabel() const;

	UE_DEPRECATED(5.2, "GetBounds is deprecated, GetEditorBounds or GetRuntimeBounds should be used instead.")
	FBox GetBounds() const;

	FBox GetEditorBounds() const;
	FBox GetRuntimeBounds() const;

	const TArray<FGuid>& GetReferences() const;
	FString ToString() const;
	const FGuid& GetParentActor() const;
	FName GetActorName() const;
	const FGuid& GetFolderGuid() const;

	FGuid GetContentBundleGuid() const;
	FName GetContainerPackage() const;
	bool IsContainerInstance() const;
	bool GetContainerInstance(FWorldPartitionActorDesc::FContainerInstance& OutContainerInstance) const;
	bool IsContainerFilter() const;
	const FWorldPartitionActorFilter* GetContainerFilter() const;
	
	UE_DEPRECATED(5.3, "GetLevelPackage is deprecated use GetContainerPackage instead.")
	FName GetLevelPackage() const { return GetContainerPackage(); }

	void CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const;

	FName GetActorLabelOrName() const;

	UE_DEPRECATED(5.2, "ShouldValidateRuntimeGrid is deprecated and should not be used.")
	bool ShouldValidateRuntimeGrid() const;

	void SetForcedNonSpatiallyLoaded();
	void SetForcedNoRuntimeGrid();
	void SetInvalidDataLayers();
	void SetRuntimeDataLayerInstanceNames(TArray<FName>& InRuntimeDataLayerInstanceNames);
	void SetRuntimeReferences(TArray<FGuid>& InRuntimeReferences);
	void SetDataLayerInstanceNames(const TArray<FName>& InDataLayerInstanceNames);

	AActor* GetActor() const;

	bool operator==(const FWorldPartitionActorDescView& Other) const
	{
		return GetGuid() == Other.GetGuid();
	}

	friend uint32 GetTypeHash(const FWorldPartitionActorDescView& Key)
	{
		return GetTypeHash(Key.GetGuid());
	}

	const FWorldPartitionActorDesc* GetActorDesc() const { return ActorDesc; }

	bool GetProperty(FName PropertyName, FName* PropertyValue) const;
	bool HasProperty(FName PropertyName) const;

protected:
	const FWorldPartitionActorDesc* ActorDesc;
	bool bIsForcedNonSpatiallyLoaded;
	bool bIsForcedNoRuntimeGrid;
	bool bInvalidDataLayers;	
	TOptional<TArray<FName>> ResolvedDataLayerInstanceNames;
	TOptional<TArray<FName>> RuntimeDataLayerInstanceNames;
	TOptional<TArray<FGuid>> RuntimeReferences;
};
#endif