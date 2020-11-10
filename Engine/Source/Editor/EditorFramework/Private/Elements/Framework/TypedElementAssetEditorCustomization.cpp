// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementAssetEditorCustomization.h"
#include "Elements/Framework/TypedElementRegistry.h"

FTypedHandleTypeId FTypedElementAssetEditorCustomizationRegistryBase::GetElementTypeIdFromName(const FName InElementTypeName) const
{
	return UTypedElementRegistry::GetInstance()->GetRegisteredElementTypeId(InElementTypeName);
}

FTypedHandleTypeId FTypedElementAssetEditorCustomizationRegistryBase::GetElementTypeIdFromNameChecked(const FName InElementTypeName) const
{
	const FTypedHandleTypeId ElementTypeId = GetElementTypeIdFromName(InElementTypeName);
	checkf(ElementTypeId > 0, TEXT("Element type '%s' has not been registered!"), *InElementTypeName.ToString());
	return ElementTypeId;
}
