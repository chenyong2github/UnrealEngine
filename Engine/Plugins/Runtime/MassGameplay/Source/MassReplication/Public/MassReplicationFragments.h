// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCommonTypes.h"
#include "MassReplicationTypes.h"
// #include "MassLODManager.h"
// #include "IndexedHandle.h"
// #include "MassEntityTemplate.h"
// #include "MassObserverProcessor.h"
// #include "MassLODLogic.h"

#include "MassReplicationFragments.generated.h"

/**
 *  Fragment type for the mass network id of a mass entity
 */
USTRUCT()
struct MASSREPLICATION_API FMassNetworkIDFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassNetworkID NetID;
};

struct FMassReplicatedAgentArrayData
{
	void Invalidate()
	{
		Handle.Invalidate();
		LastUpdateTime = 0.f;
#if UE_ALLOW_DEBUG_REPLICATION
		LOD = EMassLOD::Off;
#endif // UE_DO_DEBUG_REPLICATION
	}

	FMassReplicatedAgentHandle Handle;
	float LastUpdateTime = 0.f;

#if UE_ALLOW_DEBUG_REPLICATION
	EMassLOD::Type LOD = EMassLOD::Off;
#endif // UE_DO_DEBUG_REPLICATION
};

/**
 * Agent handle per client, these will be at TArray indices of the Client handles indicies (used as a free list array)
 */
USTRUCT()
struct MASSREPLICATION_API FMassReplicatedAgentFragment : public FMassFragment
{
	GENERATED_BODY()

	//these will be maintained so that the TArray entries are at the same indices as Client Handles
	TArray<FMassReplicatedAgentArrayData, TInlineAllocator<UE::Mass::Replication::MaxNumOfClients>> AgentsData;
};

/*
 * Data fragment to store the calculated distances to viewers
 */
USTRUCT()
struct MASSREPLICATION_API FMassReplicationViewerInfoFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Closest viewer distance */
	float ClosestViewerDistanceSq;

	/** Distance between each viewer and entity */
	TArray<float> DistanceToViewerSq;
};

USTRUCT()
struct MASSREPLICATION_API FMassReplicationLODFragment : public FMassFragment
{
	GENERATED_BODY()

	/**LOD information */
	TEnumAsByte<EMassLOD::Type> LOD = EMassLOD::Max;

	/** Previous LOD information*/
	TEnumAsByte<EMassLOD::Type> PrevLOD = EMassLOD::Max;

	/** Per viewer LOD information */
	TArray<TEnumAsByte<EMassLOD::Type>> LODPerViewer;

	/** Per viewer previous LOD information */
	TArray<TEnumAsByte<EMassLOD::Type>> PrevLODPerViewer;
};

UCLASS()
class MASSREPLICATION_API UMassNetworkIDFragmentInitializer : public UMassObserverProcessor
{
	GENERATED_BODY()
public:
	UMassNetworkIDFragmentInitializer();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	UPROPERTY()
	UMassReplicationSubsystem* ReplicationSubsystem = nullptr;

	FMassEntityQuery EntityQuery;
};
