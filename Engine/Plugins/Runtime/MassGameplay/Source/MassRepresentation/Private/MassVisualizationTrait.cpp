// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassVisualizationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassRepresentationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationProcessor.h"
#include "Engine/World.h"

UMassVisualizationTrait::UMassVisualizationTrait()
{
	RepresentationSubsystemClass = UMassRepresentationSubsystem::StaticClass();
	RepresentationFragmentDeinitializerClass = UMassRepresentationFragmentDestructor::StaticClass();
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
	BuildContext.AddFragment<FDataFragment_Transform>();

	if (UMassRepresentationSubsystem* RepresentationSubsystem = Cast<UMassRepresentationSubsystem>(World.GetSubsystemBase(RepresentationSubsystemClass)))
	{
		FMassRepresentationFragment& RepresentationFragment = BuildContext.AddFragment_GetRef<FMassRepresentationFragment>();
		RepresentationFragment.StaticMeshDescIndex = RepresentationSubsystem->FindOrAddStaticMeshDesc(StaticMeshInstanceDesc);
		RepresentationFragment.HighResTemplateActorIndex = HighResTemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(HighResTemplateActor.Get()) : INDEX_NONE;
		RepresentationFragment.LowResTemplateActorIndex = LowResTemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(LowResTemplateActor.Get()) : INDEX_NONE;

		BuildContext.AddDeinitializer(*(RepresentationFragmentDeinitializerClass->GetDefaultObject<UMassProcessor>()));

		BuildContext.AddFragment<FMassViewerInfoFragment>();
		BuildContext.AddFragment<FMassRepresentationLODFragment>();
		BuildContext.AddTag<FMassVisibilityCulledByDistanceTag>();

		BuildContext.AddChunkFragment<FMassVisualizationChunkFragment>();
	}
}


