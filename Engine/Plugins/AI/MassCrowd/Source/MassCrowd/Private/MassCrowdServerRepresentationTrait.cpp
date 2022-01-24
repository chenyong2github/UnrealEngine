// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdServerRepresentationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCrowdRepresentationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "Engine/World.h"
#include "MassCrowdRepresentationActorManagement.h"

void UMassCrowdServerRepresentationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	// This should only be ran on NM_DedicatedServer network mode
	if (!World.IsNetMode(NM_DedicatedServer))
	{
		return;
	}
	
	// the following needs to be always there for mesh vis to work. Adding following fragments after already 
	// adding Config.AdditionalDataFragments to let user configure the fragments first. Calling BuildContext.Add() 
	// won't override any fragments that are already there
	BuildContext.AddFragment<FDataFragment_Transform>();

	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	UMassCrowdRepresentationSubsystem* RepresentationSubsystem = World.GetSubsystem<UMassCrowdRepresentationSubsystem>();
	check(RepresentationSubsystem);

	FMassRepresentationSubsystemFragment Subsystem;
	Subsystem.RepresentationSubsystem = RepresentationSubsystem;
	uint32 SubsystemHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Subsystem));
	FSharedStruct SubsystemFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassRepresentationSubsystemFragment>(SubsystemHash, Subsystem);
	BuildContext.AddSharedFragment(SubsystemFragment);

	FMassRepresentationConfig Config;
	Config.RepresentationActorManagement = UMassCrowdRepresentationActorManagement::StaticClass()->GetDefaultObject<UMassCrowdRepresentationActorManagement>();
	uint32 ConfigHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Config));
	FConstSharedStruct ConfigFragment = EntitySubsystem->GetOrCreateConstSharedFragment<FMassRepresentationConfig>(ConfigHash, Config);
	BuildContext.AddConstSharedFragment(ConfigFragment);

	FMassRepresentationFragment& RepresentationFragment = BuildContext.AddFragment_GetRef<FMassRepresentationFragment>();
	RepresentationFragment.StaticMeshDescIndex = INDEX_NONE;
	RepresentationFragment.HighResTemplateActorIndex = TemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(TemplateActor.Get()) : INDEX_NONE;
	RepresentationFragment.LowResTemplateActorIndex = INDEX_NONE;

	BuildContext.AddFragment<FMassRepresentationLODFragment>();

	// @todo figure out if this chunk fragment is really needed?
	BuildContext.AddChunkFragment<FMassVisualizationChunkFragment>();
}