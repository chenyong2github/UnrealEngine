// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdReplicationProcessor.h"
#include "MassReplicationSubsystem.h"
#include "MassReplicationTypes.h"
#include "MassClientBubbleHandler.h"
#include "MassCrowdBubble.h"
#include "MassCommonFragments.h"
#include "MassLODManager.h"
#include "MassCrowdFragments.h"
#include "MassReplicationPathHandlers.h"
#include "MassReplicationTransformHandlers.h"

namespace UE { namespace Mass { namespace Crowd
{
	int32 bDebugReplicationViewerLOD = 0;
	FAutoConsoleVariableRef CVarDebugReplicationViewerLOD(TEXT("ai.debug.CrowdReplicationViewerLOD"), bDebugReplicationViewerLOD, TEXT("Crowd Debug Replication Viewer LOD"), ECVF_Cheat);
}}}

//----------------------------------------------------------------------//
//  UMassProcessor_Replication
//----------------------------------------------------------------------//
UMassCrowdReplicationProcessor::UMassCrowdReplicationProcessor()
{
#if !UE_ALLOW_DEBUG_REPLICATION_BUBBLES_STANDALONE
	ExecutionFlags = int32(EProcessorExecutionFlags::Server);
#else
	ExecutionFlags = int32(EProcessorExecutionFlags::All);
#endif // UE_ALLOW_DEBUG_REPLICATION_BUBBLES_STANDALONE
}

void UMassCrowdReplicationProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();

	FMassReplicationProcessorPositionYawHandler::AddRequirements(EntityQuery);
	FMassReplicationProcessorPathHandler::AddRequirements(EntityQuery);

	CollectViewerInfoQuery.AddTagRequirement<FTagFragment_MassCrowd>(EMassFragmentPresence::All);
	CalculateLODQuery.AddTagRequirement<FTagFragment_MassCrowd>(EMassFragmentPresence::All);
	EntityQuery.AddTagRequirement<FTagFragment_MassCrowd>(EMassFragmentPresence::All);
}

void UMassCrowdReplicationProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	check(ReplicationSubsystem);

	BubbleInfoClassHandle = ReplicationSubsystem->GetBubbleInfoClassHandle(AMassCrowdClientBubbleInfo::StaticClass());
}

void UMassCrowdReplicationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	Super::Execute(EntitySubsystem, Context);

	ProcessClientReplication(EntitySubsystem, Context);

#if WITH_MASSGAMEPLAY_DEBUG
	// Optional debug display
	if (UE::Mass::Crowd::bDebugReplicationViewerLOD)
	{
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
			{
				const TConstArrayView<FDataFragment_Transform> TransformList = Context.GetFragmentView<FDataFragment_Transform>();
				const TConstArrayView<FMassReplicationLODFragment> ViewerLODList = Context.GetFragmentView<FMassReplicationLODFragment>();
				LODCalculator.DebugDisplayLOD(Context, ViewerLODList, TransformList, World);
			});
	}
#endif // WITH_MASSGAMEPLAY_DEBUG
}

void UMassCrowdReplicationProcessor::ProcessClientReplication(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	FMassReplicationProcessorPathHandler PathHandler;
	FMassReplicationProcessorPositionYawHandler PositionYawHandler;

	auto CacheViewsCallback = [&Context, &PathHandler, &PositionYawHandler]()
	{
		PathHandler.CacheFragmentViews(Context);
		PositionYawHandler.CacheFragmentViews(Context);
	};

	auto AddEntityCallback = [this, &Context, &PathHandler, &PositionYawHandler](const int32 EntityIdx, FReplicatedCrowdAgent& InReplicatedAgent, const FMassClientHandle ClientHandle)->FMassReplicatedAgentHandle
	{
		AMassCrowdClientBubbleInfo& CrowdBubbleInfo = GetTypedClientBubbleInfoChecked<AMassCrowdClientBubbleInfo>(ClientHandle);

		PathHandler.AddEntity(EntityIdx, InReplicatedAgent.GetReplicatedPathDataMutable());
		PositionYawHandler.AddEntity(EntityIdx, InReplicatedAgent.GetReplicatedPositionYawDataMutable());

		return CrowdBubbleInfo.GetCrowdSerializer().Bubble.AddAgent(Context.GetEntity(EntityIdx), InReplicatedAgent);
	};

	auto ModifyEntityCallback = [this, &PathHandler](const int32 EntityIdx, const EMassLOD::Type LOD, const float Time, const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		AMassCrowdClientBubbleInfo& CrowdBubbleInfo = GetTypedClientBubbleInfoChecked<AMassCrowdClientBubbleInfo>(ClientHandle);

		FMassCrowdClientBubbleHandler& Bubble = CrowdBubbleInfo.GetCrowdSerializer().Bubble;

		PathHandler.ModifyEntity<FCrowdFastArrayItem>(Handle, EntityIdx, Bubble.GetPathHandlerMutable());

		// Don't call the PositionYawHandler here as we currently only replicate the position and yaw when we add an entity to Mass
	};

	auto RemoveEntityCallback = [this](const FMassReplicatedAgentHandle Handle, const FMassClientHandle ClientHandle)
	{
		AMassCrowdClientBubbleInfo& CrowdBubbleInfo = GetTypedClientBubbleInfoChecked<AMassCrowdClientBubbleInfo>(ClientHandle);

		CrowdBubbleInfo.GetCrowdSerializer().Bubble.RemoveAgentChecked(Handle);
	};

	QUICK_SCOPE_CYCLE_COUNTER(UMassCrowdReplicationProcessor_ProcessClientReplication);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, &CacheViewsCallback, &AddEntityCallback, &ModifyEntityCallback, &RemoveEntityCallback](FMassExecutionContext& Context)
	{
		CalculateClientReplication<FCrowdFastArrayItem>(Context, CacheViewsCallback, AddEntityCallback, ModifyEntityCallback, RemoveEntityCallback);
	});
#endif // UE_REPLICATION_COMPILE_SERVER_CODE
}
