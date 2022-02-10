// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdVisualizationLODProcessor.h"
#include "MassCommonFragments.h"
#include "MassCrowdFragments.h"

namespace UE::MassCrowd
{

	MASSCROWD_API int32 GCrowdTurnOffVisualization = 0;
	FAutoConsoleVariableRef CVarCrowdTurnOffVisualization(TEXT("Mass.CrowdTurnOffVisualization"), GCrowdTurnOffVisualization, TEXT("Turn off crowd visualization"));

	int32 bDebugCrowdVisualizationLOD = 0;
	int32 bDebugShowISMUnderSpecifiedRange = 0;

	FAutoConsoleVariableRef ConsoleVariables[] =
	{
		FAutoConsoleVariableRef(TEXT("ai.debug.CrowdVisualizationLOD"), bDebugCrowdVisualizationLOD, TEXT("Debug crowd visualization LOD"), ECVF_Cheat),
		FAutoConsoleVariableRef(TEXT("ai.debug.ShowISMUnderSpecifiedRange"), bDebugShowISMUnderSpecifiedRange, TEXT("Show ISM under a specified range (meters)"), ECVF_Cheat)
	};

// #if WITH_EDITOR
// 	int32 DebugCrowdMaxCountHigh = -1;
// 	int32 DebugCrowdMaxCountMedium = -1;
// 	int32 DebugCrowdMaxCountLow = -1;
// 
// 	FAutoConsoleVariableRef EditorConsoleVariables[] =
// 	{
// 		FAutoConsoleVariableRef(TEXT("ai.debug.CrowdMaxCountHigh"), DebugCrowdMaxCountHigh, TEXT("Forced MASS crowd max count high"), ECVF_Cheat),
// 		FAutoConsoleVariableRef(TEXT("ai.debug.CrowdMaxCountMedium"), DebugCrowdMaxCountMedium, TEXT("Forced MASS crowd max count medium"), ECVF_Cheat),
// 		FAutoConsoleVariableRef(TEXT("ai.debug.CrowdMaxCountLow"), DebugCrowdMaxCountLow, TEXT("Forced MASS crowd max count low"), ECVF_Cheat)
// 	};
// 
// 	int32 LastDebugCrowdMaxCountHigh = -1;
// 	int32 LastDebugCrowdMaxCountMedium = -1;
// 	int32 LastDebugCrowdMaxCountLow = -1;
// #endif // WITH_EDITOR
} // UE::MassCrowd

UMassCrowdVisualizationLODProcessor::UMassCrowdVisualizationLODProcessor()
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);

	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::LOD;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::LODCollector);

// #if WITH_EDITOR
// 	UE::MassCrowd::LastDebugCrowdMaxCountHigh = -1;
// 	UE::MassCrowd::LastDebugCrowdMaxCountMedium = -1;
// 	UE::MassCrowd::LastDebugCrowdMaxCountLow = -1;
// #endif // WITH_EDITOR
}

void UMassCrowdVisualizationLODProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();

	CloseEntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	CloseEntityAdjustDistanceQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	FarEntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	DebugEntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
}

void UMassCrowdVisualizationLODProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	ForceOffLOD((bool)UE::MassCrowd::GCrowdTurnOffVisualization);

	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("CrowdVisualizationLOD"))

	Super::Execute(EntitySubsystem, Context);
	
	if (UE::MassCrowd::bDebugCrowdVisualizationLOD)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayLOD"))

		DebugEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			FMassVisualizationLODSharedFragment& LODSharedFragment = Context.GetMutableSharedFragment<FMassVisualizationLODSharedFragment>();
			const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FMassRepresentationLODFragment> VisualizationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();
			LODSharedFragment.LODCalculator.DebugDisplayLOD(Context, VisualizationLODList, LocationList, World);
		});
	}

	if (UE::MassCrowd::bDebugShowISMUnderSpecifiedRange > 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("ShowISMUnderSpecifiedRange"))

		DebugEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](const FMassExecutionContext& Context)
		{
			const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FMassRepresentationFragment> RepresentationFragmentList = Context.GetFragmentView<FMassRepresentationFragment>();
			const TConstArrayView<FMassViewerInfoFragment> LODInfoFragmentList = Context.GetFragmentView<FMassViewerInfoFragment>();
			const int32 NumEntities = Context.GetNumEntities();
			const float SpecifiedRangeSquaredCentimeters = FMath::Square(UE::MassCrowd::bDebugShowISMUnderSpecifiedRange * 100);
			for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
			{
				const FMassRepresentationFragment& RepresentationFragment = RepresentationFragmentList[EntityIdx];
				const FMassViewerInfoFragment& LODInfoFragment = LODInfoFragmentList[EntityIdx];
				if (RepresentationFragment.CurrentRepresentation == EMassRepresentationType::StaticMeshInstance && SpecifiedRangeSquaredCentimeters > LODInfoFragment.ClosestViewerDistanceSq)
				{
					const FTransformFragment& EntityLocation = LocationList[EntityIdx];
					DrawDebugSolidBox(World, EntityLocation.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 150.0f), FVector(50.0f), FColor::Red);
				}
			}
		});
	}

// @todo find a way to do this now with shared fragments
// #if WITH_EDITOR
// 	if (UE::MassCrowd::LastDebugCrowdMaxCountHigh != UE::MassCrowd::DebugCrowdMaxCountHigh)
// 	{
// 		if (UE::MassCrowd::DebugCrowdMaxCountHigh >= 0)
// 		{
// 			LODMaxCount[0] = UE::MassCrowd::DebugCrowdMaxCountHigh;
// 		}
// 		else
// 		{
// 			LODMaxCount[0] = GetClass()->GetDefaultObject<UMassCrowdVisualizationLODProcessor>()->LODMaxCount[0];
// 		}
// 		UE::MassCrowd::LastDebugCrowdMaxCountHigh = UE::MassCrowd::DebugCrowdMaxCountHigh;
// 		LODCalculator.Initialize(BaseLODDistance, BufferHysteresisOnDistancePercentage / 100.f, LODMaxCount, nullptr, DistanceToFrustum, DistanceToFrustumHysteresis, VisibleLODDistance);
// 	}
// 	if (UE::MassCrowd::LastDebugCrowdMaxCountMedium != UE::MassCrowd::DebugCrowdMaxCountMedium)
// 	{
// 		if (UE::MassCrowd::DebugCrowdMaxCountMedium >= 0)
// 		{
// 			LODMaxCount[1] = UE::MassCrowd::DebugCrowdMaxCountMedium;
// 		}
// 		else
// 		{
// 			LODMaxCount[1] = GetClass()->GetDefaultObject<UMassCrowdVisualizationLODProcessor>()->LODMaxCount[1];
// 		}
// 		UE::MassCrowd::LastDebugCrowdMaxCountMedium = UE::MassCrowd::DebugCrowdMaxCountMedium;
// 		LODCalculator.Initialize(BaseLODDistance, BufferHysteresisOnDistancePercentage / 100.f, LODMaxCount, nullptr, DistanceToFrustum, DistanceToFrustumHysteresis, VisibleLODDistance);
// 	}
// 	if (UE::MassCrowd::LastDebugCrowdMaxCountLow != UE::MassCrowd::DebugCrowdMaxCountLow)
// 	{
// 		if (UE::MassCrowd::DebugCrowdMaxCountLow >= 0)
// 		{
// 			LODMaxCount[2] = UE::MassCrowd::DebugCrowdMaxCountLow;
// 		}
// 		else
// 		{
// 			LODMaxCount[2] = GetClass()->GetDefaultObject<UMassCrowdVisualizationLODProcessor>()->LODMaxCount[2];
// 		}
// 		UE::MassCrowd::LastDebugCrowdMaxCountLow = UE::MassCrowd::DebugCrowdMaxCountLow;
// 		LODCalculator.Initialize(BaseLODDistance, BufferHysteresisOnDistancePercentage / 100.f, LODMaxCount, nullptr, DistanceToFrustum, DistanceToFrustumHysteresis, VisibleLODDistance);
// 	}
// #endif // WITH_EDITOR
}
