// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Object/ObjectElementEditorSelectionInterface.h"
#include "Elements/Object/ObjectElementData.h"

#include "UObject/Package.h"
#include "Elements/Framework/EngineElementsLibrary.h"

bool UObjectElementEditorSelectionInterface::ShouldPreventTransactions(const FTypedElementHandle& InElementHandle)
{
	const UObject* Object = ObjectElementDataUtil::GetObjectFromHandle(InElementHandle);
	return Object && ShouldObjectPreventTransactions(Object);
}

void UObjectElementEditorSelectionInterface::WriteTransactedElement(const FTypedElementHandle& InElementHandle, FArchive& InArchive)
{
	if (const UObject* Object = ObjectElementDataUtil::GetObjectFromHandle(InElementHandle))
	{
		WriteTransactedObject(Object, InArchive);
	}
}

FTypedElementHandle UObjectElementEditorSelectionInterface::ReadTransactedElement(FArchive& InArchive)
{
	return ReadTransactedObject(InArchive, [](const UObject* InObject)
	{
		check(InObject);
		return UEngineElementsLibrary::AcquireEditorObjectElementHandle(InObject);
	});
}

bool UObjectElementEditorSelectionInterface::ShouldObjectPreventTransactions(const UObject* InObject)
{
	// If the selection currently contains any PIE objects we should not be including it in the transaction buffer
	return InObject->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsScript | PKG_CompiledIn);
}

void UObjectElementEditorSelectionInterface::WriteTransactedObject(const UObject* InObject, FArchive& InArchive)
{
	TWeakObjectPtr<const UObject> SelectedObject = InObject;
	InArchive << SelectedObject;
}

FTypedElementHandle UObjectElementEditorSelectionInterface::ReadTransactedObject(FArchive& InArchive, TFunctionRef<FTypedElementHandle(const UObject*)> InFindElementForObject)
{
	TWeakObjectPtr<const UObject> SelectedObject;
	InArchive << SelectedObject;

	const UObject* SelectedObjectPtr = SelectedObject.Get(/*bEvenIfPendingKill*/true);
	return SelectedObjectPtr
		? InFindElementForObject(SelectedObjectPtr)
		: FTypedElementHandle();
}
