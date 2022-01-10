// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "StructUtilsTypes.h"
#include "MassLODTypes.h"

void UMassLODCollectorTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragment<FMassViewerInfoFragment>();
	BuildContext.AddTag<FMassCollectLODViewerInfoTag>();
}

void UMassSimulationLODTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddTag<FMassCollectLODViewerInfoTag>(); // Depends on FMassViewerInfoFragment
	BuildContext.AddFragment<FDataFragment_Transform>();

	FMassSimulationLODFragment& LODFragment = BuildContext.AddFragment_GetRef<FMassSimulationLODFragment>();

	// Start all simulation LOD in the Off 
	if(Config.bSetLODTags || bEnableVariableTicking)
	{
		LODFragment.LOD = EMassLOD::Off;
		BuildContext.AddTag<FMassOffLODTag>();
	}

	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);

	uint32 ConfigHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Config));
	FConstSharedStruct ConfigFragment = EntitySubsystem->GetOrCreateConstSharedFragment(ConfigHash, Config);
	BuildContext.AddConstSharedFragment(ConfigFragment);
	FSharedStruct SharedFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassSimulationLODSharedFragment>(ConfigHash, Config);
	BuildContext.AddSharedFragment(SharedFragment);

	// Variable ticking from simulation LOD
	if(bEnableVariableTicking)
	{
		BuildContext.AddFragment<FMassSimulationVariableTickFragment>();
		BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

		uint32 VariableTickConfigHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(VariableTickConfig));
		FConstSharedStruct VariableTickConfigFragment = EntitySubsystem->GetOrCreateConstSharedFragment(VariableTickConfigHash, VariableTickConfig);
		BuildContext.AddConstSharedFragment(VariableTickConfigFragment);
		FSharedStruct VariableTickSharedFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassSimulationVariableTickSharedFragment>(VariableTickConfigHash, VariableTickConfig);
		BuildContext.AddSharedFragment(VariableTickSharedFragment);
	}
}
