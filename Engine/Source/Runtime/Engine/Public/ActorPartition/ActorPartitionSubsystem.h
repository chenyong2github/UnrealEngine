// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Misc/HashBuilder.h"
#include "WorldPartition/ActorPartition/InstancedObjectsActorDescFactory.h"
#include "ActorPartitionSubsystem.generated.h"

class FBaseActorPartition;

/**
 * FActorPartitionGetParam
 */
struct FActorPartitionGetParams
{
	FActorPartitionGetParams(const TSubclassOf<AActor>& InActorClass, bool bInCreate, ULevel* InLevelHint, const FVector& InLocationHint, int32 InGridSize)
		: ActorClass(InActorClass)
		, bCreate(bInCreate)
		, LocationHint(InLocationHint)
		, LevelHint(InLevelHint)
		, GridSize(InGridSize)
	{}

	/* Class of Actor we are getting from the subsystem. */
	TSubclassOf<AActor> ActorClass;
	
	/* Tells Subsystem if it needs to create Actor if it doesn't exist. */
	bool bCreate;
	
	/* Depending on the world LocationHint can be used to find/create the Actor. */
	FVector LocationHint;
	
	/* Depending on the world LevelHint can be used to find/create the Actor. */
	ULevel* LevelHint;
	
	/* Desired size of the grid to create actors on if partitioned. */
	int32 GridSize;
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

	struct FCellCoord
	{
		FCellCoord()
		{}

		FCellCoord(int64 InX, int64 InY, int64 InZ, const ULevel* InLevel)
			: X(InX)
			, Y(InY)
			, Z(InZ)
			, Level(InLevel)
		{}

		int64 X;
		int64 Y;
		int64 Z;
		const ULevel* Level;

		bool operator==(const FCellCoord& Other) const
		{
			return (X == Other.X) && (Y == Other.Y) && (Z == Other.Z) && (Level == Other.Level);
		}

		friend ENGINE_API uint32 GetTypeHash(const FCellCoord& CellCoord)
		{
			FHashBuilder HashBuilder;
			HashBuilder << CellCoord.X << CellCoord.Y << CellCoord.Z << PointerHash(CellCoord.Level);
			return HashBuilder.GetHash();
		}
	};
		
#if WITH_EDITOR
	AActor* GetActor(const FActorPartitionGetParams& GetParam);
		
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
#endif
	bool IsLevelPartition() const;

private:

#if WITH_EDITOR
	void OnActorPartitionHashInvalidated(const FCellCoord& Hash);

	void InitializeActorPartition();

	TMap<FCellCoord, TMap<UClass*, TWeakObjectPtr<AActor>>> PartitionedActors;
	TUniquePtr<FBaseActorPartition> ActorPartition;
	
	FInstancedObjectsActorDescFactory InstancedObjectsActorDescFactory;

	FDelegateHandle ActorPartitionHashInvalidatedHandle;
#endif
};

/**
 * FBaseActorPartition
 */
class FBaseActorPartition
{
public:
	FBaseActorPartition(UWorld* InWorld) : World(InWorld) {}
	virtual ~FBaseActorPartition() {}

	virtual UActorPartitionSubsystem::FCellCoord GetActorPartitionHash(const FActorPartitionGetParams& GetParams) const = 0;
	virtual AActor* GetActor(const FActorPartitionGetParams& GetParams, const UActorPartitionSubsystem::FCellCoord& CellCoord) = 0;

	DECLARE_EVENT_OneParam(FBaseActorPartition, FOnActorPartitionHashInvalidated, const UActorPartitionSubsystem::FCellCoord&);
	FOnActorPartitionHashInvalidated& GetOnActorPartitionHashInvalidated() { return OnActorPartitionHashInvalidated; }
protected:
	UWorld* World;

	FOnActorPartitionHashInvalidated OnActorPartitionHashInvalidated;
};