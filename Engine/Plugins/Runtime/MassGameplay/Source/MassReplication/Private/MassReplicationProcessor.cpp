// Copyright Epic Games, Inc. All Rights Reserved.UMassSimulationSettings

#include "MassReplicationProcessor.h"
#include "MassClientBubbleHandler.h"
#include "MassLODSubsystem.h"
#include "MassCommonFragments.h"

namespace UE::Mass::Replication
{
	int32 bDebugReplicationLOD = 0;
	FAutoConsoleVariableRef CVarDebugReplicationViewerLOD(TEXT("ai.debug.ReplicationLOD"), bDebugReplicationLOD, TEXT("Debug Replication LOD"), ECVF_Cheat);
} // UE::Mass::Crowd

//----------------------------------------------------------------------//
//  UMassReplicationProcessor
//----------------------------------------------------------------------//
UMassReplicationProcessor::UMassReplicationProcessor()
{
#if !UE_ALLOW_DEBUG_REPLICATION_BUBBLES_STANDALONE
	ExecutionFlags = int32(EProcessorExecutionFlags::Server);
#else
	ExecutionFlags = int32(EProcessorExecutionFlags::All);
#endif // UE_ALLOW_DEBUG_REPLICATION_BUBBLES_STANDALONE

	ProcessingPhase = EMassProcessingPhase::PostPhysics;
}

void UMassReplicationProcessor::ConfigureQueries()
{
	CollectViewerInfoQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	CollectViewerInfoQuery.AddRequirement<FMassReplicationViewerInfoFragment>(EMassFragmentAccess::ReadOnly);
	CollectViewerInfoQuery.AddSharedRequirement<FMassReplicationSharedFragment>(EMassFragmentAccess::ReadWrite);

	CalculateLODQuery.AddRequirement<FMassReplicationViewerInfoFragment>(EMassFragmentAccess::ReadOnly);
	CalculateLODQuery.AddRequirement<FMassReplicationLODFragment>(EMassFragmentAccess::ReadWrite);
	CalculateLODQuery.AddConstSharedRequirement<FMassReplicationParameters>();
	CalculateLODQuery.AddSharedRequirement<FMassReplicationSharedFragment>(EMassFragmentAccess::ReadWrite);

	AdjustLODDistancesQuery.AddRequirement<FMassReplicationViewerInfoFragment>(EMassFragmentAccess::ReadOnly);
	AdjustLODDistancesQuery.AddRequirement<FMassReplicationLODFragment>(EMassFragmentAccess::ReadWrite);
	AdjustLODDistancesQuery.AddSharedRequirement<FMassReplicationSharedFragment>(EMassFragmentAccess::ReadWrite);
	AdjustLODDistancesQuery.SetArchetypeFilter([](const FMassExecutionContext& Context)
	{
		const FMassReplicationSharedFragment& LODSharedFragment = Context.GetSharedFragment<FMassReplicationSharedFragment>();
		return LODSharedFragment.bHasAdjustedDistancesFromCount;
	});

	EntityQuery.AddRequirement<FMassNetworkIDFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FReplicationTemplateIDFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassReplicationLODFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassReplicatedAgentFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassReplicationParameters>();
	EntityQuery.AddSharedRequirement<FMassReplicationSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassReplicationProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

#if UE_REPLICATION_COMPILE_SERVER_CODE
	ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(World);

	check(ReplicationSubsystem);
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}

void UMassReplicationProcessor::PrepareExecution(UMassEntitySubsystem& EntitySubsystem)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	check(ReplicationSubsystem);

	//first synchronize clients and viewers
	ReplicationSubsystem->SynchronizeClientsAndViewers();

	check(LODSubsystem);
	const TArray<FViewerInfo>& Viewers = LODSubsystem->GetViewers();
	EntitySubsystem.ForEachSharedFragment<FMassReplicationSharedFragment>([this, &Viewers](FMassReplicationSharedFragment& RepSharedFragment)
	{
		if (!RepSharedFragment.bEntityQueryInitialized)
		{
			RepSharedFragment.EntityQuery = EntityQuery;
			RepSharedFragment.EntityQuery.SetArchetypeFilter([&RepSharedFragment](const FMassExecutionContext& Context)
			{
				const FMassReplicationSharedFragment& CurRepSharedFragment = Context.GetSharedFragment<FMassReplicationSharedFragment>();
				return &CurRepSharedFragment == &RepSharedFragment;
			});
			RepSharedFragment.CachedReplicator->AddRequirements(RepSharedFragment.EntityQuery);
			RepSharedFragment.bEntityQueryInitialized = true;
		}

		RepSharedFragment.LODCollector.PrepareExecution(Viewers);
		RepSharedFragment.LODCalculator.PrepareExecution(Viewers);

		const TArray<FMassClientHandle>& CurrentClientHandles = ReplicationSubsystem->GetClientReplicationHandles();
		const int32 MinNumHandles = FMath::Min(RepSharedFragment.CachedClientHandles.Num(), CurrentClientHandles.Num());

		//check to see if we don't have enough cached client handles
		if (RepSharedFragment.CachedClientHandles.Num() < CurrentClientHandles.Num())
		{
			RepSharedFragment.CachedClientHandles.Reserve(CurrentClientHandles.Num());
			RepSharedFragment.bBubbleChanged.Reserve(CurrentClientHandles.Num());
			RepSharedFragment.BubbleInfos.Reserve(CurrentClientHandles.Num());

			for (int32 Idx = RepSharedFragment.CachedClientHandles.Num(); Idx < CurrentClientHandles.Num(); ++Idx)
			{
				const FMassClientHandle& CurrentClientHandle = CurrentClientHandles[Idx];

				RepSharedFragment.CachedClientHandles.Add(CurrentClientHandle);
				RepSharedFragment.bBubbleChanged.Add(true);
				AMassClientBubbleInfoBase* Info = CurrentClientHandle.IsValid() ?
					ReplicationSubsystem->GetClientBubbleChecked(RepSharedFragment.BubbleInfoClassHandle, CurrentClientHandle) :
					nullptr;

				check(Info);

				RepSharedFragment.BubbleInfos.Add(Info);
			}
		}
		//check to see if we have too many cached client handles
		else if (RepSharedFragment.CachedClientHandles.Num() > CurrentClientHandles.Num())
		{
			const int32 NumRemove = RepSharedFragment.CachedClientHandles.Num() - CurrentClientHandles.Num();

			RepSharedFragment.CachedClientHandles.RemoveAt(CurrentClientHandles.Num(), NumRemove, /* bAllowShrinking */ false);
			RepSharedFragment.bBubbleChanged.RemoveAt(CurrentClientHandles.Num(), NumRemove, /* bAllowShrinking */ false);
			RepSharedFragment.BubbleInfos.RemoveAt(CurrentClientHandles.Num(), NumRemove, /* bAllowShrinking */ false);
		}

		//check to see if any cached client handles have changed, if they have set the BubbleInfo[] appropriately
		for (int32 Idx = 0; Idx < MinNumHandles; ++Idx)
		{
			const FMassClientHandle& CurrentClientHandle = CurrentClientHandles[Idx];
			FMassClientHandle& CachedClientHandle = RepSharedFragment.CachedClientHandles[Idx];

			const bool bChanged = (RepSharedFragment.bBubbleChanged[Idx] = (CurrentClientHandle != CachedClientHandle));
			if (bChanged)
			{
				AMassClientBubbleInfoBase* Info = CurrentClientHandle.IsValid() ?
					ReplicationSubsystem->GetClientBubbleChecked(RepSharedFragment.BubbleInfoClassHandle, CurrentClientHandle) :
					nullptr;

				check(Info);

				RepSharedFragment.BubbleInfos[Idx] = Info;
				CachedClientHandle = CurrentClientHandle;
			}
		}
	});

#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}

void UMassReplicationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE
	check(World);
	check(LODSubsystem);
	check(ReplicationSubsystem);

	{
		QUICK_SCOPE_CYCLE_COUNTER(UMassReplicationProcessor_Preperation);
		PrepareExecution(EntitySubsystem);
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(UMassReplicationProcessor_LODCaculation);
		CollectViewerInfoQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
			const TArrayView<FMassReplicationViewerInfoFragment> ViewersInfoList = Context.GetMutableFragmentView<FMassReplicationViewerInfoFragment>();
			FMassReplicationSharedFragment& RepSharedFragment = Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
			RepSharedFragment.LODCollector.CollectLODInfo(Context, LocationList, ViewersInfoList, ViewersInfoList);
		});

		CalculateLODQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FMassReplicationViewerInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassReplicationViewerInfoFragment>();
			const TArrayView<FMassReplicationLODFragment> ViewerLODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();
			FMassReplicationSharedFragment& RepSharedFragment = Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
			RepSharedFragment.LODCalculator.CalculateLOD(Context, ViewersInfoList, ViewerLODList, ViewersInfoList);
		});

		EntitySubsystem.ForEachSharedFragment<FMassReplicationSharedFragment>([](FMassReplicationSharedFragment& RepSharedFragment)
		{
			RepSharedFragment.bHasAdjustedDistancesFromCount = RepSharedFragment.LODCalculator.AdjustDistancesFromCount();
		});

		AdjustLODDistancesQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FMassReplicationViewerInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassReplicationViewerInfoFragment>();
			const TArrayView<FMassReplicationLODFragment> ViewerLODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();
			FMassReplicationSharedFragment& RepSharedFragment = Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
			RepSharedFragment.LODCalculator.AdjustLODFromCount(Context, ViewersInfoList, ViewerLODList, ViewersInfoList);
		});
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(UMassReplicationProcessor_ProcessClientReplication);
		FMassReplicationContext ReplicationContext(*World, *LODSubsystem, *ReplicationSubsystem);
		EntitySubsystem.ForEachSharedFragment<FMassReplicationSharedFragment>([this, &EntitySubsystem, &Context, &ReplicationContext](FMassReplicationSharedFragment& RepSharedFragment)
		{
			RepSharedFragment.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&ReplicationContext, &RepSharedFragment](FMassExecutionContext& Context)
			{
				RepSharedFragment.CachedReplicator->ProcessClientReplication(Context, ReplicationContext);
			});
		});
	}

	// Optional debug display
	if (UE::Mass::Replication::bDebugReplicationLOD)
	{
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FMassReplicationLODFragment> ViewerLODList = Context.GetFragmentView<FMassReplicationLODFragment>();
			FMassReplicationSharedFragment& RepSharedFragment = Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();
			RepSharedFragment.LODCalculator.DebugDisplayLOD(Context, ViewerLODList, TransformList, World);
		});
	}
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}