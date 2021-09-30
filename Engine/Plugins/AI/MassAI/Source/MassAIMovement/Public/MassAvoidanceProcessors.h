// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassCommonFragments.h"
#include "MassSimulationLOD.h"
#include "MovementProcessor.h"
#include "NavMesh/RecastNavMesh.h"
#include "ZoneGraphTypes.h"
#include "MassAvoidanceProcessors.generated.h"

class UZoneGraphSubsystem;

MASSAIMOVEMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogAvoidance, Warning, All);
MASSAIMOVEMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogAvoidanceVelocities, Warning, All);
MASSAIMOVEMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogAvoidanceAgents, Warning, All);
MASSAIMOVEMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogAvoidanceObstacles, Warning, All);

USTRUCT()
struct MASSAIMOVEMENT_API FMassEdgeDetectionParamsFragment : public FMassFragment
{
	GENERATED_BODY()
	float EdgeDetectionRange = 500.f;
};

/** Edge with normal */
struct FNavigationAvoidanceEdge
{
	FNavigationAvoidanceEdge(const FVector InStart, const FVector InEnd)
	{
		Start = InStart;
		End = InEnd;
		LeftDir = FVector::CrossProduct((End - Start).GetSafeNormal(), FVector::UpVector);
	}
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FVector LeftDir = FVector::ZeroVector;
};

USTRUCT()
struct MASSAIMOVEMENT_API FMassNavigationEdgesFragment : public FMassFragment
{
	GENERATED_BODY()

	static const int MaxEdgesCount = 8;
	TArray<FNavigationAvoidanceEdge, TFixedAllocator<MaxEdgesCount>> AvoidanceEdges;
};

/** Component tag to tell the avoidance to extend the size of this obstacle when too close to edges. */
USTRUCT()
struct MASSAIMOVEMENT_API FMassAvoidanceExtendToEdgeObstacleTag : public FMassTag
{
	GENERATED_BODY()
};

/** Experimental: move using cumulative forces to avoid close agents */
UCLASS()
class MASSAIMOVEMENT_API UMassAvoidanceProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassAvoidanceProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

private:
	TWeakObjectPtr<UWorld> WeakWorld;
	TWeakObjectPtr<UMassMovementSubsystem> WeakMovementSubsystem;
	FMassEntityQuery EntityQuery;
};

/** Avoidance while standing. */
UCLASS()
class MASSAIMOVEMENT_API UMassStandingAvoidanceProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassStandingAvoidanceProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

private:
	TWeakObjectPtr<UWorld> WeakWorld;
	TWeakObjectPtr<UMassMovementSubsystem> WeakMovementSubsystem;
	FMassEntityQuery EntityQuery;
};

/** Component Tag to tell if the entity is in the avoidance obstacle grid */
USTRUCT()
struct MASSAIMOVEMENT_API FMassInAvoidanceObstacleGridTag : public FMassTag
{
	GENERATED_BODY()
};

/** Processor to update avoidance obstacle data */
UCLASS()
class MASSAIMOVEMENT_API UMassAvoidanceObstacleProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassAvoidanceObstacleProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

private:
	TWeakObjectPtr<UMassMovementSubsystem> WeakMovementSubsystem;
	FMassEntityQuery AddToGridEntityQuery;
	FMassEntityQuery UpdateGridEntityQuery;
	FMassEntityQuery RemoveFromGridEntityQuery;
};

/** Experimental: navmesh edges gathering */
UCLASS()
class MASSAIMOVEMENT_API UMassNavigationBoundaryProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassNavigationBoundaryProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

private:
	TWeakObjectPtr<UWorld> WeakWorld;
	TWeakObjectPtr<UMassMovementSubsystem> WeakMovementSubsystem;
	TWeakObjectPtr<ANavigationData> WeakNavData;
	FMassEntityQuery EntityQuery;
};

USTRUCT()
struct MASSAIMOVEMENT_API FMassLastUpdatePositionFragment : public FMassFragment
{
	GENERATED_BODY()
	FVector Value;
};

USTRUCT()
struct MASSAIMOVEMENT_API FMassAvoidanceBoundaryLastLaneHandleFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Not replicated but computed and used on client and server */
	FZoneGraphLaneHandle LaneHandle;
};

/** Experimental: lane borders gathering */
UCLASS()
class MASSAIMOVEMENT_API UMassLaneBoundaryProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassLaneBoundaryProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

private:
	TWeakObjectPtr<UWorld> WeakWorld;
	TWeakObjectPtr<UZoneGraphSubsystem> WeakZoneGraph;
	FMassEntityQuery EntityQuery;
};

USTRUCT()
struct MASSAIMOVEMENT_API FMassLaneCacheBoundaryFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Last update position. */
	FVector LastUpdatePosition = FVector::ZeroVector;

	/** Lane cached ID at last update. */
	uint16 LastUpdateCacheID = 0;
};

/** ZoneGraph lane cache boundary processor */
// @todo MassMovement: Make this signal based.
UCLASS()
class MASSAIMOVEMENT_API UMassLaneCacheBoundaryProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassLaneCacheBoundaryProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

private:
	TWeakObjectPtr<UWorld> WeakWorld;
	FMassEntityQuery EntityQuery;
};