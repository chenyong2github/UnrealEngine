// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"

#include "ActorPartitionSubsystem.generated.h"

using FActorPartitionHash = int32;

/**
 * FActorPartitionGetParam
 */
struct FActorPartitionGetParams
{
	FActorPartitionGetParams(const TSubclassOf<AActor>& InActorClass, bool bInCreate, ULevel* InLevelHint, const FVector& InLocationHint)
		: ActorClass(InActorClass)
		, bCreate(bInCreate)
		, LocationHint(InLocationHint)
		, LevelHint(InLevelHint)
	{}

	/* Class of Actor we are getting from the subsystem. */
	TSubclassOf<AActor> ActorClass;
	/* Tells Subsystem if it needs to create Actor if it doesn't exist. */
	bool bCreate;
	/* Depending on the world LocationHint can be used to find/create the Actor. */
	FVector LocationHint;
	/* Depending on the world LevelHint can be used to find/create the Actor. */
	ULevel* LevelHint;
};

/**
 * FBaseActorPartition
 */
class FBaseActorPartition
{
public:
	FBaseActorPartition(UWorld* InWorld) : World(InWorld) {}
	virtual ~FBaseActorPartition() {}

	virtual bool GetActorPartitionHash(const FActorPartitionGetParams& GetParams, FActorPartitionHash& OutPartitionHash) const = 0;
	virtual AActor* GetActor(const FActorPartitionGetParams& GetParams) = 0;

	DECLARE_EVENT_OneParam(FBaseActorPartition, FOnActorPartitionHashInvalidated, const FActorPartitionHash&);
	FOnActorPartitionHashInvalidated& GetOnActorPartitionHashInvalidated() { return OnActorPartitionHashInvalidated; }
protected:
	UWorld* World;

	FOnActorPartitionHashInvalidated OnActorPartitionHashInvalidated;
};

/**
 * UActorPartitionSubsystem
 */

UCLASS()
class ENGINE_API UActorPartitionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UActorPartitionSubsystem();
		
#if WITH_EDITOR
	AActor* GetActor(const FActorPartitionGetParams& GetParam);
		
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
#endif
	bool IsLevelPartition() const;

private:

#if WITH_EDITOR
	void OnActorPartitionHashInvalidated(const FActorPartitionHash& Hash);

	using TPartitionActors = TMap<UClass*, TWeakObjectPtr<AActor>>;
	
	FBaseActorPartition& GetActorPartition();
	void InitializeActorPartition();

	TMap<FActorPartitionHash, TPartitionActors> PartitionedActors;
	TUniquePtr<FBaseActorPartition> ActorPartitionPtr;

	FDelegateHandle ActorPartitionHashInvalidatedHandle;
#endif
};
