// Copyright Epic Games, Inc. All Rights Reserved.UMassSimulationSettings

#include "MassReplicationProcessorBase.h"
#include "MassClientBubbleHandler.h"
#include "MassLODManager.h"
#include "MassCommonFragments.h"

//----------------------------------------------------------------------//
//  UMassReplicationProcessorBase
//----------------------------------------------------------------------//
UMassReplicationProcessorBase::UMassReplicationProcessorBase()
{
	ExecutionFlags = int32(EProcessorExecutionFlags::None);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
}

void UMassReplicationProcessorBase::ConfigureQueries()
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

void UMassReplicationProcessorBase::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

#if UE_REPLICATION_COMPILE_SERVER_CODE
	ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(World);

	check(ReplicationSubsystem);
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}

void UMassReplicationProcessorBase::PrepareExecution(UMassEntitySubsystem& EntitySubsystem)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	check(ReplicationSubsystem);

	//first synchronize clients and viewers
	ReplicationSubsystem->SynchronizeClientsAndViewers();

	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();
	EntitySubsystem.ForEachSharedFragment<FMassReplicationSharedFragment>([this, &Viewers](FMassReplicationSharedFragment& RepSharedFragment)
	{
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

void UMassReplicationProcessorBase::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE
	check(World);

	PrepareExecution(EntitySubsystem);

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

#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}