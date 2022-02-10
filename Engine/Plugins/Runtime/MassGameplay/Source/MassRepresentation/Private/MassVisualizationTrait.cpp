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

	Params.RepresentationActorManagementClass = UMassRepresentationActorManagement::StaticClass();
	Params.LODRepresentation[EMassLOD::High] = EMassRepresentationType::HighResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Medium] = EMassRepresentationType::LowResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Low] = EMassRepresentationType::StaticMeshInstance;
	Params.LODRepresentation[EMassLOD::Off] = EMassRepresentationType::None;

	LODParams.BaseLODDistance[EMassLOD::High] = 0.f;
	LODParams.BaseLODDistance[EMassLOD::Medium] = 1000.f;
	LODParams.BaseLODDistance[EMassLOD::Low] = 2500.f;
	LODParams.BaseLODDistance[EMassLOD::Off] = 10000.f;

	LODParams.VisibleLODDistance[EMassLOD::High] = 0.f;
	LODParams.VisibleLODDistance[EMassLOD::Medium] = 2000.f;
	LODParams.VisibleLODDistance[EMassLOD::Low] = 4000.f;
	LODParams.VisibleLODDistance[EMassLOD::Off] = 10000.f;

	LODParams.LODMaxCount[EMassLOD::High] = 50;
	LODParams.LODMaxCount[EMassLOD::Medium] = 100;
	LODParams.LODMaxCount[EMassLOD::Low] = 500;
	LODParams.LODMaxCount[EMassLOD::Off] = 0;

	LODParams.BufferHysteresisOnDistancePercentage = 10.0f;
	LODParams.DistanceToFrustum = 0.0f;
	LODParams.DistanceToFrustumHysteresis = 0.0f;
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
	BuildContext.AddFragment<FTransformFragment>();

	UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(&World);
	check(EntitySubsystem);

	UMassRepresentationSubsystem* RepresentationSubsystem = Cast<UMassRepresentationSubsystem>(World.GetSubsystemBase(RepresentationSubsystemClass));
	if (RepresentationSubsystem == nullptr)
	{
		UE_LOG(LogMassRepresentation, Error, TEXT("Expecting a valid class for the representation subsystem"));
		RepresentationSubsystem = UWorld::GetSubsystem<UMassRepresentationSubsystem>(&World);
		check(RepresentationSubsystem);
	}

	FMassRepresentationSubsystemSharedFragment Subsystem;
	Subsystem.RepresentationSubsystem = RepresentationSubsystem;
	uint32 SubsystemHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Subsystem));
	FSharedStruct SubsystemFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassRepresentationSubsystemSharedFragment>(SubsystemHash, Subsystem);
	BuildContext.AddSharedFragment(SubsystemFragment);

	if (!Params.RepresentationActorManagementClass)
	{
		UE_LOG(LogMassRepresentation, Error, TEXT("Expecting a valid class for the representation actor management"));
	}
	uint32 ParamsHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Params));
	FConstSharedStruct ParamsFragment = EntitySubsystem->GetOrCreateConstSharedFragment<FMassRepresentationParameters>(ParamsHash, Params);
	ParamsFragment.Get<FMassRepresentationParameters>().ComputeCachedValues();
	BuildContext.AddConstSharedFragment(ParamsFragment);

	FMassRepresentationFragment& RepresentationFragment = BuildContext.AddFragment_GetRef<FMassRepresentationFragment>();
	RepresentationFragment.StaticMeshDescIndex = RepresentationSubsystem->FindOrAddStaticMeshDesc(StaticMeshInstanceDesc);
	RepresentationFragment.HighResTemplateActorIndex = HighResTemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(HighResTemplateActor.Get()) : INDEX_NONE;
	RepresentationFragment.LowResTemplateActorIndex = LowResTemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(LowResTemplateActor.Get()) : INDEX_NONE;

	uint32 LODParamsHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(LODParams));
	FConstSharedStruct LODParamsFragment = EntitySubsystem->GetOrCreateConstSharedFragment<FMassVisualizationLODParameters>(LODParamsHash, LODParams);
	BuildContext.AddConstSharedFragment(LODParamsFragment);
	FSharedStruct LODSharedFragment = EntitySubsystem->GetOrCreateSharedFragment<FMassVisualizationLODSharedFragment>(LODParamsHash, LODParams);
	BuildContext.AddSharedFragment(LODSharedFragment);

	BuildContext.AddFragment<FMassRepresentationLODFragment>();
	BuildContext.AddTag<FMassVisibilityCulledByDistanceTag>();
	BuildContext.AddChunkFragment<FMassVisualizationChunkFragment>();
}


