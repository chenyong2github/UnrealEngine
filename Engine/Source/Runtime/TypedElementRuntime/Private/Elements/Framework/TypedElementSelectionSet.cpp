// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/TypedElementRegistry.h"

#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"

UTypedElementSelectionSet::UTypedElementSelectionSet()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ElementList = UTypedElementRegistry::GetInstance()->CreateElementList();
		ElementList->OnPreChange().AddUObject(this, &UTypedElementSelectionSet::OnElementListPreChange);
		ElementList->OnChanged().AddUObject(this, &UTypedElementSelectionSet::OnElementListChanged);
	}
}

#if WITH_EDITOR
void UTypedElementSelectionSet::PreEditUndo()
{
	Super::PreEditUndo();

	checkf(!PendingUndoRedoState, TEXT("PendingUndoRedoState was set! Missing call to PostEditUndo?"));
	PendingUndoRedoState = MakeUnique<FTypedElementSelectionSetState>();
}

void UTypedElementSelectionSet::PostEditUndo()
{
	Super::PostEditUndo();

	checkf(PendingUndoRedoState, TEXT("PendingUndoRedoState was null! Missing call to PreEditUndo?"));
	RestoreSelectionState(*PendingUndoRedoState);
	PendingUndoRedoState.Reset();
}

bool UTypedElementSelectionSet::Modify(bool bAlwaysMarkDirty)
{
	if (GUndo && CanModify())
	{
		bool bCanModify = true;
		ElementList->ForEachElement<UTypedElementSelectionInterface>([&bCanModify](const TTypedElement<UTypedElementSelectionInterface>& InSelectionElement)
		{
			bCanModify = !InSelectionElement.ShouldPreventTransactions();
			return bCanModify;
		});

		if (!bCanModify)
		{
			return false;
		}

		return Super::Modify(bAlwaysMarkDirty);
	}

	return false;
}
#endif	// WITH_EDITOR

void UTypedElementSelectionSet::Serialize(FArchive& Ar)
{
	checkf(!Ar.IsPersistent(), TEXT("UTypedElementSelectionSet can only be serialized by transient archives!"));

	if (Ar.IsSaving())
	{
		FTypedElementSelectionSetState SelectionState = ElementList ? GetCurrentSelectionState() : FTypedElementSelectionSetState();

		int32 NumTransactedElements = SelectionState.TransactedElements.Num();
		Ar << NumTransactedElements;

		for (const TUniquePtr<ITypedElementTransactedElement>& TransactedElement : SelectionState.TransactedElements)
		{
			FTypedHandleTypeId TypeId = TransactedElement->GetElementType();
			Ar << TypeId;

			TransactedElement->Serialize(Ar);
		}
	}
	else if (Ar.IsLoading())
	{
		UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

		const bool bIsUndoRedo = PendingUndoRedoState && Ar.IsTransacting();
		FTypedElementSelectionSetState TmpSelectionState;

		FTypedElementSelectionSetState& SelectionState = bIsUndoRedo ? *PendingUndoRedoState : TmpSelectionState;
		SelectionState.CreatedFromSelectionSet = this;

		int32 NumTransactedElements = 0;
		Ar << NumTransactedElements;

		SelectionState.TransactedElements.Reserve(NumTransactedElements);
		for (int32 TransactedElementIndex = 0; TransactedElementIndex < NumTransactedElements; ++TransactedElementIndex)
		{
			FTypedHandleTypeId TypeId = 0;
			Ar << TypeId;

			UTypedElementSelectionInterface* ElementTypeSelectionInterface = Registry->GetElementInterface<UTypedElementSelectionInterface>(TypeId);
			checkf(ElementTypeSelectionInterface, TEXT("Failed to find selection interface for a previously transacted element type!"));

			TUniquePtr<ITypedElementTransactedElement> TransactedElement = ElementTypeSelectionInterface->CreateTransactedElement(TypeId);
			checkf(TransactedElement, TEXT("Failed to allocate a transacted element for a previously transacted element type!"));

			TransactedElement->Serialize(Ar);
			SelectionState.TransactedElements.Emplace(MoveTemp(TransactedElement));
		}

		if (ElementList && !bIsUndoRedo)
		{
			RestoreSelectionState(SelectionState);
		}
	}
}

bool UTypedElementSelectionSet::IsElementSelected(const FTypedElementHandle& InElementHandle, const FTypedElementIsSelectedOptions InSelectionOptions) const
{
	FTypedElementSelectionSetElement SelectionSetElement = ResolveSelectionSetElement(InElementHandle);
	return SelectionSetElement && SelectionSetElement.IsElementSelected(InSelectionOptions);
}

bool UTypedElementSelectionSet::CanSelectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions) const
{
	FTypedElementSelectionSetElement SelectionSetElement = ResolveSelectionSetElement(InElementHandle);
	return SelectionSetElement && SelectionSetElement.CanSelectElement(InSelectionOptions);
}

bool UTypedElementSelectionSet::CanDeselectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions) const
{
	FTypedElementSelectionSetElement SelectionSetElement = ResolveSelectionSetElement(InElementHandle);
	return SelectionSetElement && SelectionSetElement.CanDeselectElement(InSelectionOptions);
}

bool UTypedElementSelectionSet::SelectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions)
{
	FTypedElementSelectionSetElement SelectionSetElement = ResolveSelectionSetElement(InElementHandle);
	return SelectionSetElement && SelectionSetElement.CanSelectElement(InSelectionOptions) && SelectionSetElement.SelectElement(InSelectionOptions);
}

bool UTypedElementSelectionSet::SelectElements(const TArray<FTypedElementHandle>& InElementHandles, const FTypedElementSelectionOptions InSelectionOptions)
{
	return SelectElements(MakeArrayView(InElementHandles), InSelectionOptions);
}

bool UTypedElementSelectionSet::SelectElements(TArrayView<const FTypedElementHandle> InElementHandles, const FTypedElementSelectionOptions InSelectionOptions)
{
	FTypedElementListLegacySyncScopedBatch LegacySyncBatch(ElementList, InSelectionOptions.AllowLegacyNotifications());

	bool bSelectionChanged = false;

	for (const FTypedElementHandle& ElementHandle : InElementHandles)
	{
		bSelectionChanged |= SelectElement(ElementHandle, InSelectionOptions);
	}

	return bSelectionChanged;
}

bool UTypedElementSelectionSet::DeselectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions)
{
	FTypedElementSelectionSetElement SelectionSetElement = ResolveSelectionSetElement(InElementHandle);
	return SelectionSetElement && SelectionSetElement.CanDeselectElement(InSelectionOptions) && SelectionSetElement.DeselectElement(InSelectionOptions);
}

bool UTypedElementSelectionSet::DeselectElements(const TArray<FTypedElementHandle>& InElementHandles, const FTypedElementSelectionOptions InSelectionOptions)
{
	return DeselectElements(MakeArrayView(InElementHandles), InSelectionOptions);
}

bool UTypedElementSelectionSet::DeselectElements(TArrayView<const FTypedElementHandle> InElementHandles, const FTypedElementSelectionOptions InSelectionOptions)
{
	FTypedElementListLegacySyncScopedBatch LegacySyncBatch(ElementList, InSelectionOptions.AllowLegacyNotifications());

	bool bSelectionChanged = false;

	for (const FTypedElementHandle& ElementHandle : InElementHandles)
	{
		bSelectionChanged |= DeselectElement(ElementHandle, InSelectionOptions);
	}

	return bSelectionChanged;
}

bool UTypedElementSelectionSet::ClearSelection(const FTypedElementSelectionOptions InSelectionOptions)
{
	FTypedElementListLegacySyncScopedBatch LegacySyncBatch(ElementList, InSelectionOptions.AllowLegacyNotifications());

	bool bSelectionChanged = false;

	// Run deselection via the selection interface where possible
	{
		// Take a copy of the currently selected elements to avoid mutating the selection set while iterating
		TArray<FTypedElementHandle, TInlineAllocator<8>> ElementsCopy;
		ElementList->GetElementHandles(ElementsCopy);

		for (const FTypedElementHandle& ElementHandle : ElementsCopy)
		{
			bSelectionChanged |= DeselectElement(ElementHandle, InSelectionOptions);
		}
	}

	// TODO: BSP surfaces?

	// If anything remains in the selection set after processing elements that implement this interface, just clear it
	if (ElementList->Num() > 0)
	{
		bSelectionChanged = true;
		ElementList->Reset();
	}

	return bSelectionChanged;
}

bool UTypedElementSelectionSet::SetSelection(const TArray<FTypedElementHandle>& InElementHandles, const FTypedElementSelectionOptions InSelectionOptions)
{
	return SetSelection(MakeArrayView(InElementHandles), InSelectionOptions);
}

bool UTypedElementSelectionSet::SetSelection(TArrayView<const FTypedElementHandle> InElementHandles, const FTypedElementSelectionOptions InSelectionOptions)
{
	FTypedElementListLegacySyncScopedBatch LegacySyncBatch(ElementList, InSelectionOptions.AllowLegacyNotifications());

	bool bSelectionChanged = false;

	bSelectionChanged |= ClearSelection(InSelectionOptions);
	bSelectionChanged |= SelectElements(InElementHandles, InSelectionOptions);

	return bSelectionChanged;
}

bool UTypedElementSelectionSet::AllowSelectionModifiers(const FTypedElementHandle& InElementHandle) const
{
	FTypedElementSelectionSetElement SelectionSetElement = ResolveSelectionSetElement(InElementHandle);
	return SelectionSetElement && SelectionSetElement.AllowSelectionModifiers();
}

FTypedElementHandle UTypedElementSelectionSet::GetSelectionElement(const FTypedElementHandle& InElementHandle, const ETypedElementSelectionMethod InSelectionMethod) const
{
	FTypedElementSelectionSetElement SelectionSetElement = ResolveSelectionSetElement(InElementHandle);
	return SelectionSetElement ? SelectionSetElement.GetSelectionElement(InSelectionMethod) : FTypedElementHandle();
}

FTypedElementSelectionSetState UTypedElementSelectionSet::GetCurrentSelectionState() const
{
	FTypedElementSelectionSetState CurrentState;

	CurrentState.CreatedFromSelectionSet = this;

	CurrentState.TransactedElements.Reserve(ElementList->Num());
	ElementList->ForEachElement<UTypedElementSelectionInterface>([&CurrentState](const TTypedElement<UTypedElementSelectionInterface>& InSelectionElement)
	{
		if (TUniquePtr<ITypedElementTransactedElement> TransactedElement = InSelectionElement.CreateTransactedElement())
		{
			CurrentState.TransactedElements.Emplace(MoveTemp(TransactedElement));
		}
		return true;
	});

	return CurrentState;
}

void UTypedElementSelectionSet::RestoreSelectionState(const FTypedElementSelectionSetState& InSelectionState)
{
	if (InSelectionState.CreatedFromSelectionSet == this)
	{
		TArray<FTypedElementHandle, TInlineAllocator<256>> SelectedElements;
		SelectedElements.Reserve(InSelectionState.TransactedElements.Num());
		
		for (const TUniquePtr<ITypedElementTransactedElement>& TransactedElement : InSelectionState.TransactedElements)
		{
			if (FTypedElementHandle SelectedElement = TransactedElement->GetElement())
			{
				SelectedElements.Add(MoveTemp(SelectedElement));
			}
		}

		{
			TGuardValue<bool> GuardIsRestoringState(bIsRestoringState, true);

			const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
				.SetAllowHidden(true)
				.SetAllowGroups(false)
				.SetAllowLegacyNotifications(false)
				.SetWarnIfLocked(false);

			// TODO: Work out the intersection of the before and after state instead of clearing and reselecting?
			SetSelection(SelectedElements, SelectionOptions);
		}
	}
}

FTypedElementSelectionSetElement UTypedElementSelectionSet::ResolveSelectionSetElement(const FTypedElementHandle& InElementHandle) const
{
	return InElementHandle
		? FTypedElementSelectionSetElement(ElementList->GetElement<UTypedElementSelectionInterface>(InElementHandle), ElementList, GetInterfaceCustomizationByTypeId(InElementHandle.GetId().GetTypeId()))
		: FTypedElementSelectionSetElement();
}

void UTypedElementSelectionSet::OnElementListPreChange(const UTypedElementList* InElementList)
{
	check(InElementList == ElementList);
	OnPreChangeDelegate.Broadcast(this);

	if (!bIsRestoringState)
	{
		// Track the pre-change state for undo/redo
		Modify();
	}
}

void UTypedElementSelectionSet::OnElementListChanged(const UTypedElementList* InElementList)
{
	check(InElementList == ElementList);
	OnChangedDelegate.Broadcast(this);
}
