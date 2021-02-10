// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Misc/Guid.h"
#include "PartitionActor.generated.h"

class UBoxComponent;

// Actor base class for instance containers placed on a grid.
// See UActorPartitionSubsystem.
UCLASS(Abstract)
class ENGINE_API APartitionActor: public AActor
{
	GENERATED_BODY()

public:
	APartitionActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin AActor Interface
#if WITH_EDITOR	
	virtual EActorGridPlacement GetDefaultGridPlacement() const override { return EActorGridPlacement::Location; }
	virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;
	virtual uint32 GetDefaultGridSize(UWorld* InWorld) const PURE_VIRTUAL(APartitionActor, return 0;)
	virtual FGuid GetGridGuid() const { return FGuid(); }
	virtual bool IsUserManaged() const override;
#endif
	//~ End AActor Interface	

#if WITH_EDITORONLY_DATA
	/** The grid size this actors was generated for */
	UPROPERTY()
	uint32 GridSize;
#endif
};

DEFINE_ACTORDESC_TYPE(APartitionActor, FPartitionActorDesc);
