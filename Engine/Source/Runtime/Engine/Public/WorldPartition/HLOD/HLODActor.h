// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameFramework/Actor.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
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

protected:
	//~ Begin AActor Interface.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface.

#if WITH_EDITOR
	void SetLODParent(AActor& InActor);
	void ClearLODParent(AActor& InActor);
	void UpdateLODParent(AActor& InActor, bool bInClear);
#endif

	UPrimitiveComponent* GetHLODComponent();

public:
#if WITH_EDITOR
	void SetHLODPrimitives(const TArray<UPrimitiveComponent*>& InHLODPrimitives, float InFadeOutDistance);
	void SetChildrenPrimitives(const TArray<UPrimitiveComponent*>& InChildrenPrimitives);

	const TArray<FGuid>& GetSubActors() const;

	void SetHLODLayer(const UHLODLayer* InSubActorsHLODLayer, int32 InSubActorsHLODLevel);
	
	const FBox& GetHLODBounds() const;
	void SetHLODBounds(const FBox& InBounds);

protected:
	//~ Begin AActor Interface.
	virtual EActorGridPlacement GetDefaultGridPlacement() const override;
	virtual void PostActorCreated() override;
	virtual void GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const override;
	virtual void GetActorLocationBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const override;
	//~ End AActor Interface.
#endif // WITH_EDITOR

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FGuid> SubActors;

	UPROPERTY()
	const UHLODLayer* SubActorsHLODLayer;

	UPROPERTY()
	FBox HLODBounds;

	FDelegateHandle ActorRegisteredDelegateHandle;
#endif
	
	UPROPERTY()
	int32 SubActorsHLODLevel;

	UPROPERTY()
	FGuid HLODGuid;
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