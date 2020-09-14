// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection.h"
#include "UObject/Package.h"
#include "UObject/UObjectAnnotation.h"
#include "GameFramework/Actor.h"

#define UE_USE_ELEMENT_LIST_SELECTION (1)

#if UE_USE_ELEMENT_LIST_SELECTION
#include "TypedElementList.h"
#include "TypedElementSelectionInterface.h"
#endif	// UE_USE_ELEMENT_LIST_SELECTION

DEFINE_LOG_CATEGORY_STATIC(LogSelection, Log, All);

namespace Selection_Private
{

class FSelectionStoreBase : public ISelectionStore
{
public:
	explicit FSelectionStoreBase(const UClass* InObjectBaseClass);

	//~ ISelectionStore interface
	virtual void SetSink(ISelectionStoreSink* InSink) override;
	virtual bool IsValidObjectToSelect(const UObject* InObject) const override;

protected:
	/** Base type of objects that this selection instance manages. */
	const UClass* ObjectBaseClass = nullptr;

	/** Sink instance to notify of changes. */
	ISelectionStoreSink* Sink = nullptr;
};

FSelectionStoreBase::FSelectionStoreBase(const UClass* InObjectBaseClass)
	: ObjectBaseClass(InObjectBaseClass)
{
}

void FSelectionStoreBase::SetSink(ISelectionStoreSink* InSink)
{
	Sink = InSink;
}

bool FSelectionStoreBase::IsValidObjectToSelect(const UObject* InObject) const
{
	return InObject && (!ObjectBaseClass || InObject->IsA(ObjectBaseClass));
}


class FObjectSelectionStore : public FSelectionStoreBase
{
public:
	explicit FObjectSelectionStore(const UClass* InObjectBaseClass);

	//~ ISelectionStore interface
	virtual void SetElementList(UTypedElementList* InElementList) override;
	virtual int32 GetNumObjects() const override;
	virtual UObject* GetObjectAtIndex(const int32 InIndex) const override;
	virtual bool IsObjectSelected(const UObject* InObject) const override;
	virtual void SelectObject(UObject* InObject) override;
	virtual void DeselectObject(UObject* InObject) override;
	virtual int32 DeselectObjects(TFunctionRef<bool(UObject*)> InPredicate) override;
	virtual void BeginBatchSelection() override;
	virtual void EndBatchSelection(const bool InNotify) override;
	virtual bool IsBatchSelecting() const override;
	virtual void ForceBatchDirty() override;

private:
	/** Tracks the number of active selection operations.  Allows batched selection operations to only send one notification at the end of the batch */
	int32 SelectionMutex = 0;

	/** Tracks whether the selection set changed during a batch selection operation */
	bool bIsBatchDirty = false;

	/** List of selected objects, ordered as they were selected. */
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
};

FObjectSelectionStore::FObjectSelectionStore(const UClass* InObjectBaseClass)
	: FSelectionStoreBase(InObjectBaseClass)
{
}

void FObjectSelectionStore::SetElementList(UTypedElementList* InElementList)
{
	checkf(!UE_USE_ELEMENT_LIST_SELECTION, TEXT("This selection store does not support element lists! Only actor and component selections may currently use element lists!"));
}

int32 FObjectSelectionStore::GetNumObjects() const
{
	return SelectedObjects.Num();
}

UObject* FObjectSelectionStore::GetObjectAtIndex(const int32 InIndex) const
{
	return SelectedObjects.IsValidIndex(InIndex)
		? SelectedObjects[InIndex].Get()
		: nullptr;
}

bool FObjectSelectionStore::IsObjectSelected(const UObject* InObject) const
{
	return SelectedObjects.Contains(InObject);
}

void FObjectSelectionStore::SelectObject(UObject* InObject)
{
	bIsBatchDirty = true;
	SelectedObjects.Add(InObject);
	if (Sink)
	{
		Sink->OnObjectSelected(InObject, !IsBatchSelecting());
	}
}

void FObjectSelectionStore::DeselectObject(UObject* InObject)
{
	bIsBatchDirty = true;
	SelectedObjects.RemoveSingle(InObject);
	if (Sink)
	{
		Sink->OnObjectDeselected(InObject, !IsBatchSelecting());
	}
}

int32 FObjectSelectionStore::DeselectObjects(TFunctionRef<bool(UObject*)> InPredicate)
{
	int32 RemovedCount = 0;
	
	for (int32 Index = SelectedObjects.Num() - 1; Index >= 0; --Index)
	{
		UObject* SelectedObject = SelectedObjects[Index].Get();
		if (!SelectedObject || InPredicate(SelectedObject))
		{
			SelectedObjects.RemoveAt(Index);
			if (SelectedObject)
			{
				if (Sink)
				{
					Sink->OnObjectDeselected(SelectedObject, /*bNotify*/false);
				}
				++RemovedCount;
			}
		}
	}

	if (RemovedCount > 0)
	{
		bIsBatchDirty = true;
		if (Sink)
		{
			Sink->OnSelectedChanged(/*bSyncState*/false, !IsBatchSelecting());
		}
	}

	return RemovedCount;
}

void FObjectSelectionStore::BeginBatchSelection()
{
	if (SelectionMutex++ == 0)
	{
		bIsBatchDirty = false;
	}
}

void FObjectSelectionStore::EndBatchSelection(const bool InNotify)
{
	if (--SelectionMutex == 0)
	{
		const bool bSelectionChanged = bIsBatchDirty;
		bIsBatchDirty = false;

		if (Sink && bSelectionChanged)
		{
			Sink->OnSelectedChanged(/*bSyncState*/false, !IsBatchSelecting() && InNotify);
		}
	}
}

bool FObjectSelectionStore::IsBatchSelecting() const
{
	return SelectionMutex > 0;
}

void FObjectSelectionStore::ForceBatchDirty()
{
	bIsBatchDirty = true;
}


#if UE_USE_ELEMENT_LIST_SELECTION

class FElementSelectionStore : public FSelectionStoreBase
{
public:
	explicit FElementSelectionStore(const UClass* InObjectBaseClass);
	virtual ~FElementSelectionStore();

	//~ ISelectionStore interface
	virtual void SetElementList(UTypedElementList* InElementList) override;
	virtual int32 GetNumObjects() const override;
	virtual UObject* GetObjectAtIndex(const int32 InIndex) const override;
	virtual bool IsValidObjectToSelect(const UObject* InObject) const override;
	virtual bool IsObjectSelected(const UObject* InObject) const override;
	virtual void SelectObject(UObject* InObject) override;
	virtual void DeselectObject(UObject* InObject) override;
	virtual int32 DeselectObjects(TFunctionRef<bool(UObject*)> InPredicate) override;
	virtual void BeginBatchSelection() override;
	virtual void EndBatchSelection(const bool InNotify) override;
	virtual bool IsBatchSelecting() const override;
	virtual void ForceBatchDirty() override;

	virtual FTypedElementHandle GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate = true) const = 0;

private:
	void OnElementListSyncEvent(const UTypedElementList* InElementList, FTypedElementListLegacySync::ESyncType InSyncType, const FTypedElementHandle& InElementHandle, bool bIsWithinBatchOperation);
	UObject* GetObjectForElementHandle(const FTypedElementHandle& InElementHandle) const;

	/** External list of elements that this selection instance is bridging. This will be null unless a level editor instance exists. */
	UTypedElementList* ElementList = nullptr;
};

FElementSelectionStore::FElementSelectionStore(const UClass* InObjectBaseClass)
	: FSelectionStoreBase(InObjectBaseClass)
{
}

FElementSelectionStore::~FElementSelectionStore()
{
	if (ElementList)
	{
		ElementList->Legacy_GetSync().OnSyncEvent().RemoveAll(this);
	}
}

void FElementSelectionStore::SetElementList(UTypedElementList* InElementList)
{
	if (ElementList)
	{
		ElementList->Legacy_GetSync().OnSyncEvent().RemoveAll(this);
	}

	ElementList = InElementList;

	if (ElementList)
	{
		ElementList->Legacy_GetSync().OnSyncEvent().AddRaw(this, &FElementSelectionStore::OnElementListSyncEvent);
	}
}

int32 FElementSelectionStore::GetNumObjects() const
{
	return ElementList
		? ElementList->Num()
		: 0;
}

UObject* FElementSelectionStore::GetObjectAtIndex(const int32 InIndex) const
{
	if (ElementList && ElementList->IsValidIndex(InIndex))
	{
		const FTypedElementHandle& ElementHandle = ElementList->GetElementHandleAt(InIndex);
		return GetObjectForElementHandle(ElementHandle);
	}

	return nullptr;
}

bool FElementSelectionStore::IsValidObjectToSelect(const UObject* InObject) const
{
	bool bIsValid = FSelectionStoreBase::IsValidObjectToSelect(InObject);

	if (bIsValid && ElementList)
	{
		TTypedElement<UTypedElementSelectionInterface> SelectionElement = ElementList->GetElement<UTypedElementSelectionInterface>(GetElementHandleForObject(InObject));
		if (SelectionElement)
		{
			bIsValid &= SelectionElement.IsValidSelection() && SelectionElement.Legacy_GetSelectionObject() == InObject;
		}
		else
		{
			// Elements must implement the selection interface in order to be selected!
			bIsValid = false;
		}
	}

	return bIsValid;
}

bool FElementSelectionStore::IsObjectSelected(const UObject* InObject) const
{
	if (ElementList)
	{
		if (const FTypedElementHandle ElementHandle = GetElementHandleForObject(InObject, /*bAllowCreate*/false))
		{
			return ElementList->Contains(ElementHandle);
		}
	}

	return false;
}

void FElementSelectionStore::SelectObject(UObject* InObject)
{
	if (ElementList)
	{
		FTypedElementHandle ElementHandle = GetElementHandleForObject(InObject);
		check(ElementHandle);
		ElementList->Add(MoveTemp(ElementHandle));
	}
}

void FElementSelectionStore::DeselectObject(UObject* InObject)
{
	if (ElementList)
	{
		if (const FTypedElementHandle ElementHandle = GetElementHandleForObject(InObject, /*bAllowCreate*/false))
		{
			ElementList->Remove(ElementHandle);
		}
	}
}

int32 FElementSelectionStore::DeselectObjects(TFunctionRef<bool(UObject*)> InPredicate)
{
	if (ElementList)
	{
		return ElementList->RemoveAll([this, &InPredicate](const FTypedElementHandle& InElementHandle)
		{
			UObject* Object = GetObjectForElementHandle(InElementHandle);
			return Object && InPredicate(Object);
		});
	}

	return 0;
}

void FElementSelectionStore::BeginBatchSelection()
{
	if (ElementList)
	{
		ElementList->Legacy_GetSync().BeginBatchOperation();
	}
}

void FElementSelectionStore::EndBatchSelection(const bool InNotify)
{
	if (ElementList)
	{
		ElementList->Legacy_GetSync().EndBatchOperation(InNotify);
	}
}

bool FElementSelectionStore::IsBatchSelecting() const
{
	return ElementList && ElementList->Legacy_GetSync().IsRunningBatchOperation();
}

void FElementSelectionStore::ForceBatchDirty()
{
	if (ElementList)
	{
		ElementList->Legacy_GetSync().ForceBatchOperationDirty();
	}
}

void FElementSelectionStore::OnElementListSyncEvent(const UTypedElementList* InElementList, FTypedElementListLegacySync::ESyncType InSyncType, const FTypedElementHandle& InElementHandle, bool bIsWithinBatchOperation)
{
	if (Sink)
	{
		switch (InSyncType)
		{
		case FTypedElementListLegacySync::ESyncType::Added:
			if (UObject* Object = GetObjectForElementHandle(InElementHandle))
			{
				Sink->OnObjectSelected(Object, !bIsWithinBatchOperation);
			}
			break;

		case FTypedElementListLegacySync::ESyncType::Removed:
			if (UObject* Object = GetObjectForElementHandle(InElementHandle))
			{
				Sink->OnObjectDeselected(Object, !bIsWithinBatchOperation);
			}
			break;

		case FTypedElementListLegacySync::ESyncType::BatchComplete:
			Sink->OnSelectedChanged(/*bSyncState*/false, !bIsWithinBatchOperation);
			break;

		default:
			Sink->OnSelectedChanged(/*bSyncState*/true, !bIsWithinBatchOperation);
			break;
		}
	}
}

UObject* FElementSelectionStore::GetObjectForElementHandle(const FTypedElementHandle& InElementHandle) const
{
	check(InElementHandle && ElementList);
	
	TTypedElement<UTypedElementSelectionInterface> SelectionElement = ElementList->GetElement<UTypedElementSelectionInterface>(InElementHandle);
	check(SelectionElement);

	UObject* Object = SelectionElement.Legacy_GetSelectionObject();
	if (Object && Object->IsA(ObjectBaseClass))
	{
		return Object;
	}

	return nullptr;
}


class FActorElementSelectionStore : public FElementSelectionStore
{
public:
	FActorElementSelectionStore();

	//~ FElementSelectionStore interface
	virtual FTypedElementHandle GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate = true) const override;
};

FActorElementSelectionStore::FActorElementSelectionStore()
	: FElementSelectionStore(AActor::StaticClass())
{
}

FTypedElementHandle FActorElementSelectionStore::GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate) const
{
	check(InObject);

	const AActor* Actor = CastChecked<AActor>(InObject);
	return Actor->AcquireEditorElementHandle(bAllowCreate);
}


class FComponentElementSelectionStore : public FElementSelectionStore
{
public:
	FComponentElementSelectionStore();

	//~ FElementSelectionStore interface
	virtual FTypedElementHandle GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate = true) const override;
};

FComponentElementSelectionStore::FComponentElementSelectionStore()
	: FElementSelectionStore(UActorComponent::StaticClass())
{
}

FTypedElementHandle FComponentElementSelectionStore::GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate) const
{
	check(InObject);

	const UActorComponent* Component = CastChecked<UActorComponent>(InObject);
	return Component->AcquireEditorElementHandle(bAllowCreate);
}

#endif	// UE_USE_ELEMENT_LIST_SELECTION

} // namespace Selection_Private

USelection::FOnSelectionChanged	USelection::SelectionChangedEvent;
USelection::FOnSelectionChanged	USelection::SelectObjectEvent;
FSimpleMulticastDelegate		USelection::SelectNoneEvent;

USelection* USelection::CreateObjectSelection(FUObjectAnnotationSparseBool* InSelectionAnnotation, UObject* InOuter, FName InName, EObjectFlags InFlags)
{
	USelection* Selection = NewObject<USelection>(InOuter, InName, InFlags);
	Selection->Initialize(InSelectionAnnotation, MakeShared<Selection_Private::FObjectSelectionStore>(nullptr));
	return Selection;
}

USelection* USelection::CreateActorSelection(FUObjectAnnotationSparseBool* InSelectionAnnotation, UObject* InOuter, FName InName, EObjectFlags InFlags)
{
	USelection* Selection = NewObject<USelection>(InOuter, InName, InFlags);
#if UE_USE_ELEMENT_LIST_SELECTION
	Selection->Initialize(InSelectionAnnotation, MakeShared<Selection_Private::FActorElementSelectionStore>());
#else	// UE_USE_ELEMENT_LIST_SELECTION
	Selection->Initialize(InSelectionAnnotation, MakeShared<Selection_Private::FObjectSelectionStore>(AActor::StaticClass()));
#endif	// UE_USE_ELEMENT_LIST_SELECTION
	return Selection;
}

USelection* USelection::CreateComponentSelection(FUObjectAnnotationSparseBool* InSelectionAnnotation, UObject* InOuter, FName InName, EObjectFlags InFlags)
{
	USelection* Selection = NewObject<USelection>(InOuter, InName, InFlags);
#if UE_USE_ELEMENT_LIST_SELECTION
	Selection->Initialize(InSelectionAnnotation, MakeShared<Selection_Private::FComponentElementSelectionStore>());
#else	// UE_USE_ELEMENT_LIST_SELECTION
	Selection->Initialize(InSelectionAnnotation, MakeShared<Selection_Private::FObjectSelectionStore>(UActorComponent::StaticClass()));
#endif	// UE_USE_ELEMENT_LIST_SELECTION
	return Selection;
}

void USelection::Initialize(FUObjectAnnotationSparseBool* InSelectionAnnotation, TSharedRef<Selection_Private::ISelectionStore>&& InSelectionStore)
{
	SelectionStore = MoveTemp(InSelectionStore);
	SelectionStore->SetSink(this);

	if (InSelectionAnnotation)
	{
		SelectionAnnotation = InSelectionAnnotation;
		bOwnsSelectionAnnotation = false;
	}
	else
	{
		SelectionAnnotation = new FUObjectAnnotationSparseBool;
		bOwnsSelectionAnnotation = true;
	}

	SyncSelectedState();
}

void USelection::OnObjectSelected(UObject* InObject, const bool bNotify)
{
	check(InObject);

	SelectionAnnotation->Set(InObject);

	if (FSelectedClassInfo* SelectedClassInfo = SelectedClasses.Find(InObject->GetClass()))
	{
		++SelectedClassInfo->SelectionCount;
	}
	else
	{
		// 1 Object with a new class type has been selected
		SelectedClasses.Add(FSelectedClassInfo(InObject->GetClass(), 1));
	}

	if (bNotify)
	{
		// Call this after the item has been added from the selection set.
		USelection::SelectObjectEvent.Broadcast(InObject);
	}
}

void USelection::OnObjectDeselected(UObject* InObject, const bool bNotify)
{
	check(InObject);

	SelectionAnnotation->Clear(InObject);

	FSetElementId Id = SelectedClasses.FindId(InObject->GetClass());
	if (Id.IsValidId())
	{
		FSelectedClassInfo& ClassInfo = SelectedClasses[Id];
		// One less object of this class is selected;
		--ClassInfo.SelectionCount;
		// If no more objects of the selected class exists, remove it
		if (ClassInfo.SelectionCount == 0)
		{
			SelectedClasses.Remove(Id);
		}
	}

	if (bNotify)
	{
		// Call this after the item has been removed from the selection set.
		USelection::SelectObjectEvent.Broadcast(InObject);
	}
}

void USelection::OnSelectedChanged(const bool bSyncState, const bool bNotify)
{
	if (bSyncState)
	{
		SyncSelectedState();
	}
	
	if (bNotify)
	{
		USelection::SelectionChangedEvent.Broadcast(this);
	}
}

void USelection::SyncSelectedState()
{
	SelectedClasses.Reset();
	SelectionAnnotation->ClearAll();

	for (int32 Idx = 0; Idx < Num(); ++Idx)
	{
		if (UObject* Object = GetSelectedObject(Idx))
		{
			SelectionAnnotation->Set(Object);

			if (FSelectedClassInfo* SelectedClassInfo = SelectedClasses.Find(Object->GetClass()))
			{
				++SelectedClassInfo->SelectionCount;
			}
			else
			{
				// 1 Object with a new class type has been selected
				SelectedClasses.Add(FSelectedClassInfo(Object->GetClass(), 1));
			}
		}
	}
}

void USelection::Select(UObject* InObject)
{
	check(InObject && SelectionStore->IsValidObjectToSelect(InObject));

	if (!SelectionAnnotation->Get(InObject))
	{
		SelectionStore->SelectObject(InObject);
	}
}

void USelection::Deselect(UObject* InObject)
{
	check(InObject);

	if (SelectionAnnotation->Get(InObject))
	{
		SelectionStore->DeselectObject(InObject);
	}
}

void USelection::Select(UObject* InObject, bool bSelect)
{
	if (bSelect)
	{
		Select(InObject);
	}
	else
	{
		Deselect(InObject);
	}
}

void USelection::ToggleSelect(UObject* InObject)
{
	Select(InObject, InObject && !InObject->IsSelected());
}

void USelection::DeselectAll(UClass* InClass)
{
	// Fast path for deselecting all UObjects with any flags
	if (InClass == UObject::StaticClass())
	{
		InClass = nullptr;
	}

	SelectionStore->DeselectObjects([InClass](UObject* InObject)
	{
		return !InClass || InObject->IsA(InClass);
	});
}

void USelection::ForceBatchDirty()
{
	if (IsBatchSelecting())
	{
		SelectionStore->ForceBatchDirty();
	}
}

void USelection::NoteSelectionChanged()
{
	USelection::SelectionChangedEvent.Broadcast(this);
}

void USelection::NoteUnknownSelectionChanged()
{
	USelection::SelectionChangedEvent.Broadcast(nullptr);
}

bool USelection::IsSelected(const UObject* InObject) const
{
	return InObject && SelectionStore->IsObjectSelected(InObject);
}

void USelection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	if (Ar.IsSaving())
	{
		GetSelectedObjects(SelectedObjects); // TODO: The old code used to use bEvenIfPendingKill during the resolve
	}
	Ar << SelectedObjects;

	if (Ar.IsLoading())
	{
		BeginBatchSelectOperation();

		// The set of selected objects may have changed, so make sure our annotations exactly match the list, otherwise
		// UObject::IsSelected() could return a result that was different from the list of objects returned by GetSelectedObjects()
		// This needs to happen in serialize because other code may check the selection state in PostEditUndo and the order of PostEditUndo is indeterminate.
		DeselectAll(nullptr);
		for (const TWeakObjectPtr<UObject>& ObjectPtr : SelectedObjects)
		{
			if (UObject* Object = ObjectPtr.Get(true))
			{
				SelectionStore->SelectObject(Object);
			}
		}
		SyncSelectedState();

		EndBatchSelectOperation(/*bNotify*/false);
	}
}

bool USelection::Modify(bool bAlwaysMarkDirty/* =true */)
{
	// If the selection currently contains any PIE objects we should not be including it in the transaction buffer
	for (int32 Idx = 0; Idx < Num(); ++Idx)
	{
		UObject* Object = GetSelectedObject(Idx);
		if (Object && Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsScript | PKG_CompiledIn))
		{
			return false;
		}
	}

	return Super::Modify(bAlwaysMarkDirty);
}

void USelection::BeginDestroy()
{
	Super::BeginDestroy();

	SelectionStore.Reset();

	if (bOwnsSelectionAnnotation)
	{
		delete SelectionAnnotation;
		SelectionAnnotation = nullptr;
	}
}

#undef UE_USE_ELEMENT_LIST_SELECTION
