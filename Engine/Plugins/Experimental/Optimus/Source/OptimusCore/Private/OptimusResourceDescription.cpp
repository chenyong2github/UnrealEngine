// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusResourceDescription.h"

#include "OptimusDeformer.h"


UOptimusDeformer* UOptimusResourceDescription::GetOwningDeformer() const
{
	const UOptimusResourceContainer* Container = CastChecked<UOptimusResourceContainer>(GetOuter());
	return Container ? CastChecked<UOptimusDeformer>(Container->GetOuter()) : nullptr;
}


#if WITH_EDITOR
void UOptimusResourceDescription::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent
	)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOptimusResourceDescription, ResourceName))
	{
		UOptimusDeformer *Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			// Do a rename through an action. Otherwise undo won't notify on changes.
			Deformer->RenameResource(this, ResourceName);
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FOptimusDataTypeRef, TypeName))
	{
		UOptimusDeformer *Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			// Set the resource type again, so that we can remove any links that are now
			// type-incompatible.
			Deformer->SetResourceDataType(this, DataType);
		}
	}
}
#endif
