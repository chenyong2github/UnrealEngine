// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCommonTypes.h"
#include "MassReplicationTypes.h"
#include "MassLODCollector.h"
#include "MassLODCalculator.h"

#include "MassReplicationFragments.generated.h"

class AMassClientBubbleInfoBase;
class UMassReplicatorBase;

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

USTRUCT()
struct FMassReplicationParameters : public FMassSharedFragment
{
	GENERATED_BODY()
public:
	FMassReplicationParameters();

	/** Distance where each LOD becomes relevant */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float LODDistance[EMassLOD::Max];

	/** Hysteresis percentage on delta between the LOD distances */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", meta = (ClampMin = "0.0", UIMin = "0.0"), config)
	float BufferHysteresisOnDistancePercentage = 10.0f;

	/** Maximum limit of entity per LOD */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	int32 LODMaxCount[EMassLOD::Max];

	/** Maximum limit of entity per LOD per viewer */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	int32 LODMaxCountPerViewer[EMassLOD::Max];

	/** Distance where each LOD becomes relevant */
	UPROPERTY(EditAnywhere, Category = "Mass|LOD", config)
	float UpdateInterval[EMassLOD::Max];

	UPROPERTY(EditAnywhere, Category = "Mass|Replication", config)
	TSubclassOf<AMassClientBubbleInfoBase> BubbleInfoClass;

	UPROPERTY(EditAnywhere, Category = "Mass|Replication", config)
	TSubclassOf<UMassReplicatorBase> ReplicatorClass;

};

USTRUCT()
struct FMassReplicationSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()
public:
	FMassReplicationSharedFragment() = default;
	FMassReplicationSharedFragment(UMassReplicationSubsystem& ReplicationSubsystem, const FMassReplicationParameters& Params);

	FMassBubbleInfoClassHandle BubbleInfoClassHandle;

	TArray<FMassClientHandle, TInlineAllocator<UE::Mass::Replication::MaxNumOfClients>> CachedClientHandles;
	TArray<bool, TInlineAllocator<UE::Mass::Replication::MaxNumOfClients>> bBubbleChanged;

	//TODO review if we need to have this as a UPROPERTY at all and also if we can make this use a TInlineAllocator
	//Can not use TInlineAllocator with UPROPERTY()
	UPROPERTY(Transient)
	TArray<AMassClientBubbleInfoBase*> BubbleInfos;

	TMassLODCollector<FReplicationLODLogic> LODCollector;
	TMassLODCalculator<FReplicationLODLogic> LODCalculator;
	bool bHasAdjustedDistancesFromCount = false;

	bool bEntityQueryInitialized = false;
	FMassEntityQuery EntityQuery;

	UPROPERTY(Transient)
	mutable UMassReplicatorBase* CachedReplicator = nullptr;

	template<typename T>
	T& GetTypedClientBubbleInfoChecked(FMassClientHandle Handle)
	{
		checkSlow(BubbleInfos.IsValidIndex(Handle.GetIndex()));

		AMassClientBubbleInfoBase* BubbleInfo = BubbleInfos[Handle.GetIndex()];

		checkSlow(BubbleInfo && Cast<T>(BubbleInfo) != nullptr);

		return *static_cast<T*>(BubbleInfo);
	}
};