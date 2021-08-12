// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISMPartition/ISMPartitionActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "InstancedPlacementPartitionActor.generated.h"

/**
 * The base class used by any editor placement of instanced objects, which holds any relevant runtime data for the placed instances.
 */
UCLASS()
class ENGINE_API AInstancedPlacementPartitionActor : public AISMPartitionActor
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR	
	virtual uint32 GetDefaultGridSize(UWorld* InWorld) const override;
	virtual FGuid GetGridGuid() const override;

	void SetGridGuid(const FGuid& InGuid);
#endif

	virtual ISMInstanceManager* GetSMInstanceManager(const FSMInstanceId& InstanceId) override;

protected:
#if WITH_EDITORONLY_DATA
	FGuid PlacementGridGuid;
#endif
};
