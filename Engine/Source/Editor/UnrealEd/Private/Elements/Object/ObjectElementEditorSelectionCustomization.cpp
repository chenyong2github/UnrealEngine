// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Object/ObjectElementEditorSelectionCustomization.h"
#include "Elements/Object/ObjectElementData.h"
#include "UObject/UObjectAnnotation.h"

bool FObjectElementEditorSelectionCustomization::SelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	if (const UObject* Object = ObjectElementDataUtil::GetObjectFromHandle(InElementSelectionHandle))
	{
		if (InElementSelectionHandle.SelectElement(InSelectionSet, InSelectionOptions))
		{
			GSelectedObjectAnnotation.Set(Object);
			return true;
		}
	}

	return false;
}

bool FObjectElementEditorSelectionCustomization::DeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	if (const UObject* Object = ObjectElementDataUtil::GetObjectFromHandle(InElementSelectionHandle))
	{
		if (InElementSelectionHandle.DeselectElement(InSelectionSet, InSelectionOptions))
		{
			GSelectedObjectAnnotation.Clear(Object);
			return true;
		}
	}

	return false;
}
