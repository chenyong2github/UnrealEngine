// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InstancedObjectsActor.generated.h"

class UBoxComponent;

// Actor base class for instance containers placed on a grid.
// See UActorPartitionSubsystem.
UCLASS(notplaceable)
class ENGINE_API AInstancedObjectsActor: public AActor
{
	GENERATED_BODY()

public:
	AInstancedObjectsActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin UObject Interface
#if WITH_EDITOR	
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif
	//~ End UObject Interface

	//~ Begin AActor Interface
#if WITH_EDITOR	
	virtual EActorGridPlacement GetDefaultGridPlacement() const override { return EActorGridPlacement::Location; }
#endif
	//~ End AActor Interface	

#if WITH_EDITORONLY_DATA
	/** The grid size this actors was generated for */
	UPROPERTY()
	int32 GridSize;
#endif
};