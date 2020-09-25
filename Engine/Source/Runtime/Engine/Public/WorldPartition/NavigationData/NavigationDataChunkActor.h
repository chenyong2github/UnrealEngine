// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Containers/Array.h"
#include "NavigationDataChunkActor.generated.h"

class UNavigationDataChunk;

UCLASS(NotPlaceable)
class ENGINE_API ANavigationDataChunkActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	//~ End UObject Interface

	const TArray<UNavigationDataChunk*>& GetNavDataChunk() const { return NavDataChunks; }
	TArray<UNavigationDataChunk*>& GetMutableNavDataChunk() { return NavDataChunks; }

	void CollectNavData(const FBox& Bounds);

protected:
	//~ Begin AActor Interface.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface.

#if WITH_EDITOR
	//~ Begin AActor Interface.
	virtual EActorGridPlacement GetDefaultGridPlacement() const override;
	//~ End AActor Interface.
#endif

	UPROPERTY()
	TArray<UNavigationDataChunk*> NavDataChunks;
};
