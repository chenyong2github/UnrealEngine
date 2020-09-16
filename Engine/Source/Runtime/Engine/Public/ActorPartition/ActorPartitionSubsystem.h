// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Misc/HashBuilder.h"
#include "WorldPartition/ActorPartition/PartitionActorDescFactory.h"
#include "ActorPartitionSubsystem.generated.h"

class FBaseActorPartition;
class APartitionActor;

#if WITH_EDITOR
/**
 * FActorPartitionGetParam
 */
struct ENGINE_API FActorPartitionGetParams
{
	FActorPartitionGetParams(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, ULevel* InLevelHint, const FVector& InLocationHint);

	/* Class of Actor we are getting from the subsystem. */
	TSubclassOf<APartitionActor> ActorClass;
	
	/* Tells Subsystem if it needs to create Actor if it doesn't exist. */
	bool bCreate;
	
	/* Depending on the world LocationHint can be used to find/create the Actor. */
	FVector LocationHint;
	
	/* Depending on the world LevelHint can be used to find/create the Actor. */
	ULevel* LevelHint;
};

#endif

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

		FCellCoord(int64 InX, int64 InY, int64 InZ, ULevel* InLevel)
			: X(InX)
			, Y(InY)
			, Z(InZ)
			, Level(InLevel)
		{}

		int64 X;
		int64 Y;
		int64 Z;
		ULevel* Level;

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

		static FCellCoord GetCellCoord(FVector InPos, ULevel* InLevel, uint32 InGridSize)
		{
			return FCellCoord(
				FMath::FloorToInt(InPos.X / InGridSize),
				FMath::FloorToInt(InPos.Y / InGridSize),
				FMath::FloorToInt(InPos.Z / InGridSize),
				InLevel
			);
		}

		static FBox GetCellBounds(const FCellCoord& InCellCoord, uint32 InGridSize)
		{
			return FBox(
				FVector(
					InCellCoord.X * InGridSize,
					InCellCoord.Y * InGridSize,
					InCellCoord.Z * InGridSize
				),
				FVector(
					InCellCoord.X * InGridSize + InGridSize,
					InCellCoord.Y * InGridSize + InGridSize,
					InCellCoord.Z * InGridSize + InGridSize
				)
			);
		}
	};

#if WITH_EDITOR
	APartitionActor* GetActor(const FActorPartitionGetParams& GetParam);
	APartitionActor* GetActor(const TSubclassOf<APartitionActor>& InActorClass, const FCellCoord& InCellCoords, bool bInCreate);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
#endif
	bool IsLevelPartition() const;

private:

#if WITH_EDITOR
	void OnActorPartitionHashInvalidated(const FCellCoord& Hash);

	void InitializeActorPartition();

	TMap<FCellCoord, TMap<UClass*, TWeakObjectPtr<APartitionActor>>> PartitionedActors;
	TUniquePtr<FBaseActorPartition> ActorPartition;
	
	FPartitionActorDescFactory PartitionActorDescFactory;

	FDelegateHandle ActorPartitionHashInvalidatedHandle;
#endif
};

#if WITH_EDITOR
/**
 * FBaseActorPartition
 */
class FBaseActorPartition
{
public:
	FBaseActorPartition(UWorld* InWorld) : World(InWorld) {}
	virtual ~FBaseActorPartition() {}

	virtual UActorPartitionSubsystem::FCellCoord GetActorPartitionHash(const FActorPartitionGetParams& GetParams) const = 0;
	virtual APartitionActor* GetActor(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, const UActorPartitionSubsystem::FCellCoord& InCellCoord) = 0;

	DECLARE_EVENT_OneParam(FBaseActorPartition, FOnActorPartitionHashInvalidated, const UActorPartitionSubsystem::FCellCoord&);
	FOnActorPartitionHashInvalidated& GetOnActorPartitionHashInvalidated() { return OnActorPartitionHashInvalidated; }
protected:
	UWorld* World;

	FOnActorPartitionHashInvalidated OnActorPartitionHashInvalidated;

	friend class FActorPartitionGridHelper;
};


class ENGINE_API FActorPartitionGridHelper
{
public:
	static void ForEachIntersectingCell(const TSubclassOf<APartitionActor>& InActorClass, const FBox& InBounds, ULevel* InLevel, TFunctionRef<bool(const UActorPartitionSubsystem::FCellCoord&, const FBox&)> InOperation);
};
#endif