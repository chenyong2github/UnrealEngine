// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementCommonActions.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementUtil.h"

#include "UObject/GCObjectScopeGuard.h"

void FTypedElementCommonActionsCustomization::GetElementsForAction(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UTypedElementList* InElementList, UTypedElementList* OutElementsToDelete)
{
	OutElementsToDelete->Add(InElementWorldHandle);
}

bool FTypedElementCommonActionsCustomization::DeleteElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	return InWorldInterface->DeleteElements(InElementHandles, InWorld, InSelectionSet, InDeletionOptions);
}

void FTypedElementCommonActionsCustomization::DuplicateElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, const FVector& InLocationOffset, TArray<FTypedElementHandle>& OutNewElements)
{
	InWorldInterface->DuplicateElements(InElementHandles, InWorld, InLocationOffset, OutNewElements);
}


void UTypedElementCommonActions::GetElementsForAction(const UTypedElementList* InElementList, UTypedElementList* OutElementsForAction) const
{
	OutElementsForAction->Reset();
	InElementList->ForEachElement<UTypedElementWorldInterface>([this, InElementList, OutElementsForAction](const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle)
	{
		FTypedElementCommonActionsElement CommonActionsElement(InElementWorldHandle, GetInterfaceCustomizationByTypeId(InElementWorldHandle.GetId().GetTypeId()));
		check(CommonActionsElement.IsSet());
		CommonActionsElement.GetElementsForAction(InElementList, OutElementsForAction);
		return true;
	});
}

bool UTypedElementCommonActions::DeleteElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions)
{
	return DeleteElements(MakeArrayView(ElementHandles), World, SelectionSet, DeletionOptions);
}

bool UTypedElementCommonActions::DeleteElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDeleteByType;
	TypedElementUtil::BatchElementsByType(ElementHandles, ElementsToDeleteByType);

	bool bSuccess = false;

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	UTypedElementRegistry::FDisableElementDestructionOnGC GCGuard(Registry);

	for (const auto& ElementsByTypePair : ElementsToDeleteByType)
	{
		FTypedElementCommonActionsCustomization* CommonActionsCustomization = GetInterfaceCustomizationByTypeId(ElementsByTypePair.Key);
		UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key);
		if (CommonActionsCustomization && WorldInterface)
		{
			bSuccess |= CommonActionsCustomization->DeleteElements(WorldInterface, ElementsByTypePair.Value, World, SelectionSet, DeletionOptions);
		}
	}

	return bSuccess;
}

bool UTypedElementCommonActions::DeleteElements(const UTypedElementList* ElementList, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDeleteByType;
	TypedElementUtil::BatchElementsByType(ElementList, ElementsToDeleteByType);

	bool bSuccess = false;

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	UTypedElementRegistry::FDisableElementDestructionOnGC GCGuard(Registry);

	for (const auto& ElementsByTypePair : ElementsToDeleteByType)
	{
		FTypedElementCommonActionsCustomization* CommonActionsCustomization = GetInterfaceCustomizationByTypeId(ElementsByTypePair.Key);
		UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key);
		if (CommonActionsCustomization && WorldInterface)
		{
			bSuccess |= CommonActionsCustomization->DeleteElements(WorldInterface, ElementsByTypePair.Value, World, SelectionSet, DeletionOptions);
		}
	}

	return bSuccess;
}

bool UTypedElementCommonActions::DeleteSelectedElements(UTypedElementSelectionSet* SelectionSet, UWorld* World, const FTypedElementDeletionOptions& DeletionOptions)
{
	return DeleteElementsInList(SelectionSet->GetElementList(), World, SelectionSet, DeletionOptions);
}

bool UTypedElementCommonActions::DeleteElementsInList(const UTypedElementList* ElementList, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions)
{
	TGCObjectScopeGuard<UTypedElementList> ElementsForAction(UTypedElementRegistry::GetInstance()->CreateElementList());
	GetElementsForAction(ElementList, ElementsForAction.Get());

	const bool bSuccess = DeleteElements(ElementsForAction.Get(), World, SelectionSet, DeletionOptions);
	ElementsForAction.Get()->Reset();
	return bSuccess;
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, const FVector& LocationOffset)
{
	return DuplicateElements(MakeArrayView(ElementHandles), World, LocationOffset);
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, const FVector& LocationOffset)
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
			CommonActionsCustomization->DuplicateElements(WorldInterface, ElementsByTypePair.Value, World, LocationOffset, NewElements);
		}
	}

	return NewElements;
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateElements(const UTypedElementList* ElementList, UWorld* World, const FVector& LocationOffset)
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
			CommonActionsCustomization->DuplicateElements(WorldInterface, ElementsByTypePair.Value, World, LocationOffset, NewElements);
		}
	}

	return NewElements;
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateSelectedElements(const UTypedElementSelectionSet* SelectionSet, UWorld* World, const FVector& LocationOffset)
{
	return DuplicateElementsInList(SelectionSet->GetElementList(), World, LocationOffset);
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateElementsInList(const UTypedElementList* ElementList, UWorld* World, const FVector& LocationOffset)
{
	TGCObjectScopeGuard<UTypedElementList> ElementsForAction(UTypedElementRegistry::GetInstance()->CreateElementList());
	GetElementsForAction(ElementList, ElementsForAction.Get());

	TArray<FTypedElementHandle> NewElements = DuplicateElements(MakeArrayView(ElementsForAction.Get()->GetElementHandles()), World, LocationOffset);
	ElementsForAction.Get()->Reset();
	return NewElements;
}

FTypedElementCommonActionsElement UTypedElementCommonActions::ResolveCommonActionsElement(const FTypedElementHandle& InElementHandle) const
{
	return InElementHandle
		? FTypedElementCommonActionsElement(UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(InElementHandle), GetInterfaceCustomizationByTypeId(InElementHandle.GetId().GetTypeId()))
		: FTypedElementCommonActionsElement();
}
