// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Object/ObjectElementEditorSelectionInterface.h"
#include "Elements/Object/ObjectElementData.h"

#include "UObject/Package.h"
#include "Elements/Framework/EngineElementsLibrary.h"

class FObjectElementTransactedElement : public ITypedElementTransactedElement
{
private:
	virtual TUniquePtr<ITypedElementTransactedElement> CloneImpl() const override
	{
		return MakeUnique<FObjectElementTransactedElement>(*this);
	}

	virtual FTypedElementHandle GetElementImpl() const override
	{
		const UObject* Object = ObjectPtr.Get(/*bEvenIfPendingKill*/true);
		return Object
			? UEngineElementsLibrary::AcquireEditorObjectElementHandle(Object)
			: FTypedElementHandle();
	}

	virtual void SetElementImpl(const FTypedElementHandle& InElementHandle) override
	{
		UObject* Object = ObjectElementDataUtil::GetObjectFromHandleChecked(InElementHandle);
		ObjectPtr = Object;
	}

	virtual void SerializeImpl(FArchive& InArchive) override
	{
		InArchive << ObjectPtr;
	}

	TWeakObjectPtr<UObject> ObjectPtr;
};

bool UObjectElementEditorSelectionInterface::ShouldPreventTransactions(const FTypedElementHandle& InElementHandle)
{
	const UObject* Object = ObjectElementDataUtil::GetObjectFromHandle(InElementHandle);
	return Object && ShouldObjectPreventTransactions(Object);
}

TUniquePtr<ITypedElementTransactedElement> UObjectElementEditorSelectionInterface::CreateTransactedElementImpl()
{
	return MakeUnique<FObjectElementTransactedElement>();
}

bool UObjectElementEditorSelectionInterface::ShouldObjectPreventTransactions(const UObject* InObject)
{
	// If the selection currently contains any PIE objects we should not be including it in the transaction buffer
	return InObject->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsScript | PKG_CompiledIn);
}
