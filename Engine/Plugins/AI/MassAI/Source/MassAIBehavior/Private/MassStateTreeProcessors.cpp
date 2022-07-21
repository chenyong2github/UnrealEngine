// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassStateTreeProcessors.h"
#include "StateTree.h"
#include "MassStateTreeExecutionContext.h"
#include "MassEntityView.h"
#include "MassNavigationTypes.h"
#include "MassSimulationLOD.h"
#include "MassComponentHitTypes.h"
#include "MassSmartObjectTypes.h"
#include "MassZoneGraphAnnotationTypes.h"
#include "MassStateTreeSubsystem.h"
#include "MassSignals/Public/MassSignalSubsystem.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Engine/World.h"
#include "MassBehaviorSettings.h"
#include "VisualLogger/VisualLogger.h"

CSV_DEFINE_CATEGORY(StateTreeProcessor, true);

namespace UE::MassBehavior
{

bool SetExternalFragments(FMassStateTreeExecutionContext& Context, const UMassEntitySubsystem& EntitySubsystem)
{
	bool bFoundAllFragments = true;
	const FMassEntityView EntityView(EntitySubsystem, Context.GetEntity());
	for (const FStateTreeExternalDataDesc& DataDesc : Context.GetExternalDataDescs())
	{
		if (DataDesc.Struct == nullptr)
		{
			continue;
		}
		
		if (DataDesc.Struct->IsChildOf(FMassFragment::StaticStruct()))
		{
			const UScriptStruct* ScriptStruct = Cast<const UScriptStruct>(DataDesc.Struct);
			FStructView Fragment = EntityView.GetFragmentDataStruct(ScriptStruct);
			if (Fragment.IsValid())
			{
				Context.SetExternalData(DataDesc.Handle, FStateTreeDataView(Fragment));
			}
			else
			{
				if (DataDesc.Requirement == EStateTreeExternalDataRequirement::Required)
				{
					// Note: Not breaking here, so that we can validate all missing ones in one go with FMassStateTreeExecutionContext::AreExternalDataViewsValid().
					bFoundAllFragments = false;
				}
			}
		}
		else if (DataDesc.Struct->IsChildOf(FMassSharedFragment::StaticStruct()))
		{
			const UScriptStruct* ScriptStruct = Cast<const UScriptStruct>(DataDesc.Struct);
			FConstStructView Fragment = EntityView.GetConstSharedFragmentDataStruct(ScriptStruct);
			if (Fragment.IsValid())
			{
				Context.SetExternalData(DataDesc.Handle, FStateTreeDataView(Fragment.GetScriptStruct(), const_cast<uint8*>(Fragment.GetMemory())));
			}
			else
			{
				if (DataDesc.Requirement == EStateTreeExternalDataRequirement::Required)
				{
					// Note: Not breaking here, so that we can validate all missing ones in one go with FMassStateTreeExecutionContext::AreExternalDataViewsValid().
					bFoundAllFragments = false;
				}
			}
		}
	}
	return bFoundAllFragments;
}
	
bool SetExternalSubsystems(FMassStateTreeExecutionContext& Context)
{
	const UWorld* World = Context.GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	bool bFoundAllSubsystems = true;
	for (const FStateTreeExternalDataDesc& DataDesc : Context.GetExternalDataDescs())
	{
		if (DataDesc.Struct && DataDesc.Struct->IsChildOf(UWorldSubsystem::StaticClass()))
		{
			const TSubclassOf<UWorldSubsystem> SubClass = Cast<UClass>(const_cast<UStruct*>(DataDesc.Struct));
			UWorldSubsystem* Subsystem = World->GetSubsystemBase(SubClass);
			if (Subsystem)
			{
				Context.SetExternalData(DataDesc.Handle, FStateTreeDataView(Subsystem));
			}
			else
			{
				if (DataDesc.Requirement == EStateTreeExternalDataRequirement::Required)
				{
					// Note: Not breaking here, so that we can validate all missing ones in one go with FMassStateTreeExecutionContext::AreExternalDataViews Valid().
					bFoundAllSubsystems = false;
				}
			}
		}
	}
	return bFoundAllSubsystems;
}

template<typename TFunc>
void ForEachEntityInChunk(FMassExecutionContext& Context, FMassStateTreeExecutionContext& StateTreeContext, UMassStateTreeSubsystem& MassStateTreeSubsystem, TFunc&& Callback)
{
	const TArrayView<FMassStateTreeInstanceFragment> StateTreeInstanceList = Context.GetMutableFragmentView<FMassStateTreeInstanceFragment>();
	const FMassStateTreeSharedFragment& SharedStateTree = Context.GetConstSharedFragment<FMassStateTreeSharedFragment>();

	// Assuming that all the entities share same StateTree, because they all should have the same storage fragment.
	const int32 NumEntities = Context.GetNumEntities();
	check(NumEntities > 0);
	
	UStateTree* StateTree = SharedStateTree.StateTree;

	// Initialize the execution context if changed between chunks.
	if (StateTreeContext.GetStateTree() != StateTree)
	{
		// Gather subsystems.
		if (StateTreeContext.Init(MassStateTreeSubsystem, *StateTree, EStateTreeStorage::External))
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeProcessorExternalSubsystems);
			if (!ensureMsgf(UE::MassBehavior::SetExternalSubsystems(StateTreeContext), TEXT("StateTree will not execute due to missing subsystem requirements.")))
			{
				return;
			}
		}
		else
		{
			return;
		}
	}

	for (int32 EntityIndex = 0; EntityIndex < NumEntities; EntityIndex++)
	{
		const FMassEntityHandle Entity = Context.GetEntity(EntityIndex);
		FMassStateTreeInstanceFragment& StateTreeInstance = StateTreeInstanceList[EntityIndex];
		
		StateTreeContext.SetEntity(Entity);

		// Gather all required fragments.
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeProcessorExternalFragments);
			if (!ensureMsgf(UE::MassBehavior::SetExternalFragments(StateTreeContext, StateTreeContext.GetEntitySubsystem()), TEXT("StateTree will not execute due to missing required fragments.")))
			{
				break;
			}
		}

		// Make sure all required external data are set.
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeProcessorExternalDataValidation);
			// TODO: disable this when not in debug.
			if (!ensureMsgf(StateTreeContext.AreExternalDataViewsValid(), TEXT("StateTree will not execute due to missing external data.")))
			{
				break;
			}
		}

		FStateTreeInstanceData* InstanceData = MassStateTreeSubsystem.GetInstanceData(StateTreeInstance.InstanceHandle);
		if (InstanceData != nullptr)
		{
			Callback(StateTreeContext, StateTreeInstance, *InstanceData);
		}
	}
}

} // UE::MassBehavior


//----------------------------------------------------------------------//
// UMassStateTreeFragmentDestructor
//----------------------------------------------------------------------//
UMassStateTreeFragmentDestructor::UMassStateTreeFragmentDestructor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ObservedType = FMassStateTreeInstanceFragment::StaticStruct();
	Operation = EMassObservedOperation::Remove;
	bRequiresGameThreadExecution = true;
}

void UMassStateTreeFragmentDestructor::Initialize(UObject& Owner)
{
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
}

void UMassStateTreeFragmentDestructor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassStateTreeInstanceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassStateTreeSharedFragment>();
	EntityQuery.AddSubsystemRequirement<UMassStateTreeSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassStateTreeFragmentDestructor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (SignalSubsystem == nullptr)
	{
		return;
	}
		
	FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, *SignalSubsystem, Context);

	EntityQuery.ForEachEntityChunk(EntitySubsystem,Context,
		[this, &StateTreeContext, World = EntitySubsystem.GetWorld()](FMassExecutionContext& Context)
		{
			UMassStateTreeSubsystem& MassStateTreeSubsystem = Context.GetMutableSubsystemChecked<UMassStateTreeSubsystem>(World);
			const TArrayView<FMassStateTreeInstanceFragment> StateTreeInstanceList = Context.GetMutableFragmentView<FMassStateTreeInstanceFragment>();
			const FMassStateTreeSharedFragment& SharedStateTree = Context.GetConstSharedFragment<FMassStateTreeSharedFragment>();

			const int32 NumEntities = Context.GetNumEntities();
	
			UE::MassBehavior::ForEachEntityInChunk(Context, StateTreeContext, MassStateTreeSubsystem,
				[](FMassStateTreeExecutionContext& StateTreeExecutionContext, FMassStateTreeInstanceFragment& StateTreeInstance, FStateTreeInstanceData& InstanceData)
				{
					// Stop the tree instance
					StateTreeExecutionContext.Stop(&InstanceData);
				});

			// Free the StateTree instance memory
			for (int32 EntityIndex = 0; EntityIndex < NumEntities; EntityIndex++)
			{
				FMassStateTreeInstanceFragment& StateTreeInstance = StateTreeInstanceList[EntityIndex];
				if (StateTreeInstance.InstanceHandle.IsValid())
				{
					MassStateTreeSubsystem.FreeInstanceData(StateTreeInstance.InstanceHandle);
					StateTreeInstance.InstanceHandle = FMassStateTreeInstanceHandle();
				}
			}
		});
}

//----------------------------------------------------------------------//
// UMassStateTreeActivationProcessor
//----------------------------------------------------------------------//
UMassStateTreeActivationProcessor::UMassStateTreeActivationProcessor()
	: EntityQuery(*this)
{
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::LOD);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
	bRequiresGameThreadExecution = true; // due to UMassStateTreeSubsystem RW access
}

void UMassStateTreeActivationProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassStateTreeInstanceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassStateTreeSharedFragment>();
	EntityQuery.AddTagRequirement<FMassStateTreeActivatedTag>(EMassFragmentPresence::None);
	EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddSubsystemRequirement<UMassStateTreeSubsystem>(EMassFragmentAccess::ReadWrite);

	ProcessorRequirements.AddSubsystemRequirement<UMassSignalSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassStateTreeActivationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	UMassSignalSubsystem& SignalSubsystem = Context.GetMutableSubsystemChecked<UMassSignalSubsystem>(EntitySubsystem.GetWorld());

	const UMassBehaviorSettings* BehaviorSettings = GetDefault<UMassBehaviorSettings>();
	check(BehaviorSettings);

	// StateTree processor relies on signals to be ticked but we need an 'initial tick' to set the tree in the proper state.
	// The initializer provides that by sending a signal to all new entities that use StateTree.

	FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, SignalSubsystem, Context);

	const float TimeInSeconds = EntitySubsystem.GetWorld()->GetTimeSeconds();

	TArray<FMassEntityHandle> EntitiesToSignal;
	int32 ActivationCounts[EMassLOD::Max] {0,0,0,0};
	
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context,
		[&EntitiesToSignal, &ActivationCounts, MaxActivationsPerLOD = BehaviorSettings->MaxActivationsPerLOD, &StateTreeContext, TimeInSeconds, World = EntitySubsystem.GetWorld()](FMassExecutionContext& Context)
		{
			UMassStateTreeSubsystem& MassStateTreeSubsystem = Context.GetMutableSubsystemChecked<UMassStateTreeSubsystem>(World);
			const int32 NumEntities = Context.GetNumEntities();
			// Check if we already reached the maximum for this frame
			const EMassLOD::Type ChunkLOD = FMassSimulationVariableTickChunkFragment::GetChunkLOD(Context);
			if (ActivationCounts[ChunkLOD] > MaxActivationsPerLOD[ChunkLOD])
			{
				return;
			}
			ActivationCounts[ChunkLOD] += NumEntities;

			const TArrayView<FMassStateTreeInstanceFragment> StateTreeInstanceList = Context.GetMutableFragmentView<FMassStateTreeInstanceFragment>();
			const FMassStateTreeSharedFragment& SharedStateTree = Context.GetConstSharedFragment<FMassStateTreeSharedFragment>();

			// Allocate and initialize the StateTree instance memory
			for (int32 EntityIndex = 0; EntityIndex < NumEntities; EntityIndex++)
			{
				FMassStateTreeInstanceFragment& StateTreeInstance = StateTreeInstanceList[EntityIndex];
				StateTreeInstance.InstanceHandle = MassStateTreeSubsystem.AllocateInstanceData(SharedStateTree.StateTree);
			}
			
			// Start StateTree. This may do substantial amount of work, as we select and enter the first state.
			UE::MassBehavior::ForEachEntityInChunk(Context, StateTreeContext, MassStateTreeSubsystem,
				[TimeInSeconds](FMassStateTreeExecutionContext& StateTreeExecutionContext, FMassStateTreeInstanceFragment& StateTreeInstance, FStateTreeInstanceData& InstanceData)
				{
					// Start the tree instance
					StateTreeExecutionContext.Start(&InstanceData);
					StateTreeInstance.LastUpdateTimeInSeconds = TimeInSeconds;
				});

			// Adding a tag on each entities to remember we have sent the state tree initialization signal
			EntitiesToSignal.Reserve(EntitiesToSignal.Num() + NumEntities);
			for (int32 EntityIndex = 0; EntityIndex < NumEntities; EntityIndex++)
			{
				const FMassStateTreeInstanceFragment& StateTreeInstance = StateTreeInstanceList[EntityIndex];
				if (StateTreeInstance.InstanceHandle.IsValid())
				{
					const FMassEntityHandle Entity = Context.GetEntity(EntityIndex);
					Context.Defer().AddTag<FMassStateTreeActivatedTag>(Entity);
					EntitiesToSignal.Add(Entity);
				}
			}
		});
	
	// Signal all entities inside the consolidated list
	if (EntitiesToSignal.Num())
	{
		SignalSubsystem.SignalEntities(UE::Mass::Signals::StateTreeActivate, EntitiesToSignal);
	}
}

//----------------------------------------------------------------------//
// UMassStateTreeProcessor
//----------------------------------------------------------------------//
UMassStateTreeProcessor::UMassStateTreeProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRequiresGameThreadExecution = true;

	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Behavior;

	// `Behavior` doesn't run on clients but `Tasks` do.
	// We define the dependencies here so task won't need to set their dependency on `Behavior`,
	// but only on `SyncWorldToMass`
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Tasks);
}

void UMassStateTreeProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());

	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::StateTreeActivate);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::LookAtFinished);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::NewStateTreeTaskRequired);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::StandTaskFinished);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::DelayedTransitionWakeup);

	// @todo MassStateTree: add a way to register/unregister from enter/exit state (need reference counting)
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::SmartObjectRequestCandidates);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::SmartObjectCandidatesReady);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::SmartObjectInteractionDone);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::SmartObjectInteractionAborted);

	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::FollowPointPathStart);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::FollowPointPathDone);
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::CurrentLaneChanged);

	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::AnnotationTagsChanged);

	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::HitReceived);

	// @todo MassStateTree: move this to its game plugin when possible
	SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::ContextualAnimTaskFinished);
}

void UMassStateTreeProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassStateTreeInstanceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassStateTreeSharedFragment>();
	EntityQuery.AddSubsystemRequirement<UMassStateTreeSubsystem>(EMassFragmentAccess::ReadWrite);

	ProcessorRequirements.AddSubsystemRequirement<UMassSignalSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassStateTreeProcessor::SignalEntities(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	UWorld* World = EntitySubsystem.GetWorld();
	UMassSignalSubsystem& SignalSubsystem = Context.GetMutableSubsystemChecked<UMassSignalSubsystem>(World);
	
	QUICK_SCOPE_CYCLE_COUNTER(StateTreeProcessor_Run);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeProcessorExecute);

	const float TimeInSeconds = EntitySubsystem.GetWorld()->GetTimeSeconds();
	FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, SignalSubsystem, Context);

	TArray<FMassEntityHandle> EntitiesToSignal;

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context,
		[this, &StateTreeContext, TimeInSeconds, &EntitiesToSignal, World, &EntitySignals](FMassExecutionContext& Context)
		{
			// Keep stats regarding the amount of tree instances ticked per frame
			CSV_CUSTOM_STAT(StateTreeProcessor, NumTickedStateTree, Context.GetNumEntities(), ECsvCustomStatOp::Accumulate);

			UMassStateTreeSubsystem& MassStateTreeSubsystem = Context.GetMutableSubsystemChecked<UMassStateTreeSubsystem>(World);

			UE::MassBehavior::ForEachEntityInChunk(Context, StateTreeContext, MassStateTreeSubsystem,
				[TimeInSeconds, &EntitiesToSignal, &EntitySignals, &MassStateTreeSubsystem]
				(FMassStateTreeExecutionContext& StateTreeExecutionContext, FMassStateTreeInstanceFragment& StateTreeInstance, FStateTreeInstanceData& InstanceData)
				{
					// Compute adjusted delta time
					const float AdjustedDeltaTime = TimeInSeconds - StateTreeInstance.LastUpdateTimeInSeconds;
					StateTreeInstance.LastUpdateTimeInSeconds = TimeInSeconds;

#if WITH_MASSGAMEPLAY_DEBUG
					const FMassEntityHandle Entity = StateTreeExecutionContext.GetEntity();
					if (UE::Mass::Debug::IsDebuggingEntity(Entity))
					{
						TArray<FName> Signals;
						EntitySignals.GetSignalsForEntity(Entity, Signals);
						FString SignalsString;
						for (const FName& SignalName : Signals)
						{
							if (!SignalsString.IsEmpty())
							{
								SignalsString += TEXT(", ");
							}
							SignalsString += SignalName.ToString();
						}
						UE_VLOG_UELOG(&MassStateTreeSubsystem, LogStateTree, Log, TEXT("%s: Ticking StateTree because of signals: %s"), *Entity.DebugGetDescription(), *SignalsString);
					}
#endif // WITH_MASSGAMEPLAY_DEBUG

					// Tick the tree instance
					StateTreeExecutionContext.Tick(AdjustedDeltaTime, &InstanceData);

					// When last tick status is different than "Running", the state tree need to be tick again
					// For performance reason, tick again to see if we could find a new state right away instead of waiting the next frame.
					if (StateTreeExecutionContext.GetLastTickStatus(&InstanceData) != EStateTreeRunStatus::Running)
					{
						StateTreeExecutionContext.Tick(0.0f, &InstanceData);

						// Could not find new state yet, try again next frame
						if (StateTreeExecutionContext.GetLastTickStatus(&InstanceData) != EStateTreeRunStatus::Running)
						{
							EntitiesToSignal.Add(StateTreeExecutionContext.GetEntity());
						}
					}
				});
		});

	if (EntitiesToSignal.Num())
	{
		SignalSubsystem.SignalEntities(UE::Mass::Signals::NewStateTreeTaskRequired, EntitiesToSignal);
	}
}
