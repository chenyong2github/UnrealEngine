// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusResourceDescription.h"

#include "OptimusDeformer.h"

#if WITH_EDITOR
void UOptimusResourceDescription::PostEditChangeProperty(
	struct FPropertyChangedEvent& PropertyChangedEvent
	)
{
	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOptimusResourceDescription, ResourceName))
	{
		UOptimusDeformer *Deformer = Cast<UOptimusDeformer>(GetOuter());

		if (ensure(Deformer))
		{
			// Do a rename through an action. Otherwise undo won't notify on changes.
			Deformer->RenameResource(this, ResourceName);
		}
	}
}
#endif
