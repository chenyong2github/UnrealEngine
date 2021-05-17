// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusVariableDescription.h"

#include "OptimusDeformer.h"

void UOptimusVariableDescription::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOptimusVariableDescription, VariableName))
	{
		UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(GetOuter());

		if (ensure(Deformer))
		{
			// Do a rename through an action. Otherwise undo won't notify on changes.
			Deformer->RenameVariable(this, VariableName);
		}
	}
}
