// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "MassProcessor.h"
#include "MassCommonTypes.h"
#include "MassSimulationLOD.h"
#include "MassReplicationTypes.h"
#include "MassReplicationFragments.h"
#include "MassSpawnerTypes.h"
#include "MassLODManager.h"
#include "MassReplicationSubsystem.h"
#include "MassReplicationProcessorBase.generated.h"

class UMassReplicationSubsystem;
class AMassClientBubbleInfoBase;
class UWorld;

/** 
 *  Base processor that handles replication and only runs on the server. You should derive from this per entity type (that require different replication processing). It and its derived classes 
 *  query Mass entity fragments and set those values for replication when appropriate, using the MassClientBubbleHandler.
 */
UCLASS()
class MASSREPLICATION_API UMassReplicationProcessorBase : public UMassProcessor_LODBase
{
	GENERATED_BODY()

public:
	UMassReplicationProcessorBase();

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	void PrepareExecution(UMassEntitySubsystem& EntitySubsystem);

	/** 
	 *  Implemented as straight template callbacks as when profiled this was faster than TFunctionRef. Its probably easier to pass Lamdas in to these
	 *  but Functors can also be used as well as TFunctionRefs etc. Its also fairly straight forward to call member functions via some Lamda glue code
	 */
	template<typename AgentArrayItem, typename CacheViewsCallback, typename AddEntityCallback, typename ModifyEntityCallback, typename RemoveEntityCallback>
	void CalculateClientReplication(FMassExecutionContext& Context, CacheViewsCallback&& CacheViews, AddEntityCallback&& AddEntity, ModifyEntityCallback&& ModifyEntity, RemoveEntityCallback&& RemoveEntity);



protected:

	UPROPERTY()
	UMassReplicationSubsystem* ReplicationSubsystem;

	FMassEntityQuery CollectViewerInfoQuery;
	FMassEntityQuery CalculateLODQuery;
	FMassEntityQuery AdjustLODDistancesQuery;
	FMassEntityQuery EntityQuery;
};

template<typename AgentArrayItem, typename CacheViewsCallback, typename AddEntityCallback, typename ModifyEntityCallback, typename RemoveEntityCallback>
void UMassReplicationProcessorBase::CalculateClientReplication(FMassExecutionContext& Context, CacheViewsCallback&& CacheViews, AddEntityCallback&& AddEntity, ModifyEntityCallback&& ModifyEntity, RemoveEntityCallback&& RemoveEntity)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	check(ReplicationSubsystem);
	check(LODManager);
	check(World);

	const int32 NumEntities = Context.GetNumEntities();

	TConstArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetFragmentView<FMassNetworkIDFragment>();
	TArrayView<FMassReplicationLODFragment> ViewerLODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();
	TArrayView<FMassReplicatedAgentFragment> ReplicatedAgentList = Context.GetMutableFragmentView<FMassReplicatedAgentFragment>();
	TConstArrayView<FDataFragment_ReplicationTemplateID> TemplateIDList = Context.GetFragmentView<FDataFragment_ReplicationTemplateID>();
	FMassReplicationSharedFragment& RepSharedFragment = Context.GetMutableSharedFragment<FMassReplicationSharedFragment>();

	CacheViews(Context);

	const float Time = World->GetRealTimeSeconds();

	for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
	{
		FMassReplicatedAgentFragment& AgentFragment = ReplicatedAgentList[EntityIdx];

		if (AgentFragment.AgentsData.Num() < RepSharedFragment.CachedClientHandles.Num())
		{
			AgentFragment.AgentsData.AddDefaulted(RepSharedFragment.CachedClientHandles.Num() - AgentFragment.AgentsData.Num());
		}
		else if (AgentFragment.AgentsData.Num() > RepSharedFragment.CachedClientHandles.Num())
		{
			AgentFragment.AgentsData.RemoveAt(RepSharedFragment.CachedClientHandles.Num(), AgentFragment.AgentsData.Num() - RepSharedFragment.CachedClientHandles.Num(), /* bAllowShrinking */ false);
		}

		for (int32 ClientIdx = 0; ClientIdx < RepSharedFragment.CachedClientHandles.Num(); ++ClientIdx)
		{
			const FMassClientHandle& ClientHandle = RepSharedFragment.CachedClientHandles[ClientIdx];

			if (ClientHandle.IsValid())
			{
				checkSlow(RepSharedFragment.BubbleInfos[ClientHandle.GetIndex()] != nullptr);

				FMassReplicatedAgentArrayData& AgentData = AgentFragment.AgentsData[ClientIdx];

				//if the bubble has changed, set the handle Invalid. We will set this to something valid if the agent is going to replicate to the bubble
				//When a bubble has changed the existing AMassCrowdClientBubbleInfo will reset all the data associated with it.
				if (RepSharedFragment.bBubbleChanged[ClientIdx])
				{
					AgentData.Invalidate();
				}

				//now we need to see what our highest viewer LOD is on this client (split screen etc), we can use the unsafe version as we have already checked all the
				//CachedClientHandles for validity
				const FClientViewerHandles& ClientViewers = ReplicationSubsystem->GetClientViewersChecked(ClientHandle);

				EMassLOD::Type HighestLOD = EMassLOD::Off;

				for (const FMassViewerHandle& ViewerHandle : ClientViewers.Handles)
				{
					//this should always we valid as we synchronized the viewers just previously
					check(LODManager->IsValidViewer(ViewerHandle));

					const EMassLOD::Type MassLOD = ViewerLODList[EntityIdx].LODPerViewer[ViewerHandle.GetIndex()];
					check(MassLOD <= EMassLOD::Off);

					if (HighestLOD > MassLOD)
					{
						HighestLOD = MassLOD;
					}
				}

				if (HighestLOD < EMassLOD::Off)
				{
#if UE_ALLOW_DEBUG_REPLICATION
					AgentData.LOD = HighestLOD;
#endif // UE_ALLOW_DEBUG_REPLICATION

					//if the handle isn't valid we need to add the agent
					if (!AgentData.Handle.IsValid())
					{
						typename AgentArrayItem::FReplicatedAgentType ReplicatedAgent;

						const FMassNetworkIDFragment& NetIDFragment = NetworkIDList[EntityIdx];
						const FDataFragment_ReplicationTemplateID& TemplateIDFragment = TemplateIDList[EntityIdx];

						ReplicatedAgent.SetNetID(NetIDFragment.NetID);
						ReplicatedAgent.SetTemplateID(TemplateIDFragment.ID);

						AgentData.Handle = AddEntity(Context, EntityIdx, ReplicatedAgent, ClientHandle);

						AgentData.LastUpdateTime = Time;
					}
					else
					{
						ModifyEntity(Context, EntityIdx, HighestLOD, Time, AgentData.Handle, ClientHandle);
					}
				}
				else
				{
					// as this is a fresh handle, if its valid then we can use the unsafe remove function
					if (AgentData.Handle.IsValid())
					{
						RemoveEntity(Context, AgentData.Handle, ClientHandle);
						AgentData.Invalidate();
					}
				}
			}
		}
	}
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}
