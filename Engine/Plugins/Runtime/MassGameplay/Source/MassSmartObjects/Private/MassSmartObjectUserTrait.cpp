// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSmartObjectUserTrait.h"

#include "MassSmartObjectProcessor.h"
#include "MassEntityTemplateRegistry.h"

void UMassSmartObjectUserTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragment<FMassSmartObjectUserFragment>();
}
