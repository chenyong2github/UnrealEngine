// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection.h"
#include "UObject/Package.h"
#include "GameFramework/Actor.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogSelection, Log, All);

class ISelectionElementBridge
{
public:
	virtual ~ISelectionElementBridge() = default;
	virtual bool IsValidObjectType(const UObject* InObject) const = 0;
	virtual FTypedElementHandle GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate = true) const = 0;
};

class FObjectSelectionElementBridge : public ISelectionElementBridge
{
public:
	virtual bool IsValidObjectType(const UObject* InObject) const override
	{
		check(InObject);
		return true;
	}

	virtual FTypedElementHandle GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate = true) const override
	{
		check(InObject);
		return UEngineElementsLibrary::AcquireEditorObjectElementHandle(InObject, bAllowCreate);
	}
};

class FActorSelectionElementBridge : public ISelectionElementBridge
{
public:
	virtual bool IsValidObjectType(const UObject* InObject) const override
	{
		check(InObject);
		return InObject->IsA<AActor>();
	}

	virtual FTypedElementHandle GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate = true) const override
	{
		check(InObject);
		return UEngineElementsLibrary::AcquireEditorActorElementHandle(CastChecked<AActor>(InObject), bAllowCreate);
	}
};

class FComponentSelectionElementBridge : public ISelectionElementBridge
{
public:
	virtual bool IsValidObjectType(const UObject* InObject) const override
	{
		check(InObject);
		return InObject->IsA<UActorComponent>();
	}

	virtual FTypedElementHandle GetElementHandleForObject(const UObject* InObject, const bool bAllowCreate = true) const override
	{
		check(InObject);
		return UEngineElementsLibrary::AcquireEditorComponentElementHandle(CastChecked<UActorComponent>(InObject), bAllowCreate);
	}
};

USelection::FOnSelectionChanged						USelection::SelectionChangedEvent;
USelection::FOnSelectionChanged						USelection::SelectObjectEvent;
FSimpleMulticastDelegate							USelection::SelectNoneEvent;
USelection::FOnSelectionElementSelectionPtrChanged	USelection::SelectionElementSelectionPtrChanged;

USelection* USelection::CreateObjectSelection(UObject* InOuter, FName InName, EObjectFlags InFlags)
{
	USelection* Selection = NewObject<USelection>(InOuter, InName, InFlags);
	Selection->Initialize(MakeShared<FObjectSelectionElementBridge>());
	return Selection;
}

USelection* USelection::CreateActorSelection(UObject* InOuter, FName InName, EObjectFlags InFlags)
{
	USelection* Selection = NewObject<USelection>(InOuter, InName, InFlags);
	Selection->Initialize(MakeShared<FActorSelectionElementBridge>());
	return Selection;
}

USelection* USelection::CreateComponentSelection(UObject* InOuter, FName InName, EObjectFlags InFlags)
{
	USelection* Selection = NewObject<USelection>(InOuter, InName, InFlags);
	Selection->Initialize(MakeShared<FComponentSelectionElementBridge>());
	return Selection;
}

void USelection::Initialize(TSharedRef<ISelectionElementBridge>&& InSelectionElementBridge)
{
	SelectionElementBridge = MoveTemp(InSelectionElementBridge);
	SyncSelectedClasses();
}

void USelection::SetElementSelectionSet(UTypedElementSelectionSet* InElementSelectionSet)
{
	if (ElementSelectionSet)
	{
		ElementSelectionSet->Legacy_GetElementListSync().OnSyncEvent().RemoveAll(this);
	}

	UTypedElementSelectionSet* OldElementSelectionSet = ElementSelectionSet;
	ElementSelectionSet = InElementSelectionSet;

	if (ElementSelectionSet)
	{
		ElementSelectionSet->Legacy_GetElementListSync().OnSyncEvent().AddUObject(this, &USelection::OnElementListSyncEvent);
	}
	
	USelection::SelectionElementSelectionPtrChanged.Broadcast(this, OldElementSelectionSet, ElementSelectionSet);
}

UTypedElementSelectionSet* USelection::GetElementSelectionSet() const
{
	return ElementSelectionSet;
}

int32 USelection::Num() const
{
	return ElementSelectionSet
		? ElementSelectionSet->GetElementList()->Num()
		: 0;
}

UObject* USelection::GetSelectedObject(const int32 InIndex) const
{
	if (ElementSelectionSet)
	{
		const UTypedElementList* ElementList = ElementSelectionSet->GetElementList();
		if (ElementList->IsValidIndex(InIndex))
		{
			const FTypedElementHandle ElementHandle = ElementList->GetElementHandleAt(InIndex);
			return GetObjectForElementHandle(ElementHandle);
		}
	}

	return nullptr;
}

void USelection::BeginBatchSelectOperation()
{
	if (ElementSelectionSet)
	{
		ElementSelectionSet->Legacy_GetElementListSync().BeginBatchOperation();
	}
}

void USelection::EndBatchSelectOperation(bool bNotify)
{
	if (ElementSelectionSet)
	{
		ElementSelectionSet->Legacy_GetElementListSync().EndBatchOperation(bNotify);
	}
}

bool USelection::IsBatchSelecting() const
{
	return ElementSelectionSet && ElementSelectionSet->Legacy_GetElementListSync().IsRunningBatchOperation();
}

bool USelection::IsValidObjectToSelect(const UObject* InObject) const
{
	bool bIsValid = SelectionElementBridge->IsValidObjectType(InObject);

	if (bIsValid && ElementSelectionSet)
	{
		TTypedElement<UTypedElementObjectInterface> ObjectElement = ElementSelectionSet->GetElementList()->GetElement<UTypedElementObjectInterface>(SelectionElementBridge->GetElementHandleForObject(InObject));
		if (ObjectElement)
		{
			bIsValid &= ObjectElement.GetObject() == InObject;
		}
		else
		{
			// Elements must implement the object interface in order to be selected!
			bIsValid = false;
		}
	}

	return bIsValid;
}

UObject* USelection::GetObjectForElementHandle(const FTypedElementHandle& InElementHandle) const
{
	check(InElementHandle && ElementSelectionSet);

	if (TTypedElement<UTypedElementObjectInterface> ObjectElement = ElementSelectionSet->GetElementList()->GetElement<UTypedElementObjectInterface>(InElementHandle))
	{
		UObject* Object = ObjectElement.GetObject();
		if (Object && SelectionElementBridge->IsValidObjectType(Object))
		{
			return Object;
		}
	}

	return nullptr;
}

void USelection::OnElementListSyncEvent(const UTypedElementList* InElementList, FTypedElementListLegacySync::ESyncType InSyncType, const FTypedElementHandle& InElementHandle, bool bIsWithinBatchOperation)
{
	check(InElementList == ElementSelectionSet->GetElementList());
	switch (InSyncType)
	{
	case FTypedElementListLegacySync::ESyncType::Added:
		if (UObject* Object = GetObjectForElementHandle(InElementHandle))
		{
			OnObjectSelected(Object, !bIsWithinBatchOperation);
		}
		break;

	case FTypedElementListLegacySync::ESyncType::Removed:
		if (UObject* Object = GetObjectForElementHandle(InElementHandle))
		{
			OnObjectDeselected(Object, !bIsWithinBatchOperation);
		}
		break;

	case FTypedElementListLegacySync::ESyncType::BatchComplete:
		OnSelectedChanged(/*bSyncState*/false, !bIsWithinBatchOperation);
		break;

	default:
		OnSelectedChanged(/*bSyncState*/true, !bIsWithinBatchOperation);
		break;
	}
}

void USelection::OnObjectSelected(UObject* InObject, const bool bNotify)
{
	check(InObject);

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
		SyncSelectedClasses();
	}
	
	if (bNotify)
	{
		USelection::SelectionChangedEvent.Broadcast(this);
	}
}

void USelection::SyncSelectedClasses()
{
	SelectedClasses.Reset();

	for (int32 Idx = 0; Idx < Num(); ++Idx)
	{
		if (UObject* Object = GetSelectedObject(Idx))
		{
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
	check(InObject && IsValidObjectToSelect(InObject));
	if (ElementSelectionSet)
	{
		FTypedElementHandle ElementHandle = SelectionElementBridge->GetElementHandleForObject(InObject);
		check(ElementHandle);
		ElementSelectionSet->SelectElement(ElementHandle, FTypedElementSelectionOptions().SetAllowHidden(true).SetAllowGroups(false).SetWarnIfLocked(false));
	}
}

void USelection::Deselect(UObject* InObject)
{
	check(InObject);
	if (ElementSelectionSet)
	{
		if (const FTypedElementHandle ElementHandle = SelectionElementBridge->GetElementHandleForObject(InObject, /*bAllowCreate*/false))
		{
			ElementSelectionSet->DeselectElement(ElementHandle, FTypedElementSelectionOptions().SetAllowHidden(true).SetAllowGroups(false).SetWarnIfLocked(false));
		}
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
	if (!ElementSelectionSet)
	{
		return;
	}

	UClass* ClassToDeselect = InClass;
	if (!ClassToDeselect)
	{
		ClassToDeselect = UObject::StaticClass();
	}

	TArray<UObject*> ObjectsToDeselect;
	GetSelectedObjects(ClassToDeselect, ObjectsToDeselect);

	TArray<FTypedElementHandle, TInlineAllocator<256>> ElementsToDeselect;
	ElementsToDeselect.Reserve(ObjectsToDeselect.Num());
	for (UObject* ObjectToDeselect : ObjectsToDeselect)
	{
		if (FTypedElementHandle ElementHandle = SelectionElementBridge->GetElementHandleForObject(ObjectToDeselect, /*bAllowCreate*/false))
		{
			ElementsToDeselect.Add(MoveTemp(ElementHandle));
		}
	}

	if (ElementsToDeselect.Num() > 0)
	{
		ElementSelectionSet->DeselectElements(ElementsToDeselect, FTypedElementSelectionOptions().SetAllowHidden(true).SetAllowGroups(false).SetWarnIfLocked(false));
	}
}

void USelection::ForceBatchDirty()
{
	if (IsBatchSelecting())
	{
		check(ElementSelectionSet);
		ElementSelectionSet->Legacy_GetElementListSync().ForceBatchOperationDirty();
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
	if (InObject && ElementSelectionSet)
	{
		if (const FTypedElementHandle ElementHandle = SelectionElementBridge->GetElementHandleForObject(InObject, /*bAllowCreate*/false))
		{
			return ElementSelectionSet->IsElementSelected(ElementHandle, FTypedElementIsSelectedOptions());
		}
	}

	return false;
}

void USelection::Serialize(FArchive& Ar)
{
	if (ElementSelectionSet)
	{
		ElementSelectionSet->Serialize(Ar);
	}
}

bool USelection::Modify(bool bAlwaysMarkDirty)
{
	return ElementSelectionSet 
		&& ElementSelectionSet->Modify(bAlwaysMarkDirty);
}
