// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusVariableDescription.h"

#include "OptimusDeformer.h"

#if WITH_EDITOR
void UOptimusVariableDescription::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOptimusVariableDescription, VariableName))
	{
		UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(GetOuter());

		if (ensure(Deformer))
		{
			// Do a rename through an action. Otherwise undo won't notify on changes.
			Deformer->RenameVariable(this, VariableName);
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FOptimusDataType, TypeName))
	{
		ValueData.Reset();
		if (DataType->CanCreateProperty())
		{
			// Create a temporary property from the type so that we can get the size of the
			// type for properly resizing the storage.
			TUniquePtr<FProperty> TempProperty(DataType->CreateProperty(nullptr, NAME_None));

			ValueData.SetNumZeroed(TempProperty->GetSize());
		}
	}
}
#endif
