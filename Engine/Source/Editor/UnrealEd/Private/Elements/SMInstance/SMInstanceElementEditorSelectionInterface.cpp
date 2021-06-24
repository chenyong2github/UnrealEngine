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
		FSMInstanceId SMInstanceId = FSMInstanceElementIdMap::Get().GetSMInstanceIdFromSMInstanceElementId(FSMInstanceElementId{ ISMComponentPtr.Get(/*bEvenIfPendingKill*/true), InstanceId });
		return SMInstanceId
			? UEngineElementsLibrary::AcquireEditorSMInstanceElementHandle(SMInstanceId)
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

bool USMInstanceElementEditorSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const FTypedElementListProxy InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	FTypedElementListConstPtr SelectionSetPtr = InSelectionSet.GetElementList();
	const FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);

	if (SelectionSetPtr && SMInstance)
	{
		if (SelectionSetPtr->Num() == 0)
		{
			return false;
		}

		if (SelectionSetPtr->Contains(InElementHandle))
		{
			return true;
		}

		if (InSelectionOptions.AllowIndirect())
		{
			if (FTypedElementHandle ISMComponentElement = UEngineElementsLibrary::AcquireEditorComponentElementHandle(SMInstance.GetISMComponent(), /*bAllowCreate*/false))
			{
				return SelectionSetPtr->Contains(ISMComponentElement);
			}
		}
	}

	return false;
}

bool USMInstanceElementEditorSelectionInterface::ShouldPreventTransactions(const FTypedElementHandle& InElementHandle)
{
	const FSMInstanceManager SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && UObjectElementEditorSelectionInterface::ShouldObjectPreventTransactions(SMInstance.GetISMComponent());
}

TUniquePtr<ITypedElementTransactedElement> USMInstanceElementEditorSelectionInterface::CreateTransactedElementImpl()
{
	return MakeUnique<FSMInstanceElementTransactedElement>();
}
