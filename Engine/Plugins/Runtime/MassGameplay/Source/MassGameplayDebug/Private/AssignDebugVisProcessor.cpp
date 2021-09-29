// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssignDebugVisProcessor.h"
#include "MassGameplayDebugTypes.h"
#include "MassDebugVisualizationComponent.h"
#include "MassDebuggerSubsystem.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
// UAssignDebugVisProcessor
//----------------------------------------------------------------------//
UAssignDebugVisProcessor::UAssignDebugVisProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
	FragmentType = FSimDebugVisComponent::StaticStruct();
}

void UAssignDebugVisProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FSimDebugVisComponent>(ELWComponentAccess::ReadWrite);
}

void UAssignDebugVisProcessor::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	UMassDebugVisualizationComponent* Visualizer = WeakVisualizer.Get();
	if (!ensure(Visualizer))
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	QUICK_SCOPE_CYCLE_COUNTER(AssignDebugVisProcessor_Execute);
	
	// note that this function will create the "visual components" only it they're missing or out of sync. 
	Visualizer->ConditionallyConstructVisualComponent();

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, Visualizer](FLWComponentSystemExecutionContext& Context)
	{
		const TArrayView<FSimDebugVisComponent> DebugVisList = Context.GetMutableComponentView<FSimDebugVisComponent>();
		for (FSimDebugVisComponent& VisualComp : DebugVisList)
		{
			// VisualComp.VisualType needs to be assigned by now. Should be performed as part of spawning, copied from the AgentTemplate
			if (ensure(VisualComp.VisualType != INDEX_NONE))
			{
				VisualComp.InstanceIndex = Visualizer->AddDebugVisInstance(VisualComp.VisualType);
			}
		}
	});
	Visualizer->DirtyVisuals();
#endif // WITH_EDITORONLY_DATA
}

void UAssignDebugVisProcessor::Initialize(UObject& InOwner)
{
	Super::Initialize(InOwner);
	
	UMassDebuggerSubsystem* Debugger = UWorld::GetSubsystem<UMassDebuggerSubsystem>(InOwner.GetWorld());
	if (ensure(Debugger))
	{
		WeakVisualizer = Debugger->GetVisualizationComponent();	
		ensure(WeakVisualizer.Get());
	}
}
