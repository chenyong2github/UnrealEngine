// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassReplicationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassSpawnerTypes.h"
#include "MassSimulationLOD.h"
#include "MassReplicationTypes.h"
#include "MassReplicationSubsystem.h"


void UMassReplicationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	if (World.IsNetMode(NM_Standalone))
	{
		return;
	}

	FReplicationTemplateIDFragment& TemplateIDFragment = BuildContext.AddFragment_GetRef<FReplicationTemplateIDFragment>();
	TemplateIDFragment.ID = BuildContext.GetTemplateID();

	BuildContext.AddFragment<FMassNetworkIDFragment>();
	BuildContext.AddFragment<FMassReplicatedAgentFragment>();
	BuildContext.AddFragment<FMassReplicationViewerInfoFragment>();
	BuildContext.AddFragment<FMassReplicationLODFragment>();

	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	UMassReplicationSubsystem* ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(&World);
	check(ReplicationSubsystem);

	uint32 ParamsHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Params));
	FConstSharedStruct ParamsFragment = EntitySubsystem->GetOrCreateConstSharedFragment(ParamsHash, Params);
	BuildContext.AddConstSharedFragment(ParamsFragment);
	FSharedStruct SharedFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassReplicationSharedFragment>(ParamsHash, *ReplicationSubsystem, Params);
	BuildContext.AddSharedFragment(SharedFragment);
}