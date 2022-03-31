// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "StructUtilsTypes.h"
#include "MassLODFragments.h"

void UMassLODCollectorTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FMassViewerInfoFragment>();
	BuildContext.AddTag<FMassCollectLODViewerInfoTag>();
	BuildContext.RequireFragment<FTransformFragment>();
}

void UMassSimulationLODTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.RequireFragment<FMassViewerInfoFragment>();
	BuildContext.RequireFragment<FTransformFragment>();

	FMassSimulationLODFragment& LODFragment = BuildContext.AddFragment_GetRef<FMassSimulationLODFragment>();

	// Start all simulation LOD in the Off 
	if(Params.bSetLODTags || bEnableVariableTicking)
	{
		LODFragment.LOD = EMassLOD::Off;
		BuildContext.AddTag<FMassOffLODTag>();
	}

	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	uint32 ParamsHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Params));
	FConstSharedStruct ParamsFragment = EntitySubsystem->GetOrCreateConstSharedFragment(ParamsHash, Params);
	BuildContext.AddConstSharedFragment(ParamsFragment);
	FSharedStruct SharedFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassSimulationLODSharedFragment>(ParamsHash, Params);
	BuildContext.AddSharedFragment(SharedFragment);

	// Variable ticking from simulation LOD
	if(bEnableVariableTicking)
	{
		BuildContext.AddFragment<FMassSimulationVariableTickFragment>();
		BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

		uint32 VariableTickParamsHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(VariableTickParams));
		FConstSharedStruct VariableTickParamsFragment = EntitySubsystem->GetOrCreateConstSharedFragment(VariableTickParamsHash, VariableTickParams);
		BuildContext.AddConstSharedFragment(VariableTickParamsFragment);
		FSharedStruct VariableTickSharedFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassSimulationVariableTickSharedFragment>(VariableTickParamsHash, VariableTickParams);
		BuildContext.AddSharedFragment(VariableTickSharedFragment);
	}
}
