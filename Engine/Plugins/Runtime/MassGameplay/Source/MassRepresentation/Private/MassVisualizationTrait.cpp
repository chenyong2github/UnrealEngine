// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassVisualizationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassRepresentationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationActorManagement.h"
#include "Engine/World.h"
#include "MassLODFragments.h"

UMassVisualizationTrait::UMassVisualizationTrait()
{
	RepresentationSubsystemClass = UMassRepresentationSubsystem::StaticClass();

	Config.RepresentationActorManagementClass = UMassRepresentationActorManagement::StaticClass();
	Config.LODRepresentation[EMassLOD::High] = EMassRepresentationType::HighResSpawnedActor;
	Config.LODRepresentation[EMassLOD::Medium] = EMassRepresentationType::LowResSpawnedActor;
	Config.LODRepresentation[EMassLOD::Low] = EMassRepresentationType::StaticMeshInstance;
	Config.LODRepresentation[EMassLOD::Off] = EMassRepresentationType::None;
}

void UMassVisualizationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	// This should not be ran on NM_Server network mode
	if (World.IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	// the following needs to be always there for mesh vis to work. Adding following fragments after already 
	// adding Config.AdditionalDataFragments to let user configure the fragments first. Calling BuildContext.Add() 
	// won't override any fragments that are already there
	BuildContext.AddTag<FMassCollectLODViewerInfoTag>(); // Depends on FMassViewerInfoFragment
	BuildContext.AddFragment<FDataFragment_Transform>();

	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	UMassRepresentationSubsystem* RepresentationSubsystem = Cast<UMassRepresentationSubsystem>(World.GetSubsystemBase(RepresentationSubsystemClass));
	if (RepresentationSubsystem == nullptr)
	{
		UE_LOG(LogMassRepresentation, Error, TEXT("Expecting a valid class for the representation subsystem"));
		RepresentationSubsystem = UWorld::GetSubsystem<UMassRepresentationSubsystem>(&World);
		check(RepresentationSubsystem);
	}

	FMassRepresentationSubsystemFragment Subsystem;
	Subsystem.RepresentationSubsystem = RepresentationSubsystem;
	uint32 SubsystemHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Subsystem));
	FSharedStruct SubsystemFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassRepresentationSubsystemFragment>(SubsystemHash, Subsystem);
	BuildContext.AddSharedFragment(SubsystemFragment);

	if (!Config.RepresentationActorManagementClass)
	{
		UE_LOG(LogMassRepresentation, Error, TEXT("Expecting a valid class for the representation actor management"));
	}
	uint32 ConfigHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Config));
	FConstSharedStruct ConfigFragment = EntitySubsystem->GetOrCreateConstSharedFragment<FMassRepresentationConfig>(ConfigHash, Config);
	ConfigFragment.Get<FMassRepresentationConfig>().ComputeCachedValues();
	BuildContext.AddConstSharedFragment(ConfigFragment);

	FMassRepresentationFragment& RepresentationFragment = BuildContext.AddFragment_GetRef<FMassRepresentationFragment>();
	RepresentationFragment.StaticMeshDescIndex = RepresentationSubsystem->FindOrAddStaticMeshDesc(StaticMeshInstanceDesc);
	RepresentationFragment.HighResTemplateActorIndex = HighResTemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(HighResTemplateActor.Get()) : INDEX_NONE;
	RepresentationFragment.LowResTemplateActorIndex = LowResTemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(LowResTemplateActor.Get()) : INDEX_NONE;

	BuildContext.AddFragment<FMassRepresentationLODFragment>();
	BuildContext.AddTag<FMassVisibilityCulledByDistanceTag>();
	BuildContext.AddChunkFragment<FMassVisualizationChunkFragment>();
}


