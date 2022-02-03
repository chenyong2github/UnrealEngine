// Copyright Epic Games, Inc. All Rights Reserved.UMassSimulationSettings

#include "MassReplicationProcessorBase.h"
#include "MassClientBubbleHandler.h"
#include "MassLODManager.h"
#include "MassCommonFragments.h"

//----------------------------------------------------------------------//
//  UMassProcessor_Replication
//----------------------------------------------------------------------//
UMassReplicationProcessorBase::UMassReplicationProcessorBase()
{
	LODDistance[EMassLOD::High] = 0.f;
	LODDistance[EMassLOD::Medium] = 1000.f;
	LODDistance[EMassLOD::Low] = 2500.f;
	LODDistance[EMassLOD::Off] = 5000.f;
	
	LODMaxCount[EMassLOD::High] = 1600;
	LODMaxCount[EMassLOD::Medium] = 3200;
	LODMaxCount[EMassLOD::Low] = 48000;
	LODMaxCount[EMassLOD::Off] = 0;

	LODMaxCountPerViewer[EMassLOD::High] = 100;
	LODMaxCountPerViewer[EMassLOD::Medium] = 200;
	LODMaxCountPerViewer[EMassLOD::Low] = 300;
	LODMaxCountPerViewer[EMassLOD::Off] = 0;
	
	UpdateInterval[EMassLOD::High] = 0.1f;
	UpdateInterval[EMassLOD::Medium] = 0.2f;
	UpdateInterval[EMassLOD::Low] = 0.3f;
	UpdateInterval[EMassLOD::Off] = 0.5f;

	ExecutionFlags = int32(EProcessorExecutionFlags::None);
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
}

void UMassReplicationProcessorBase::ConfigureQueries()
{
	CollectViewerInfoQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	CollectViewerInfoQuery.AddRequirement<FMassReplicationViewerInfoFragment>(EMassFragmentAccess::ReadOnly);

	CalculateLODQuery.AddRequirement<FMassReplicationViewerInfoFragment>(EMassFragmentAccess::ReadOnly);
	CalculateLODQuery.AddRequirement<FMassReplicationLODFragment>(EMassFragmentAccess::ReadWrite);

	EntityQuery.AddRequirement<FMassNetworkIDFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_ReplicationTemplateID>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassReplicationLODFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassReplicatedAgentFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassReplicationProcessorBase::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

#if UE_REPLICATION_COMPILE_SERVER_CODE
	LODCalculator.Initialize(LODDistance, BufferHysteresisOnDistancePercentage / 100.0f, LODMaxCount, LODMaxCountPerViewer);

	ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(World);

	check(ReplicationSubsystem);

	//BubbleInfoClassHandle = ReplicationSubsystem->GetBubbleInfoClassHandle(AMassCrowdClientBubbleInfo::StaticClass());
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}

void UMassReplicationProcessorBase::PrepareExecution()
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	check(ReplicationSubsystem);

	//first synchronize clients and viewers
	ReplicationSubsystem->SynchronizeClientsAndViewers();

	//then call base class
	check(LODManager);
	const TArray<FViewerInfo>& Viewers = LODManager->GetViewers();
	LODCollector.PrepareExecution(Viewers);
	LODCalculator.PrepareExecution(Viewers);

	const TArray<FMassClientHandle>& CurrentClientHandles = ReplicationSubsystem->GetClientReplicationHandles();
	const int32 MinNumHandles = FMath::Min(CachedClientHandles.Num(), CurrentClientHandles.Num());

	//check to see if we don't have enough cached client handles
	if (CachedClientHandles.Num() < CurrentClientHandles.Num())
	{
		CachedClientHandles.Reserve(CurrentClientHandles.Num());
		bBubbleChanged.Reserve(CurrentClientHandles.Num());
		BubbleInfos.Reserve(CurrentClientHandles.Num());

		for (int32 Idx = CachedClientHandles.Num(); Idx < CurrentClientHandles.Num(); ++Idx)
		{
			const FMassClientHandle& CurrentClientHandle = CurrentClientHandles[Idx];

			CachedClientHandles.Add(CurrentClientHandle);
			bBubbleChanged.Add(true);
			AMassClientBubbleInfoBase* Info = CurrentClientHandle.IsValid() ?
				ReplicationSubsystem->GetClientBubbleChecked(BubbleInfoClassHandle, CurrentClientHandle) :
				nullptr;

			check(Info);

			BubbleInfos.Add(Info);
		}
	}
	//check to see if we have too many cached client handles
	else if (CachedClientHandles.Num() > CurrentClientHandles.Num())
	{
		const int32 NumRemove = CachedClientHandles.Num() - CurrentClientHandles.Num();

		CachedClientHandles.RemoveAt(CurrentClientHandles.Num(), NumRemove, /* bAllowShrinking */ false);
		bBubbleChanged.RemoveAt(CurrentClientHandles.Num(), NumRemove, /* bAllowShrinking */ false);
		BubbleInfos.RemoveAt(CurrentClientHandles.Num(), NumRemove, /* bAllowShrinking */ false);
	}

	//check to see if any cached client handles have changed, if they have set the BubbleInfo[] appropriately
	for (int32 Idx = 0; Idx < MinNumHandles; ++Idx)
	{
		const FMassClientHandle& CurrentClientHandle = CurrentClientHandles[Idx];
		FMassClientHandle& CachedClientHandle = CachedClientHandles[Idx];

		const bool bChanged = (bBubbleChanged[Idx] = (CurrentClientHandle != CachedClientHandle));
		if (bChanged)
		{
			AMassClientBubbleInfoBase* Info = CurrentClientHandle.IsValid() ?
				ReplicationSubsystem->GetClientBubbleChecked(BubbleInfoClassHandle, CurrentClientHandle) :
				nullptr;

			check(Info);

			BubbleInfos[Idx] = Info;
			CachedClientHandle = CurrentClientHandle;
		}
	}

#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}

void UMassReplicationProcessorBase::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE
	check(World);

	PrepareExecution();

	CollectViewerInfoQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
		const TArrayView<FMassReplicationViewerInfoFragment> ViewersInfoList = Context.GetMutableFragmentView<FMassReplicationViewerInfoFragment>();
		LODCollector.CollectLODInfo(Context, LocationList, ViewersInfoList, ViewersInfoList);
	});

	CalculateLODQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassReplicationViewerInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassReplicationViewerInfoFragment>();
		const TArrayView<FMassReplicationLODFragment> ViewerLODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();
		LODCalculator.CalculateLOD(Context, ViewersInfoList, ViewerLODList, ViewersInfoList);
	});

	if (LODCalculator.AdjustDistancesFromCount())
	{
		CalculateLODQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FMassReplicationViewerInfoFragment> ViewersInfoList = Context.GetFragmentView<FMassReplicationViewerInfoFragment>();
			const TArrayView<FMassReplicationLODFragment> ViewerLODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();
			LODCalculator.AdjustLODFromCount(Context, ViewersInfoList, ViewerLODList, ViewersInfoList);
		});
	}
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}
