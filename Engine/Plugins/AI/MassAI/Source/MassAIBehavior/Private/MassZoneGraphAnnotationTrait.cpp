// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassZoneGraphAnnotationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassZoneGraphAnnotationFragments.h"

void UMassZoneGraphAnnotationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragmentWithDefaultInitializer<FMassZoneGraphAnnotationTagsFragment>();
	BuildContext.AddChunkFragment<FMassZoneGraphAnnotationVariableTickChunkFragment>();
}
