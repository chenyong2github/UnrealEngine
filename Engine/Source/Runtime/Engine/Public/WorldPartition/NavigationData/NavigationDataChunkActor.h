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
#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#endif //WITH_EDITOR
	//~ End UObject Interface

	const TArray<UNavigationDataChunk*>& GetNavDataChunk() const { return NavDataChunks; }
	TArray<UNavigationDataChunk*>& GetMutableNavDataChunk() { return NavDataChunks; }

	void CollectNavData(const FBox& QueryBounds, FBox& OutTilesBounds);

#if WITH_EDITOR
	void SetDataChunkActorBounds(const FBox& InBounds);

	//~ Begin AActor Interface.
	virtual EActorGridPlacement GetDefaultGridPlacement() const override;
	virtual void GetActorLocationBounds(bool bOnlyCollidingComponents, FVector& OutOrigin, FVector& OutBoxExtent, bool bIncludeFromChildActors) const override;
	//~ End AActor Interface.
#endif // WITH_EDITOR
	
protected:
	//~ Begin AActor Interface.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetActorBounds(bool bOnlyCollidingComponents, FVector& OutOrigin, FVector& OutBoxExtent, bool bIncludeFromChildActors) const override;
	//~ End AActor Interface.

	void AddNavigationDataChunkToWorld();
	void RemoveNavigationDataChunkFromWorld();
	void Log(const TCHAR* FunctionName) const;

	UPROPERTY()
	TArray<TObjectPtr<UNavigationDataChunk>> NavDataChunks;

	UPROPERTY()
	FBox DataChunkActorBounds;
};
