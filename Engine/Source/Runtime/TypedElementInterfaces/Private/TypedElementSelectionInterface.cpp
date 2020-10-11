// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementSelectionInterface.h"
#include "TypedElementList.h"

bool UTypedElementSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	return InSelectionSet->Contains(InElementHandle);
}

bool UTypedElementSelectionInterface::SelectElement(const FTypedElementHandle& InElementHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	return InSelectionSet->Add(InElementHandle);
}

bool UTypedElementSelectionInterface::DeselectElement(const FTypedElementHandle& InElementHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	return InSelectionSet->Remove(InElementHandle);
}
