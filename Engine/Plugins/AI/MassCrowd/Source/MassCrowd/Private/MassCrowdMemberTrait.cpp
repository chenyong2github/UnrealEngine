// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdMemberTrait.h"
#include "MassCrowdFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCrowdNavigationProcessor.h"

void UMassCrowdMemberTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddTag<FTagFragment_MassCrowd>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassCrowdLaneTrackingFragment>();	
}
