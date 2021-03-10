// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementCommonActions.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementUtil.h"

void FTypedElementCommonActionsCustomization::GetElementsToDelete(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToDelete)
{
	OutElementsToDelete->Add(InElementWorldHandle);
}

bool FTypedElementCommonActionsCustomization::DeleteElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	return InWorldInterface->DeleteElements(InElementHandles, InWorld, InSelectionSet, InDeletionOptions);
}

void FTypedElementCommonActionsCustomization::DuplicateElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, bool bOffsetLocations, TArray<FTypedElementHandle>& OutNewElements)
{
	InWorldInterface->DuplicateElements(InElementHandles, InWorld, bOffsetLocations, OutNewElements);
}


void UTypedElementCommonActions::GetSelectedElementsToDelete(const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToDelete) const
{
	OutElementsToDelete->Reset();
	InSelectionSet->ForEachSelectedElement<UTypedElementWorldInterface>([this, InSelectionSet, OutElementsToDelete](const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle)
	{
		FTypedElementCommonActionsElement CommonActionsElement(InElementWorldHandle, GetInterfaceCustomizationByTypeId(InElementWorldHandle.GetId().GetTypeId()));
		check(CommonActionsElement.IsSet());
		CommonActionsElement.GetElementsToDelete(InSelectionSet, OutElementsToDelete);
		return true;
	});
}

bool UTypedElementCommonActions::DeleteElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	return DeleteElements(MakeArrayView(ElementHandles), World, InSelectionSet, InDeletionOptions);
}

bool UTypedElementCommonActions::DeleteElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDuplicateByType;
	TypedElementUtil::BatchElementsByType(ElementHandles, ElementsToDuplicateByType);

	bool bSuccess = false;

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	UTypedElementRegistry::FDisableElementDestructionOnGC GCGuard(Registry);

	for (const auto& ElementsByTypePair : ElementsToDuplicateByType)
	{
		FTypedElementCommonActionsCustomization* CommonActionsCustomization = GetInterfaceCustomizationByTypeId(ElementsByTypePair.Key);
		UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key);
		if (CommonActionsCustomization && WorldInterface)
		{
			bSuccess |= CommonActionsCustomization->DeleteElements(WorldInterface, ElementsByTypePair.Value, World, InSelectionSet, InDeletionOptions);
		}
	}

	return bSuccess;
}

bool UTypedElementCommonActions::DeleteElements(const UTypedElementList* ElementList, UWorld* World, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDuplicateByType;
	TypedElementUtil::BatchElementsByType(ElementList, ElementsToDuplicateByType);

	bool bSuccess = false;

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	UTypedElementRegistry::FDisableElementDestructionOnGC GCGuard(Registry);

	for (const auto& ElementsByTypePair : ElementsToDuplicateByType)
	{
		FTypedElementCommonActionsCustomization* CommonActionsCustomization = GetInterfaceCustomizationByTypeId(ElementsByTypePair.Key);
		UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key);
		if (CommonActionsCustomization && WorldInterface)
		{
			bSuccess |= CommonActionsCustomization->DeleteElements(WorldInterface, ElementsByTypePair.Value, World, InSelectionSet, InDeletionOptions);
		}
	}

	return bSuccess;
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, bool bOffsetLocations)
{
	return DuplicateElements(MakeArrayView(ElementHandles), World, bOffsetLocations);
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, bool bOffsetLocations)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDuplicateByType;
	TypedElementUtil::BatchElementsByType(ElementHandles, ElementsToDuplicateByType);

	TArray<FTypedElementHandle> NewElements;
	NewElements.Reserve(ElementHandles.Num());

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	for (const auto& ElementsByTypePair : ElementsToDuplicateByType)
	{
		FTypedElementCommonActionsCustomization* CommonActionsCustomization = GetInterfaceCustomizationByTypeId(ElementsByTypePair.Key);
		UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key);
		if (CommonActionsCustomization && WorldInterface)
		{
			CommonActionsCustomization->DuplicateElements(WorldInterface, ElementsByTypePair.Value, World, bOffsetLocations, NewElements);
		}
	}

	return NewElements;
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateElements(const UTypedElementList* ElementList, UWorld* World, bool bOffsetLocations)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDuplicateByType;
	TypedElementUtil::BatchElementsByType(ElementList, ElementsToDuplicateByType);

	TArray<FTypedElementHandle> NewElements;
	NewElements.Reserve(ElementList->Num());

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	for (const auto& ElementsByTypePair : ElementsToDuplicateByType)
	{
		FTypedElementCommonActionsCustomization* CommonActionsCustomization = GetInterfaceCustomizationByTypeId(ElementsByTypePair.Key);
		UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key);
		if (CommonActionsCustomization && WorldInterface)
		{
			CommonActionsCustomization->DuplicateElements(WorldInterface, ElementsByTypePair.Value, World, bOffsetLocations, NewElements);
		}
	}

	return NewElements;
}

FTypedElementCommonActionsElement UTypedElementCommonActions::ResolveCommonActionsElement(const FTypedElementHandle& InElementHandle) const
{
	return InElementHandle
		? FTypedElementCommonActionsElement(UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(InElementHandle), GetInterfaceCustomizationByTypeId(InElementHandle.GetId().GetTypeId()))
		: FTypedElementCommonActionsElement();
}
