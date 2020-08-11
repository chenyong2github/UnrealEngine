// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class UHLODSubsystem;
class UWorldPartition;

/**
 * ActorDesc for AWorldPartitionHLOD.
 */
class ENGINE_API FHLODActorDesc : public FWorldPartitionActorDesc
{
public:
#if WITH_EDITOR
	inline const TArray<FGuid>& GetSubActors() const { return SubActors; }

protected:
	FHLODActorDesc() = delete;
	FHLODActorDesc(const FWorldPartitionActorDescData& DescData, const TArray<FGuid>& InSubActors);
	FHLODActorDesc(AActor* InActor);

	virtual void BuildHash(FHashBuilder& HashBuilder) override;

	friend class FHLODActorDescFactory;

	TArray<FGuid> SubActors;
#endif
};
