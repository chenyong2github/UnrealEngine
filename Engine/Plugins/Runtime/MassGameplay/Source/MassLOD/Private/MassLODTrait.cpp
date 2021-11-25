// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "StructUtilsTypes.h"

void UMassSimulationLODTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_Transform>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassLODInfoFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassSimulationLODFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_MassSimulationLODInfo>();
	BuildContext.AddTag<FMassOffLODTag>();
	BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);

	uint32 ConfigHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Config));
	FConstSharedStruct ConfigFragment = EntitySubsystem->GetOrCreateConstSharedFragment(ConfigHash, Config);
	BuildContext.AddConstSharedFragment(ConfigFragment);
	FSharedStruct SharedFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassSimulationLODSharedFragment>(ConfigHash, Config);
	BuildContext.AddSharedFragment(SharedFragment);
}
