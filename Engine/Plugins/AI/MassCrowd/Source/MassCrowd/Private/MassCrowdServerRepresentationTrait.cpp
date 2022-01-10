// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdServerRepresentationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCrowdRepresentationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassRepresentationFragments.h"
#include "Engine/World.h"

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

	if (UMassCrowdRepresentationSubsystem* RepresentationSubsystem = World.GetSubsystem<UMassCrowdRepresentationSubsystem>())
	{
		FMassRepresentationFragment& RepresentationFragment = BuildContext.AddFragment_GetRef<FMassRepresentationFragment>();
		RepresentationFragment.StaticMeshDescIndex = INDEX_NONE;
		RepresentationFragment.HighResTemplateActorIndex = TemplateActor.Get() ? RepresentationSubsystem->FindOrAddTemplateActor(TemplateActor.Get()) : INDEX_NONE;
		RepresentationFragment.LowResTemplateActorIndex = INDEX_NONE;

		BuildContext.AddFragment<FMassRepresentationLODFragment>();

		BuildContext.AddChunkFragment<FMassVisualizationChunkFragment>();
	}
}