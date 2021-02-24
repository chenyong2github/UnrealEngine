// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementEditorSelectionInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Object/ObjectElementEditorSelectionInterface.h"

class FSMInstanceElementTransactedElement : public ITypedElementTransactedElement
{
private:
	virtual TUniquePtr<ITypedElementTransactedElement> CloneImpl() const override
	{
		return MakeUnique<FSMInstanceElementTransactedElement>(*this);
	}

	virtual FTypedElementHandle GetElementImpl() const override
	{
		FSMInstanceElementId SMInstanceElementId{ ISMComponentPtr.Get(/*bEvenIfPendingKill*/true), InstanceId };
		return FSMInstanceElementIdMap::Get().GetSMInstanceIdFromSMInstanceElementId(SMInstanceElementId)
			? UEngineElementsLibrary::AcquireEditorSMInstanceElementHandle(SMInstanceElementId)
			: FTypedElementHandle();
	}

	virtual void SetElementImpl(const FTypedElementHandle& InElementHandle) override
	{
		const FSMInstanceElementData& SMInstanceElementData = InElementHandle.GetDataChecked<FSMInstanceElementData>();
		ISMComponentPtr = SMInstanceElementData.InstanceElementId.ISMComponent;
		InstanceId = SMInstanceElementData.InstanceElementId.InstanceId;
	}

	virtual void SerializeImpl(FArchive& InArchive) override
	{
		InArchive << ISMComponentPtr;
		InArchive << InstanceId;
	}

	TWeakObjectPtr<UInstancedStaticMeshComponent> ISMComponentPtr;
	uint64 InstanceId = 0;
};

bool USMInstanceElementEditorSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	if (const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle))
	{
		if (InSelectionSet->Num() == 0)
		{
			return false;
		}

		if (InSelectionSet->Contains(InElementHandle))
		{
			return true;
		}

		if (InSelectionOptions.AllowIndirect())
		{
			if (FTypedElementHandle ISMComponentElement = UEngineElementsLibrary::AcquireEditorComponentElementHandle(SMInstance.ISMComponent, /*bAllowCreate*/false))
			{
				return InSelectionSet->Contains(ISMComponentElement);
			}
		}
	}

	return false;
}

bool USMInstanceElementEditorSelectionInterface::ShouldPreventTransactions(const FTypedElementHandle& InElementHandle)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && UObjectElementEditorSelectionInterface::ShouldObjectPreventTransactions(SMInstance.ISMComponent);
}

TUniquePtr<ITypedElementTransactedElement> USMInstanceElementEditorSelectionInterface::CreateTransactedElementImpl()
{
	return MakeUnique<FSMInstanceElementTransactedElement>();
}
