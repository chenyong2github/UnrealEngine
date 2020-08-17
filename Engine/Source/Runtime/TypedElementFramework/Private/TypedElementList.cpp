// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementList.h"
#include "TypedElementRegistry.h"

namespace TypedElementList_Private
{

void GetElementImpl(const UTypedElementRegistry* InRegistry, const FTypedElementHandle& InElementHandle, const UClass* InBaseInterfaceType, FTypedElement& OutElement)
{
	InRegistry->Private_GetElementImpl(InElementHandle, InBaseInterfaceType, OutElement);
}

} // namespace TypedElementList_Private


FTypedElementListLegacySync::FTypedElementListLegacySync(const FTypedElementList& InElementList)
	: ElementList(InElementList)
{
}

FTypedElementListLegacySync::FOnSyncEvent& FTypedElementListLegacySync::OnSyncEvent()
{
	return OnSyncEventDelegate;
}

void FTypedElementListLegacySync::Private_EmitSyncEvent(const ESyncType InSyncType, const FTypedElementHandle& InElementHandle)
{
	const bool bIsWithinBatchOperation = IsRunningBatchOperation();
	bBatchOperationIsDirty |= bIsWithinBatchOperation;
	OnSyncEventDelegate.Broadcast(ElementList, InSyncType, InElementHandle, bIsWithinBatchOperation);
}

bool FTypedElementListLegacySync::IsRunningBatchOperation() const
{
	return NumOpenBatchOperations > 0;
}

void FTypedElementListLegacySync::BeginBatchOperation()
{
	++NumOpenBatchOperations;
}

void FTypedElementListLegacySync::EndBatchOperation(const bool InNotify)
{
	checkf(NumOpenBatchOperations > 0, TEXT("Batch operation underflow!"));

	if (--NumOpenBatchOperations == 0)
	{
		const bool bNotifyChange = bBatchOperationIsDirty && InNotify;
		bBatchOperationIsDirty = false;

		if (bNotifyChange)
		{
			Private_EmitSyncEvent(ESyncType::BatchComplete);
			check(!bBatchOperationIsDirty); // This should still be false after emitting the notification!
		}
	}
}

bool FTypedElementListLegacySync::IsBatchOperationDirty() const
{
	return bBatchOperationIsDirty;
}

void FTypedElementListLegacySync::ForceBatchOperationDirty()
{
	if (NumOpenBatchOperations > 0)
	{
		bBatchOperationIsDirty = true;
	}
}


FTypedElementList::FTypedElementList(UTypedElementRegistry* InRegistry)
	: Registry(InRegistry)
{
	Registry->Private_OnElementListCreated(this);
}

FTypedElementList::~FTypedElementList()
{
	LegacySync.Reset();
	Registry->Private_OnElementListDestroyed(this);
}

FTypedElementListPtr FTypedElementList::Private_CreateElementList(UTypedElementRegistry* InRegistry)
{
	return FTypedElementListPtr(new FTypedElementList(InRegistry));
}

FTypedElementListPtr FTypedElementList::Clone() const
{
	FTypedElementListPtr ClonedElementList = Private_CreateElementList(Registry);
	ClonedElementList->ElementCombinedIds = ElementCombinedIds;
	ClonedElementList->ElementHandles = ElementHandles;
	return ClonedElementList;
}

bool FTypedElementList::AddElementImpl(FTypedElementHandle&& InElementHandle)
{
	if (!InElementHandle)
	{
		return false;
	}

	bool bAlreadyAdded = false;
	ElementCombinedIds.Add(InElementHandle.GetId().GetCombinedId(), &bAlreadyAdded);

	if (!bAlreadyAdded)
	{
		const FTypedElementHandle& AddedElementHandle = ElementHandles.Add_GetRef(MoveTemp(InElementHandle));
		NoteListChanged(EChangeType::Added, AddedElementHandle);
	}

	return !bAlreadyAdded;
}

bool FTypedElementList::RemoveElementImpl(const FTypedElementId& InElementId)
{
	if (!InElementId)
	{
		return false;
	}

	const bool bRemoved = ElementCombinedIds.Remove(InElementId.GetCombinedId()) > 0;

	if (bRemoved)
	{
		const int32 ElementHandleIndexToRemove = ElementHandles.IndexOfByPredicate([&InElementId](const FTypedElementHandle& InElementHandle)
		{
			return InElementHandle.GetId() == InElementId;
		});
		checkSlow(ElementHandleIndexToRemove != INDEX_NONE);

		FTypedElementHandle RemovedElementHandle = MoveTemp(ElementHandles[ElementHandleIndexToRemove]);
		ElementHandles.RemoveAt(ElementHandleIndexToRemove, 1, /*bAllowShrinking*/false);

		NoteListChanged(EChangeType::Removed, RemovedElementHandle);
	}

	return bRemoved;
}

int32 FTypedElementList::RemoveAllElementsImpl(TFunctionRef<bool(const FTypedElementHandle&)> InPredicate)
{
	if (LegacySync)
	{
		LegacySync->BeginBatchOperation();
	}

	int32 RemovedCount = 0;
	for (int32 Index = ElementHandles.Num() - 1; Index >= 0; --Index)
	{
		if (InPredicate(ElementHandles[Index]))
		{
			FTypedElementHandle RemovedElementHandle = MoveTemp(ElementHandles[Index]);
			ElementCombinedIds.Remove(RemovedElementHandle.GetId().GetCombinedId());
			ElementHandles.RemoveAt(Index, 1, /*bAllowShrinking*/false);

			NoteListChanged(EChangeType::Removed, RemovedElementHandle);

			++RemovedCount;
		}
	}

	if (LegacySync)
	{
		LegacySync->EndBatchOperation();
	}

	return RemovedCount;
}

bool FTypedElementList::ContainsElementImpl(const FTypedElementId& InElementId) const
{
	return InElementId 
		&& ElementCombinedIds.Contains(InElementId.GetCombinedId());
}

FTypedElementListLegacySync& FTypedElementList::Legacy_GetSync()
{
	if (!LegacySync)
	{
		LegacySync = MakeUnique<FTypedElementListLegacySync>(*this);
	}
	return *LegacySync;
}

void FTypedElementList::NotifyPendingChanges()
{
	if (bHasPendingNotify)
	{
		bHasPendingNotify = false;
		OnChangedDelegate.Broadcast(*this);
		check(!bHasPendingNotify); // This should still be false after emitting the notification!
	}
}

void FTypedElementList::NoteListChanged(const EChangeType InChangeType, const FTypedElementHandle& InElementHandle)
{
	bHasPendingNotify = true;

	if (LegacySync)
	{
		FTypedElementListLegacySync::ESyncType SyncType = FTypedElementListLegacySync::ESyncType::Modified;
		switch (InChangeType)
		{
		case EChangeType::Added:
			SyncType = FTypedElementListLegacySync::ESyncType::Added;
			break;

		case EChangeType::Removed:
			SyncType = FTypedElementListLegacySync::ESyncType::Removed;
			break;

		case EChangeType::Cleared:
			SyncType = FTypedElementListLegacySync::ESyncType::Cleared;
			break;

		default:
			break;
		}

		LegacySync->Private_EmitSyncEvent(SyncType, InElementHandle);
	}
}
