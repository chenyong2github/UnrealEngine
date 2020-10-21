// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Object/ObjectElementEditorSelectionProxy.h"
#include "Elements/Object/ObjectElementData.h"
#include "UObject/UObjectAnnotation.h"

bool UObjectElementEditorSelectionProxy::SelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	if (const FObjectElementData* ObjectData = InElementSelectionHandle.GetData<FObjectElementData>())
	{
		if (InElementSelectionHandle.SelectElement(InSelectionSet, InSelectionOptions))
		{
			GSelectedObjectAnnotation.Set(ObjectData->Object);
			return true;
		}
	}

	return false;
}

bool UObjectElementEditorSelectionProxy::DeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	if (const FObjectElementData* ObjectData = InElementSelectionHandle.GetData<FObjectElementData>())
	{
		if (InElementSelectionHandle.DeselectElement(InSelectionSet, InSelectionOptions))
		{
			GSelectedObjectAnnotation.Clear(ObjectData->Object);
			return true;
		}
	}

	return false;
}
