// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSmartObjectUserTrait.h"

#include "MassSmartObjectFragments.h"
#include "MassEntityTemplateRegistry.h"

void UMassSmartObjectUserTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragment<FMassSmartObjectUserFragment>();
}
