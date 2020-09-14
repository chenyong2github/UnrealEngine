// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class UHLODLayer;

/**
 * ActorDesc for AWorldPartitionHLOD.
 */
class ENGINE_API FHLODActorDesc : public FWorldPartitionActorDesc
{
public:
#if WITH_EDITOR
	inline const TArray<FGuid>& GetSubActors() const { return SubActors; }
	inline const FSoftObjectPath& GetHLODLayer() const { return HLODLayer; }

protected:
	FHLODActorDesc() = delete;
	FHLODActorDesc(const FWorldPartitionActorDescData& DescData, const TArray<FGuid>& InSubActors, const FSoftObjectPath& HLODLayer);
	FHLODActorDesc(AActor* InActor);

	virtual void BuildHash(FHashBuilder& HashBuilder) override;

	friend class FHLODActorDescFactory;

	TArray<FGuid> SubActors;
	FSoftObjectPath	HLODLayer;
#endif
};
