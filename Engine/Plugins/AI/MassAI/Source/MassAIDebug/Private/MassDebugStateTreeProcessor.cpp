// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassDebugStateTreeProcessor.h"
#include "MassDebuggerSubsystem.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeExecutionContext.h"
#include "MassStateTreeFragments.h"
#include "MassCommonFragments.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
// UMassDebugStateTreeProcessor
//----------------------------------------------------------------------//
UMassDebugStateTreeProcessor::UMassDebugStateTreeProcessor()
{
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Behavior;
	ExecutionOrder.ExecuteAfter.Add(TEXT("MassStateTreeProcessor"));
}

void UMassDebugStateTreeProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassStateTreeFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassDebugStateTreeProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
#if WITH_MASSGAMEPLAY_DEBUG
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	
	UMassDebuggerSubsystem* Debugger = World->GetSubsystem<UMassDebuggerSubsystem>();
	if (Debugger == nullptr)
	{
		return;
	}

	UMassStateTreeSubsystem* MassStateTreeSubsystem = World->GetSubsystem<UMassStateTreeSubsystem>();
	if (MassStateTreeSubsystem == nullptr)
	{
		return;
	}

	UMassSignalSubsystem* MassSignalSubsystem = World->GetSubsystem<UMassSignalSubsystem>();
	if (MassSignalSubsystem == nullptr)
	{
		return;
	}

	if (!Debugger->GetSelectedEntity().IsSet() && !UE::Mass::Debug::HasDebugEntities())
	{
		return;
	}
	
	QUICK_SCOPE_CYCLE_COUNTER(UMassDebugStateTreeProcessor_Run);	
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, Debugger, MassStateTreeSubsystem, &EntitySubsystem, MassSignalSubsystem](FMassExecutionContext& Context)
		{
			const FMassEntityHandle SelectedEntity = Debugger->GetSelectedEntity();
			const int32 NumEntities = Context.GetNumEntities();
			const TConstArrayView<FMassStateTreeFragment> StateTreeList = Context.GetFragmentView<FMassStateTreeFragment>();
			const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
			const UStateTree* StateTree = MassStateTreeSubsystem->GetRegisteredStateTreeAsset(StateTreeList[0].StateTreeHandle);

			// Not reporting error since this processor is a debug tool 
			if (StateTree == nullptr)
			{
				return;
			}
		
			for (int32 i = 0; i < NumEntities; ++i)
			{
				FMassEntityHandle Entity = Context.GetEntity(i);
				if (Entity == SelectedEntity)
				{
					FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, *MassSignalSubsystem, Context);
					StateTreeContext.Init(*this, *StateTree, EStateTreeStorage::External);
					StateTreeContext.SetEntity(Entity);
					
#if WITH_GAMEPLAY_DEBUGGER
					const FStructView Storage = EntitySubsystem.GetFragmentDataStruct(Entity, StateTree->GetInstanceStorageStruct());
					Debugger->AppendSelectedEntityInfo(StateTreeContext.GetDebugInfoString(Storage));
#endif // WITH_GAMEPLAY_DEBUGGER
				}
					
				FColor EntityColor = FColor::White;
				const bool bDisplayDebug = UE::Mass::Debug::IsDebuggingEntity(Entity, &EntityColor);
				if (bDisplayDebug)
				{
					const FTransformFragment& Transform = TransformList[i];
					
					const FVector ZOffset(0,0,50);
					const FVector Position = Transform.GetTransform().GetLocation() + ZOffset;

					FMassStateTreeExecutionContext StateTreeContext(EntitySubsystem, *MassSignalSubsystem, Context);
					StateTreeContext.Init(*this, *StateTree, EStateTreeStorage::External);
					StateTreeContext.SetEntity(Entity);

					const FStructView Storage = EntitySubsystem.GetFragmentDataStruct(Entity, StateTree->GetInstanceStorageStruct());

					// State
					UE_VLOG_SEGMENT_THICK(this, LogStateTree, Log, Position, Position + FVector(0,0,50), EntityColor, /*Thickness*/ 2, TEXT("%s %s"),
						*Entity.DebugGetDescription(), *StateTreeContext.GetActiveStateName(Storage));
				}
			}
		});
	#endif // WITH_MASSGAMEPLAY_DEBUG
}
