// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameFramework/Actor.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "HLODActor.generated.h"

class UHLODLayer;

UCLASS(NotPlaceable)
class ENGINE_API AWorldPartitionHLOD : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	UPrimitiveComponent* GetHLODComponent();

	void LinkCell(FName InCellName);
	void UnlinkCell(FName InCellName);

	const FGuid& GetHLODGuid() const { return HLODGuid; }

protected:
	//~ Begin AActor Interface.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface.

	void SetLODParent(AActor& InActor);
	void ClearLODParent(AActor& InActor);
	void UpdateLODParent(AActor& InActor, bool bInClear);

public:
#if WITH_EDITOR
	void SetHLODPrimitive(UPrimitiveComponent* InHLODPrimitive);
	void SetChildrenPrimitives(const TArray<UPrimitiveComponent*>& InChildrenPrimitives);

	const TArray<FGuid>& GetSubActors() const;

	void SetHLODLayer(const UHLODLayer* InSubActorsHLODLayer, int32 InSubActorsHLODLevel);

protected:
	void OnWorldPartitionActorRegistered(AActor& InActor, bool bInLoaded);

	//~ Begin AActor Interface.
	virtual EActorGridPlacement GetDefaultGridPlacement() const override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual void PostActorCreated() override;
	virtual void RegisterAllComponents() override;
	virtual void UnregisterAllComponents(const bool bForReregister) override;
	//~ End AActor Interface.
#endif // WITH_EDITOR

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FGuid> SubActors;

	UPROPERTY()
	const UHLODLayer* SubActorsHLODLayer;

	FDelegateHandle ActorRegisteredDelegateHandle;
#endif
	
	UPROPERTY()
	int32 SubActorsHLODLevel;

	UPROPERTY()
	FGuid HLODGuid;

	UPROPERTY()
	UPrimitiveComponent* HLODComponent;

	UPROPERTY()
	TArray<TSoftObjectPtr<UPrimitiveComponent>> SubPrimitivesComponents;
};

UCLASS()
class UWorldPartitionRuntimeHLODCellData : public UWorldPartitionRuntimeCellData
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	void SetReferencedHLODActors(TArray<FGuid>&& InReferencedHLODActors);
#endif

	UPROPERTY()
	TArray<FGuid> ReferencedHLODActors;
};