// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdVisualizationProcessor.h"
#include "MassCrowdFragments.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MassActorSubsystem.h"
#include "MassCrowdRepresentationSubsystem.h"

namespace UE::MassCrowd
{
	int32 bDebugCrowdVisualType = 0;
	FAutoConsoleVariableRef CVarDebugVisualType(TEXT("ai.debug.CrowdVisualType"), bDebugCrowdVisualType, TEXT("Debug Crowd Visual Type"), ECVF_Cheat);

	FColor CrowdRepresentationTypesColors[] =
	{
		FColor::Red,
		FColor::Yellow,
		FColor::Emerald,
		FColor::White,
	};
} // UE::MassCrowd

//----------------------------------------------------------------------//
// UMassCrowdVisualizationProcessor
//----------------------------------------------------------------------//
UMassCrowdVisualizationProcessor::UMassCrowdVisualizationProcessor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::LOD);

	bRequiresGameThreadExecution = true;
}

void UMassCrowdVisualizationProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();
}

void UMassCrowdVisualizationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		UpdateVisualization(Context);
	});

	if (UE::MassCrowd::bDebugCrowdVisualType)
	{
		check(World);

		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayVisualType"))

		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](const FMassExecutionContext& Context)
		{
			const TConstArrayView<FMassRepresentationFragment> VisualizationList = Context.GetFragmentView<FMassRepresentationFragment>();
			const TConstArrayView<FDataFragment_Actor> ActorList = Context.GetFragmentView<FDataFragment_Actor>();
			const TConstArrayView<FDataFragment_Transform> EntityLocationList = Context.GetFragmentView<FDataFragment_Transform>();

			const int32 NumEntities = Context.GetNumEntities();
			for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
			{
				const FDataFragment_Transform& EntityLocation = EntityLocationList[EntityIdx];
				const FMassRepresentationFragment& Visualization = VisualizationList[EntityIdx];
				const FDataFragment_Actor& ActorInfo = ActorList[EntityIdx];
				const int32 RepresentationTypeIdx = (int32)Visualization.CurrentRepresentation;
				// Show replicated actors
				if (ActorInfo.IsValid() && !ActorInfo.IsOwnedByMass())
				{
					DrawDebugBox(World, EntityLocation.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 120.0f), FVector(25.0f), UE::MassCrowd::CrowdRepresentationTypesColors[RepresentationTypeIdx]);
				}
				else
				{ 
					DrawDebugSolidBox(World, EntityLocation.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 120.0f), FVector(25.0f), UE::MassCrowd::CrowdRepresentationTypesColors[RepresentationTypeIdx]);
				}
			}
		});
	}
}