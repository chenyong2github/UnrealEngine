// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementHandle.h"
#include "TypedElementSelectionInterface.generated.h"

class UTypedElementList;

UENUM()
enum class ETypedElementSelectionMethod : uint8
{
	/** Select the "primary" element (eg, a component favor selecting its owner actor) */
	Primary,
	/** Select the "secondary" element (eg, a component would favor selecting itself) */
	Secondary,
};

USTRUCT(BlueprintType)
struct FTypedElementIsSelectedOptions
{
	GENERATED_BODY()

public:
	FTypedElementIsSelectedOptions& SetAllowIndirect(const bool InAllowIndirect) { bAllowIndirect = InAllowIndirect; return *this; }
	bool AllowIndirect() const { return bAllowIndirect; }
	
private:
	UPROPERTY(BlueprintReadWrite, Category="TypedElementInterfaces|Selection|IsSelectedOptions", meta=(AllowPrivateAccess=true))
	bool bAllowIndirect = false;
};

USTRUCT(BlueprintType)
struct FTypedElementSelectionOptions
{
	GENERATED_BODY()

public:
	FTypedElementSelectionOptions& SetAllowHidden(const bool InAllowHidden) { bAllowHidden = InAllowHidden; return *this; }
	bool AllowHidden() const { return bAllowHidden; }

	FTypedElementSelectionOptions& SetAllowGroups(const bool InAllowGroups) { bAllowGroups = InAllowGroups; return *this; }
	bool AllowGroups() const { return bAllowGroups; }

	FTypedElementSelectionOptions& SetAllowLegacyNotifications(const bool InAllowLegacyNotifications) { bAllowLegacyNotifications = InAllowLegacyNotifications; return *this; }
	bool AllowLegacyNotifications() const { return bAllowLegacyNotifications; }

	FTypedElementSelectionOptions& SetWarnIfLocked(const bool InWarnIfLocked) { bWarnIfLocked = InWarnIfLocked; return *this; }
	bool WarnIfLocked() const { return bWarnIfLocked; }
	
private:
	UPROPERTY(BlueprintReadWrite, Category="TypedElementInterfaces|Selection|SelectionOptions", meta=(AllowPrivateAccess=true))
	bool bAllowHidden = false;

	UPROPERTY(BlueprintReadWrite, Category="TypedElementInterfaces|Selection|SelectionOptions", meta=(AllowPrivateAccess=true))
	bool bAllowGroups = true;

	UPROPERTY(BlueprintReadWrite, Category="TypedElementInterfaces|Selection|SelectionOptions", meta=(AllowPrivateAccess=true))
	bool bAllowLegacyNotifications = true;

	UPROPERTY(BlueprintReadWrite, Category="TypedElementInterfaces|Selection|SelectionOptions", meta=(AllowPrivateAccess=true))
	bool bWarnIfLocked = false;
};

UCLASS(Abstract)
class TYPEDELEMENTINTERFACES_API UTypedElementSelectionInterface : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	/**
	 * Test to see whether the given element is currently considered selected.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Selection")
	virtual bool IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions);

	/**
	 * Test to see whether the given element can be selected.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Selection")
	virtual bool CanSelectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions& InSelectionOptions) { return true; }

	/**
	 * Test to see whether the given element can be deselected.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Selection")
	virtual bool CanDeselectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions& InSelectionOptions) { return true; }

	/**
	 * Attempt to select the given element.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|Selection")
	virtual bool SelectElement(const FTypedElementHandle& InElementHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions);

	/**
	 * Attempt to deselect the given element.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|Selection")
	virtual bool DeselectElement(const FTypedElementHandle& InElementHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions);

	/**
	 * Test to see whether selection modifiers (Ctrl or Shift) are allowed while selecting this element.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Selection")
	virtual bool AllowSelectionModifiers(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet) { return true; }

	/**
	 * Given an element, return the element that should actually perform a selection operation.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Selection")
	virtual FTypedElementHandle GetSelectionElement(const FTypedElementHandle& InElementHandle, const UTypedElementList* InCurrentSelection, const ETypedElementSelectionMethod InSelectionMethod) { return InElementHandle; }

	/**
	 * Test to see whether the given element prevents the selection set state from being transacted for undo/redo (eg, if the element belongs to a PIE instance).
	 */
	virtual bool ShouldPreventTransactions(const FTypedElementHandle& InElementHandle) { return false; }

	/**
	 * Write the information needed to find the given element again when this transaction is replayed for undo/redo.
	 * @note The data written must match the data read by ReadTransactedElement.
	 */
	virtual void WriteTransactedElement(const FTypedElementHandle& InElementHandle, FArchive& InArchive) {}

	/**
	 * Read the information needed to be find a previously tracked element, and attempt to get it now that this transaction is being replayed for undo/redo.
	 * @note The data read must match the data written by WriteTransactedElement.
	 */
	virtual FTypedElementHandle ReadTransactedElement(FArchive& InArchive) { return FTypedElementHandle(); }
};

template <>
struct TTypedElement<UTypedElementSelectionInterface> : public TTypedElementBase<UTypedElementSelectionInterface>
{
	bool IsElementSelected(const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions) const { return InterfacePtr->IsElementSelected(*this, InSelectionSet, InSelectionOptions); }
	bool CanSelectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return InterfacePtr->CanSelectElement(*this, InSelectionOptions); }
	bool CanDeselectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return InterfacePtr->CanDeselectElement(*this, InSelectionOptions); }
	bool SelectElement(UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions) const { return InterfacePtr->SelectElement(*this, InSelectionSet, InSelectionOptions); }
	bool DeselectElement(UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions) const { return InterfacePtr->DeselectElement(*this, InSelectionSet, InSelectionOptions); }
	bool AllowSelectionModifiers(const UTypedElementList* InSelectionSet) const { return InterfacePtr->AllowSelectionModifiers(*this, InSelectionSet); }
	FTypedElementHandle GetSelectionElement(const UTypedElementList* InCurrentSelection, const ETypedElementSelectionMethod InSelectionMethod) const { return InterfacePtr->GetSelectionElement(*this, InCurrentSelection, InSelectionMethod); }
	bool ShouldPreventTransactions() const { return InterfacePtr->ShouldPreventTransactions(*this); }
	void WriteTransactedElement(FArchive& InArchive) const { return InterfacePtr->WriteTransactedElement(*this, InArchive); }
	FTypedElementHandle ReadTransactedElement(FArchive& InArchive) const { return InterfacePtr->ReadTransactedElement(InArchive); }
};
