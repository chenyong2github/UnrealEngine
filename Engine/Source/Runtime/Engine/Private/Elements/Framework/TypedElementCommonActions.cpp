// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementCommonActions.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementUtil.h"

#include "UObject/GCObjectScopeGuard.h"

bool FTypedElementCommonActionsCustomization::DeleteElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	return InWorldInterface->DeleteElements(InElementHandles, InWorld, InSelectionSet, InDeletionOptions);
}

void FTypedElementCommonActionsCustomization::DuplicateElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, const FVector& InLocationOffset, TArray<FTypedElementHandle>& OutNewElements)
{
	InWorldInterface->DuplicateElements(InElementHandles, InWorld, InLocationOffset, OutNewElements);
}


bool UTypedElementCommonActions::DeleteSelectedElements(UTypedElementSelectionSet* SelectionSet, UWorld* World, const FTypedElementDeletionOptions& DeletionOptions)
{
	TGCObjectScopeGuard<UTypedElementList> NormalizedElements(SelectionSet->GetNormalizedSelection(FTypedElementSelectionNormalizationOptions()));
	const bool bSuccess = DeleteNormalizedElements(NormalizedElements.Get(), World, SelectionSet, DeletionOptions);
	NormalizedElements.Get()->Reset();
	return bSuccess;
}

bool UTypedElementCommonActions::DeleteNormalizedElements(const UTypedElementList* ElementList, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions)
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

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateSelectedElements(const UTypedElementSelectionSet* SelectionSet, UWorld* World, const FVector& LocationOffset)
{
	TGCObjectScopeGuard<UTypedElementList> NormalizedElements(SelectionSet->GetNormalizedSelection(FTypedElementSelectionNormalizationOptions()));
	TArray<FTypedElementHandle> NewElements = DuplicateNormalizedElements(NormalizedElements.Get(), World, LocationOffset);
	NormalizedElements.Get()->Reset();
	return NewElements;
}

TArray<FTypedElementHandle> UTypedElementCommonActions::DuplicateNormalizedElements(const UTypedElementList* ElementList, UWorld* World, const FVector& LocationOffset)
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

FTypedElementCommonActionsElement UTypedElementCommonActions::ResolveCommonActionsElement(const FTypedElementHandle& InElementHandle) const
{
	return InElementHandle
		? FTypedElementCommonActionsElement(UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(InElementHandle), GetInterfaceCustomizationByTypeId(InElementHandle.GetId().GetTypeId()))
		: FTypedElementCommonActionsElement();
}
