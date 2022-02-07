// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugVisLocationProcessor.h"
#include "MassDebuggerSubsystem.h"
#include "MassDebugVisualizationComponent.h"
#include "MassCommonFragments.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "MassEntitySubsystem.h"
#include "MassMovementFragments.h"

//----------------------------------------------------------------------//
// UDebugVisLocationProcessor
//----------------------------------------------------------------------//
UDebugVisLocationProcessor::UDebugVisLocationProcessor()
{
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
}

void UDebugVisLocationProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FSimDebugVisFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FMassDebuggableTag>(EMassFragmentPresence::All);
}

void UDebugVisLocationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
#if WITH_EDITORONLY_DATA
	UMassDebugVisualizationComponent* Visualizer = WeakVisualizer.Get();
	if (!ensure(Visualizer))
	{
		return;
	}

	QUICK_SCOPE_CYCLE_COUNTER(DebugVisLocationProcessor_Run);

	TArrayView<UHierarchicalInstancedStaticMeshComponent*> VisualDataISMCs = Visualizer->GetVisualDataISMCs();
	if (VisualDataISMCs.Num() > 0)
	{
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, &VisualDataISMCs](const FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();
			const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FSimDebugVisFragment> DebugVisList = Context.GetFragmentView<FSimDebugVisFragment>();

			for (int32 i = 0; i < NumEntities; ++i)
			{
				const FSimDebugVisFragment& VisualComp = DebugVisList[i];

				// @todo: remove this code once the asset is exported with correct alignment SM_Mannequin.uasset
				FTransform SMTransform = LocationList[i].GetTransform();
				FQuat FromEngineToSM(FVector::UpVector, -HALF_PI);
				SMTransform.SetRotation(FromEngineToSM * SMTransform.GetRotation());

				VisualDataISMCs[VisualComp.VisualType]->UpdateInstanceTransform(VisualComp.InstanceIndex, SMTransform, true);
			}
		});

		Visualizer->DirtyVisuals();
	}
	else 
	{
		UE_LOG(LogMassDebug, Log, TEXT("UDebugVisLocationProcessor: Trying to update InstanceStaticMeshes while none created. Check your debug visualization setup"));
	}
#endif // WITH_EDITORONLY_DATA
}

void UDebugVisLocationProcessor::Initialize(UObject& InOwner)
{
	Super::Initialize(InOwner);

#if WITH_EDITORONLY_DATA
	UMassDebuggerSubsystem* Debugger = UWorld::GetSubsystem<UMassDebuggerSubsystem>(InOwner.GetWorld());
	if (ensure(Debugger))
	{
		WeakVisualizer = Debugger->GetVisualizationComponent();
		ensure(WeakVisualizer.Get());
	}
#endif // WITH_EDITORONLY_DATA
}

//----------------------------------------------------------------------//
//  UMassProcessor_UpdateDebugVis
//----------------------------------------------------------------------//
UMassProcessor_UpdateDebugVis::UMassProcessor_UpdateDebugVis()
{
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::UpdateWorldFromMass);
}

void UMassProcessor_UpdateDebugVis::ConfigureQueries() 
{
	// @todo only FDataFragment_DebugVis should be mandatory, rest optional 
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_DebugVis>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassDebuggableTag>(EMassFragmentPresence::All);
}

void UMassProcessor_UpdateDebugVis::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	UMassDebuggerSubsystem* Debugger = UWorld::GetSubsystem<UMassDebuggerSubsystem>(GetWorld());
	if (Debugger == nullptr)
	{
		return;
	}

	QUICK_SCOPE_CYCLE_COUNTER(UMassProcessor_UpdateDebugVis_Run);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, Debugger](FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();
			const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
			const TArrayView<FDataFragment_DebugVis> DebugVisList = Context.GetMutableFragmentView<FDataFragment_DebugVis>();
			const TArrayView<FAgentRadiusFragment> RadiiList = Context.GetMutableFragmentView<FAgentRadiusFragment>();

			for (int32 i = 0; i < NumEntities; ++i)
			{
				Debugger->AddShape(DebugVisList[i].Shape, LocationList[i].GetTransform().GetLocation(), RadiiList[i].Radius);
				Debugger->AddEntityLocation(Context.GetEntity(i), LocationList[i].GetTransform().GetLocation());
			}
		});
}
