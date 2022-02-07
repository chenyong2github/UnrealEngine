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

void ForEachEntityInChunk(
	FMassStateTreeExecutionContext& StateTreeContext,
	UMassStateTreeSubsystem& MassStateTreeSubsystem,
	const TFunctionRef<void(FMassStateTreeExecutionContext&, FStateTreeDataView)> ForEachEntityCallback)
{
	const FMassExecutionContext& Context = StateTreeContext.GetEntitySubsystemExecutionContext();
	const TConstArrayView<FMassStateTreeFragment> StateTreeList = Context.GetFragmentView<FMassStateTreeFragment>();

	// Assuming that all the entities share same StateTree, because they all should have the same storage fragment.
	const int32 NumEntities = Context.GetNumEntities();
	check(NumEntities > 0);
	UStateTree* StateTree = MassStateTreeSubsystem.GetRegisteredStateTreeAsset(StateTreeList[0].StateTreeHandle);

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

	const UScriptStruct* StorageScriptStruct = StateTree->GetInstanceStorageStruct();
	for (int32 i = 0; i < NumEntities; ++i)
	{
		const FMassEntityHandle Entity = Context.GetEntity(i);
		StateTreeContext.SetEntity(Entity);
		StateTreeContext.SetEntityIndex(i);

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

		ForEachEntityCallback(StateTreeContext, StateTreeContext.GetEntitySubsystem().GetFragmentDataStruct(Entity, StorageScriptStruct));
	}
}

} // UE::MassBehavior


//----------------------------------------------------------------------//
// UMassStateTreeFragmentDestructor
//----------------------------------------------------------------------//
UMassStateTreeFragmentDestructor::UMassStateTreeFragmentDestructor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ObservedType = FMassStateTreeFragment::StaticStruct();
	Operation = EMassObservedOperation::Remove;
}

void UMassStateTreeFragmentDestructor::Initialize(UObject& Owner)
{
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
}

void UMassStateTreeFragmentDestructor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassStateTreeFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassStateTreeFragmentDestructor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (SignalSubsystem == nullptr)
	{
		return;
	}
		
	FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, *SignalSubsystem, Context);
	UMassStateTreeSubsystem* MassStateTreeSubsystem = UWorld::GetSubsystem<UMassStateTreeSubsystem>(EntitySubsystem.GetWorld());

	EntityQuery.ForEachEntityChunk(
		EntitySubsystem,
		Context,
		[this, &StateTreeContext, &MassStateTreeSubsystem](FMassExecutionContext&)
		{
			UE::MassBehavior::ForEachEntityInChunk(
				StateTreeContext,
				*MassStateTreeSubsystem,
				[](FMassStateTreeExecutionContext& StateTreeExecutionContext, FStateTreeDataView Storage)
				{
					// Stop the tree instance
					StateTreeExecutionContext.Stop(Storage);
				});
		});
}

//----------------------------------------------------------------------//
// UMassStateTreeActivationProcessor
//----------------------------------------------------------------------//
UMassStateTreeActivationProcessor::UMassStateTreeActivationProcessor()
{
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::LOD);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
	MaxActivationsPerLOD[EMassLOD::High] = 100;
	MaxActivationsPerLOD[EMassLOD::Medium] = 100;
	MaxActivationsPerLOD[EMassLOD::Low] = 100;
	MaxActivationsPerLOD[EMassLOD::Off] = 100;
}

void UMassStateTreeActivationProcessor::Initialize(UObject& Owner)
{
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
}

void UMassStateTreeActivationProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassStateTreeFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FMassStateTreeActivatedTag>(EMassFragmentPresence::None);
	EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

void UMassStateTreeActivationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (SignalSubsystem == nullptr)
	{
		return;
	}

	// StateTree processor relies on signals to be ticked but we need an 'initial tick' to set the tree in the proper state.
	// The initializer provides that by sending a signal to all new entities that use StateTree.

	FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, *SignalSubsystem, Context);
	UMassStateTreeSubsystem* MassStateTreeSubsystem = UWorld::GetSubsystem<UMassStateTreeSubsystem>(EntitySubsystem.GetWorld());

	TArray<FMassEntityHandle> EntitiesToSignal;
	int32 ActivationCounts[EMassLOD::Max] {0,0,0,0};
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&EntitiesToSignal, &ActivationCounts, MaxActivationsPerLOD = MaxActivationsPerLOD, &StateTreeContext, MassStateTreeSubsystem](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		// Check if we already reached the maximum for this frame
		const EMassLOD::Type ChunkLOD = FMassSimulationVariableTickChunkFragment::GetChunkLOD(Context);
		if (ActivationCounts[ChunkLOD] > MaxActivationsPerLOD[ChunkLOD])
		{
			return;
		}
		ActivationCounts[ChunkLOD] += NumEntities;

		// Start StateTree. This may do substantial amount of work, as we select and enter the first state.
		UE::MassBehavior::ForEachEntityInChunk(
			StateTreeContext,
			*MassStateTreeSubsystem,
			[MassStateTreeSubsystem](FMassStateTreeExecutionContext& StateTreeExecutionContext, FStateTreeDataView Storage)
			{
				// Init object instances
				StateTreeExecutionContext.InitInstanceData(*MassStateTreeSubsystem, Storage);
				// Start the tree instance
				StateTreeExecutionContext.Start(Storage);
			});

		// Append all entities of the current chunk to the consolidated list to send signal once
		EntitiesToSignal.Append(Context.GetEntities().GetData(), Context.GetEntities().Num());
		// Adding a tag on each entities to remember we have sent the state tree initialization signal
		for (int32 i = 0; i < NumEntities; ++i)
		{
			Context.Defer().AddTag<FMassStateTreeActivatedTag>(Context.GetEntity(i));
		}
	});
	// Signal all entities inside the consolidated list
	if (EntitiesToSignal.Num())
	{
		checkf(SignalSubsystem != nullptr, TEXT("Expecting a valid MassSignalSubsystem when activating state trees."));
		SignalSubsystem->SignalEntities(UE::Mass::Signals::StateTreeActivate, EntitiesToSignal);
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
	
	MassStateTreeSubsystem = UWorld::GetSubsystem<UMassStateTreeSubsystem>(Owner.GetWorld());
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());

	SubscribeToSignal(UE::Mass::Signals::StateTreeActivate);
	SubscribeToSignal(UE::Mass::Signals::LookAtFinished);
	SubscribeToSignal(UE::Mass::Signals::NewStateTreeTaskRequired);
	SubscribeToSignal(UE::Mass::Signals::StandTaskFinished);
	SubscribeToSignal(UE::Mass::Signals::DelayedTransitionWakeup);

	// @todo MassStateTree: add a way to register/unregister from enter/exit state (need reference counting)
	SubscribeToSignal(UE::Mass::Signals::SmartObjectRequestCandidates);
	SubscribeToSignal(UE::Mass::Signals::SmartObjectCandidatesReady);
	SubscribeToSignal(UE::Mass::Signals::SmartObjectInteractionDone);

	SubscribeToSignal(UE::Mass::Signals::FollowPointPathStart);
	SubscribeToSignal(UE::Mass::Signals::FollowPointPathDone);
	SubscribeToSignal(UE::Mass::Signals::CurrentLaneChanged);

	SubscribeToSignal(UE::Mass::Signals::AnnotationTagsChanged);

	SubscribeToSignal(UE::Mass::Signals::HitReceived);

	// @todo MassStateTree: move this to its game plugin when possible
	SubscribeToSignal(UE::Mass::Signals::ContextualAnimTaskFinished);
}

void UMassStateTreeProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassStateTreeFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassStateTreeProcessor::SignalEntities(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	if (MassStateTreeSubsystem == nullptr || SignalSubsystem == nullptr)
	{
		return;
	}
	QUICK_SCOPE_CYCLE_COUNTER(StateTreeProcessor_Run);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeProcessorExecute);

	const float TimeDelta = Context.GetDeltaTimeSeconds();
	FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, *SignalSubsystem, Context);
	const float TimeInSeconds = EntitySubsystem.GetWorld()->GetTimeSeconds();

	TArray<FMassEntityHandle> EntitiesToSignal;

	EntityQuery.ForEachEntityChunk(
		EntitySubsystem,
		Context,
		[this, &StateTreeContext, TimeDelta, TimeInSeconds, &EntitiesToSignal](FMassExecutionContext& Context)
		{
			// Keep stats regarding the amount of tree instances ticked per frame
			CSV_CUSTOM_STAT(StateTreeProcessor, NumTickedStateTree, Context.GetNumEntities(), ECsvCustomStatOp::Accumulate);

			TArrayView<FMassStateTreeFragment> StateTreeList = Context.GetMutableFragmentView<FMassStateTreeFragment>();

			UE::MassBehavior::ForEachEntityInChunk(
				StateTreeContext,
				*MassStateTreeSubsystem,
				[&StateTreeList, TimeDelta, TimeInSeconds, &EntitiesToSignal](FMassStateTreeExecutionContext& StateTreeExecutionContext, const FStateTreeDataView Storage)
				{
					// Compute adjusted delta time
					TOptional<float>& LastUpdate = StateTreeList[StateTreeExecutionContext.GetEntityIndex()].LastUpdateTimeInSeconds;
					const float AdjustedTimeDelta = LastUpdate.IsSet() ? TimeDelta + (TimeInSeconds - LastUpdate.GetValue()) : TimeDelta;
					LastUpdate = TimeInSeconds;

					// Tick the tree instance
					StateTreeExecutionContext.Tick(AdjustedTimeDelta, Storage);

					// When last tick status is different than "Running", the state tree need to be tick again
					// For performance reason, tick again to see if we could find a new state right away instead of waiting the next frame.
					if (StateTreeExecutionContext.GetLastTickStatus(Storage) != EStateTreeRunStatus::Running)
					{
						StateTreeExecutionContext.Tick(0.0f, Storage);

						// Could not find new state yet, try again next frame
						if (StateTreeExecutionContext.GetLastTickStatus(Storage) != EStateTreeRunStatus::Running)
						{
							EntitiesToSignal.Add(StateTreeExecutionContext.GetEntity());
						}
					}
				});
		});

	if (EntitiesToSignal.Num())
	{
		SignalSubsystem->SignalEntities(UE::Mass::Signals::NewStateTreeTaskRequired, EntitiesToSignal);
	}
}
