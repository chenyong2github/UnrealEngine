// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/TypedElementRegistry.h"

namespace TypedElementList_Private
{

void GetElementImpl(const UTypedElementRegistry* InRegistry, const FTypedElementHandle& InElementHandle, const UClass* InBaseInterfaceType, FTypedElement& OutElement)
{
	InRegistry->Private_GetElementImpl(InElementHandle, InBaseInterfaceType, OutElement);
}

} // namespace TypedElementList_Private


FTypedElementListLegacySync::FTypedElementListLegacySync(const UTypedElementList* InElementList)
	: ElementList(InElementList)
{
	checkf(ElementList, TEXT("ElementList was null!"));
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


FTypedElementListLegacySyncScopedBatch::FTypedElementListLegacySyncScopedBatch(UTypedElementList* InElementList, const bool InNotify)
	: ElementListLegacySync(InElementList->Legacy_GetSyncPtr())
	, bNotify(InNotify)
{
	if (ElementListLegacySync)
	{
		ElementListLegacySync->BeginBatchOperation();
	}
}

FTypedElementListLegacySyncScopedBatch::~FTypedElementListLegacySyncScopedBatch()
{
	if (ElementListLegacySync)
	{
		ElementListLegacySync->EndBatchOperation(bNotify);
	}
}

UTypedElementList* UTypedElementList::Private_CreateElementList(UTypedElementRegistry* InRegistry)
{
	UTypedElementList* ElementList = NewObject<UTypedElementList>();
	ElementList->Initialize(InRegistry);
	return ElementList;
}

void UTypedElementList::Initialize(UTypedElementRegistry* InRegistry)
{
	checkf(!Registry.Get(), TEXT("Initialize has already been called!"));
	Registry = InRegistry;
	checkf(InRegistry, TEXT("Registry is null!"));
	InRegistry->Private_OnElementListCreated(this);
}

void UTypedElementList::BeginDestroy()
{
	Super::BeginDestroy();

	LegacySync.Reset();
	if (UTypedElementRegistry* RegistryPtr = Registry.Get())
	{
		RegistryPtr->Private_OnElementListDestroyed(this);
		Registry = nullptr;
	}
}

UTypedElementList* UTypedElementList::Clone() const
{
	UTypedElementList* ClonedElementList = Private_CreateElementList(Registry.Get());
	ClonedElementList->ElementCombinedIds = ElementCombinedIds;
	ClonedElementList->ElementHandles = ElementHandles;
	return ClonedElementList;
}

UTypedElementInterface* UTypedElementList::GetElementInterface(const FTypedElementHandle& InElementHandle, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
{
	return Registry->GetElementInterface(InElementHandle, InBaseInterfaceType);
}

bool UTypedElementList::HasElements(const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
{
	bool bHasFilteredElements = false;

	if (InBaseInterfaceType)
	{
		ForEachElementHandle([&bHasFilteredElements](const FTypedElementHandle&)
		{
			bHasFilteredElements = true;
			return false;
		}, InBaseInterfaceType);
	}
	else
	{
		bHasFilteredElements = Num() > 0;
	}

	return bHasFilteredElements;
}

int32 UTypedElementList::CountElements(const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
{
	int32 NumFilteredElements = 0;

	if (InBaseInterfaceType)
	{
		ForEachElementHandle([&NumFilteredElements](const FTypedElementHandle&)
		{
			++NumFilteredElements;
			return true;
		}, InBaseInterfaceType);
	}
	else
	{
		NumFilteredElements = Num();
	}

	return NumFilteredElements;
}

TArray<FTypedElementHandle> UTypedElementList::GetElementHandles(const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
{
	TArray<FTypedElementHandle> FilteredElementHandles;
	FilteredElementHandles.Reserve(ElementHandles.Num());

	ForEachElementHandle([&FilteredElementHandles](const FTypedElementHandle& InElementHandle)
	{
		FilteredElementHandles.Add(InElementHandle);
		return true;
	}, InBaseInterfaceType);

	return FilteredElementHandles;
}

void UTypedElementList::ForEachElementHandle(TFunctionRef<bool(const FTypedElementHandle&)> InCallback, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
{
	for (const FTypedElementHandle& ElementHandle : ElementHandles)
	{
		if (ElementHandle && (!InBaseInterfaceType || GetElementInterface(ElementHandle, InBaseInterfaceType)))
		{
			if (!InCallback(ElementHandle))
			{
				break;
			}
		}
	}
}

bool UTypedElementList::AddElementImpl(FTypedElementHandle&& InElementHandle)
{
	if (!InElementHandle)
	{
		return false;
	}

	NoteListMayChange();

	bool bAlreadyAdded = false;
	ElementCombinedIds.Add(InElementHandle.GetId().GetCombinedId(), &bAlreadyAdded);

	if (!bAlreadyAdded)
	{
		const FTypedElementHandle& AddedElementHandle = ElementHandles.Add_GetRef(MoveTemp(InElementHandle));
		NoteListChanged(EChangeType::Added, AddedElementHandle);
	}

	return !bAlreadyAdded;
}

bool UTypedElementList::RemoveElementImpl(const FTypedElementId& InElementId)
{
	if (!InElementId)
	{
		return false;
	}

	NoteListMayChange();

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

int32 UTypedElementList::RemoveAllElementsImpl(TFunctionRef<bool(const FTypedElementHandle&)> InPredicate)
{
	int32 RemovedCount = 0;

	if (ElementHandles.Num() > 0)
	{
		FTypedElementListLegacySyncScopedBatch LegacySyncBatch(this);

		NoteListMayChange();

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
	}

	return RemovedCount;
}

bool UTypedElementList::ContainsElementImpl(const FTypedElementId& InElementId) const
{
	return InElementId 
		&& ElementCombinedIds.Contains(InElementId.GetCombinedId());
}

FTypedElementListLegacySync& UTypedElementList::Legacy_GetSync()
{
	if (!LegacySync)
	{
		LegacySync = MakeUnique<FTypedElementListLegacySync>(this);
	}
	return *LegacySync;
}

FTypedElementListLegacySync* UTypedElementList::Legacy_GetSyncPtr()
{
	return LegacySync.Get();
}

void UTypedElementList::NotifyPendingChanges()
{
	if (bHasPendingNotify)
	{
		bHasPendingNotify = false;
		OnChangedDelegate.Broadcast(this);
		check(!bHasPendingNotify); // This should still be false after emitting the notification!
	}
}

void UTypedElementList::ClearPendingChanges()
{
	bHasPendingNotify = false;
}

void UTypedElementList::NoteListMayChange()
{
	if (!bHasPendingNotify)
	{
		OnPreChangeDelegate.Broadcast(this);
	}
}

void UTypedElementList::NoteListChanged(const EChangeType InChangeType, const FTypedElementHandle& InElementHandle)
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
