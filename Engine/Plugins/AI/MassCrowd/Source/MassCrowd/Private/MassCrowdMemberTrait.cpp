// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdMemberTrait.h"
#include "MassCrowdFragments.h"
#include "MassEntityTemplateRegistry.h"

void UMassCrowdMemberTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddTag<FTagFragment_MassCrowd>();
	BuildContext.AddFragment<FMassCrowdLaneTrackingFragment>();	
}
