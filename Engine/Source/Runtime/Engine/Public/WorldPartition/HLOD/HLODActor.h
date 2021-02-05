// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameFramework/Actor.h"
#include "Containers/Array.h"
#include "Containers/Set.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "HLODActor.generated.h"

class UHLODLayer;
class UHLODSubsystem;

UCLASS(NotPlaceable)
class ENGINE_API AWorldPartitionHLOD : public AActor
{
	GENERATED_UCLASS_BODY()

	friend class UHLODSubsystem;

public:
	void OnCellShown(FName InCellName);
	void OnCellHidden(FName InCellName);

	const FGuid& GetHLODGuid() const { return HLODGuid; }
	inline uint32 GetLODLevel() const { return LODLevel; }

#if WITH_EDITOR
	void OnSubActorLoaded(const AActor& Actor);
	void OnSubActorUnloaded(const AActor& Actor);

	void SetHLODPrimitives(const TArray<UPrimitiveComponent*>& InHLODPrimitives);

	void SetSubActors(const TArray<const AActor*>& InSubActors);
	const TArray<FGuid>& GetSubActors() const;

	void SetSubActorsHLODLayer(const UHLODLayer* InSubActorsHLODLayer) { SubActorsHLODLayer = InSubActorsHLODLayer; }
	const UHLODLayer* GetSubActorsHLODLayer() const { return SubActorsHLODLayer; }

	void SetGridIndices(uint64 InGridIndexX, uint64 InGridIndexY, uint64 InGridIndexZ)
	{
		GridIndexX = InGridIndexX;
		GridIndexY = InGridIndexY;
		GridIndexZ = InGridIndexZ;
	}

	void GetGridIndices(uint64& OutGridIndexX, uint64& OutGridIndexY, uint64& OutGridIndexZ) const
	{
		OutGridIndexX = GridIndexX;
		OutGridIndexY = GridIndexY;
		OutGridIndexZ = GridIndexZ;
	}

	inline void SetLODLevel(uint32 InLODLevel) { LODLevel = InLODLevel; }
#endif // WITH_EDITOR

protected:
	//~ Begin AActor Interface.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual EActorGridPlacement GetDefaultGridPlacement() const override;
	virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;

	virtual void PostActorCreated() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif
	//~ End AActor Interface.

	UPrimitiveComponent* GetHLODComponent();

#if WITH_EDITOR
	void UpdateVisibility();
	bool HasLoadedSubActors() const;

	void ResetLoadedSubActors();
	void SetupLoadedSubActors();
#endif // WITH_EDITOR

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FGuid> SubActors;

	UPROPERTY()
	TObjectPtr<const UHLODLayer> SubActorsHLODLayer;

	UPROPERTY()
	int64 GridIndexX;

	UPROPERTY()
	int64 GridIndexY;

	UPROPERTY()
	int64 GridIndexZ;

	TSet<TWeakObjectPtr<const AActor>> LoadedSubActors;
#endif

	UPROPERTY(NonPIEDuplicateTransient, TextExportTransient, NonTransactional)
	FGuid HLODGuid;

	UPROPERTY()
	uint32 LODLevel;
};

DEFINE_ACTORDESC_TYPE(AWorldPartitionHLOD, FHLODActorDesc);

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