// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/TypedElementRegistry.h"

UTypedElementSelectionSet::UTypedElementSelectionSet()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ElementList = UTypedElementRegistry::GetInstance()->CreateElementList();
		ElementList->OnPreChange().AddUObject(this, &UTypedElementSelectionSet::OnElementListPreChange);
		ElementList->OnChanged().AddUObject(this, &UTypedElementSelectionSet::OnElementListChanged);
	}
}

bool UTypedElementSelectionSet::Modify(bool bAlwaysMarkDirty)
{
	if (GUndo && CanModify())
	{
		for (const TTypedElement<UTypedElementSelectionInterface>& SelectionElement : ElementList->IterateInterface<UTypedElementSelectionInterface>())
		{
			if (SelectionElement.ShouldPreventTransactions())
			{
				return false;
			}
		}

		return Super::Modify(bAlwaysMarkDirty);
	}

	return false;
}

void UTypedElementSelectionSet::Serialize(FArchive& Ar)
{
	checkf(!Ar.IsPersistent(), TEXT("UTypedElementSelectionSet can only be serialized by transient archives!"));

	if (Ar.IsSaving())
	{
		FTypedHandleTypeId ElementTypeId = 0;

		for (const TTypedElement<UTypedElementSelectionInterface>& SelectionElement : ElementList->IterateInterface<UTypedElementSelectionInterface>())
		{
			ElementTypeId = SelectionElement.GetId().GetTypeId();
			Ar << ElementTypeId;

			SelectionElement.WriteTransactedElement(Ar);
		}

		// End of the list
		ElementTypeId = 0;
		Ar << ElementTypeId;
	}
	else if (Ar.IsLoading())
	{
		TArray<FTypedElementHandle, TInlineAllocator<256>> SelectedElements;

		{
			UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

			FTypedHandleTypeId ElementTypeId = 0;
			for (;;)
			{
				Ar << ElementTypeId;
				if (!ElementTypeId)
				{
					// End of the list
					break;
				}

				UTypedElementSelectionInterface* ElementTypeSelectionInterface = Registry->GetElementInterface<UTypedElementSelectionInterface>(ElementTypeId);
				checkf(ElementTypeSelectionInterface, TEXT("Failed to find selection interface for a previously transacted element type!"));
				FTypedElementHandle SelectedElement = ElementTypeSelectionInterface->ReadTransactedElement(Ar);
				if (SelectedElement)
				{
					SelectedElements.Add(MoveTemp(SelectedElement));
				}
			}
		}

		{
			FTypedElementListLegacySyncScopedBatch LegacySyncBatch(ElementList, /*bNotify*/false);
			TGuardValue<bool> GuardIsRestoringFromTransaction(bIsRestoringFromTransaction, true);

			// TODO: Work out the intersection of the before and after state instead of clearing and reselecting?

			const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
				.SetAllowHidden(true)
				.SetAllowGroups(false)
				.SetWarnIfLocked(false);

			ClearSelection(SelectionOptions);
			SelectElements(SelectedElements, SelectionOptions);
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
		ElementsCopy.Reserve(ElementList->Num());
		for (const FTypedElementHandle& ElementHandle : *ElementList)
		{
			ElementsCopy.Emplace(ElementHandle);
		}

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

FTypedElementSelectionSetElement UTypedElementSelectionSet::ResolveSelectionSetElement(const FTypedElementHandle& InElementHandle) const
{
	return InElementHandle
		? FTypedElementSelectionSetElement(ElementList->GetElement<UTypedElementSelectionInterface>(InElementHandle), ElementList, GetAssetEditorCustomizationByTypeId(InElementHandle.GetId().GetTypeId()))
		: FTypedElementSelectionSetElement();
}

void UTypedElementSelectionSet::OnElementListPreChange(const UTypedElementList* InElementList)
{
	check(InElementList == ElementList);
	OnPreChangeDelegate.Broadcast(this);

	if (!bIsRestoringFromTransaction)
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
