// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusVariableDescription.h"

#include "OptimusDeformer.h"

UOptimusDeformer* UOptimusVariableDescription::GetOwningDeformer() const
{
	const UOptimusVariableContainer* Container = CastChecked<UOptimusVariableContainer>(GetOuter());
	return Container ? CastChecked<UOptimusDeformer>(Container->GetOuter()) : nullptr;
}


#if WITH_EDITOR
void UOptimusVariableDescription::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOptimusVariableDescription, VariableName))
	{
		UOptimusDeformer* Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			// Do a rename through an action. Otherwise undo won't notify on changes.
			Deformer->RenameVariable(this, VariableName);
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FOptimusDataType, TypeName))
	{
		UOptimusDeformer* Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			// Set the variable type again, so that we can remove any links that are now
			// type-incompatible.
			Deformer->SetVariableDataType(this, DataType);
		}

		// Make sure the value data container is still large enough to hold the property value.
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
