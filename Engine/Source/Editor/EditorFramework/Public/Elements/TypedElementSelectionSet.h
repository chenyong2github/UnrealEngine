// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementSelectionInterface.h"
#include "TypedElementSelectionSet.generated.h"

class UTypedElementList;

/**
 * Proxy type used to allow asset editors (such as the level editor) to override the base behavior of element selection,
 * by injecting extra pre/post selection logic around the call into the selection interface for an element type.
 */
UCLASS()
class EDITORFRAMEWORK_API UTypedElementAssetEditorSelectionProxy : public UObject
{
	GENERATED_BODY()

public:
	//~ See UTypedElementSelectionInterface for API docs
	virtual bool IsElementSelected(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions) { return InElementSelectionHandle.IsElementSelected(InSelectionSet, InSelectionOptions); }
	virtual bool CanSelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions) { return InElementSelectionHandle.CanSelectElement(InSelectionOptions); }
	virtual bool CanDeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions) { return InElementSelectionHandle.CanDeselectElement(InSelectionOptions); }
	virtual bool SelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions) { return InElementSelectionHandle.SelectElement(InSelectionSet, InSelectionOptions); }
	virtual bool DeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions) { return InElementSelectionHandle.DeselectElement(InSelectionSet, InSelectionOptions); }
	virtual bool AllowSelectionModifiers(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const UTypedElementList* InSelectionSet) { return InElementSelectionHandle.AllowSelectionModifiers(InSelectionSet); }
	virtual FTypedElementHandle GetSelectionElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const UTypedElementList* InCurrentSelection, const ETypedElementSelectionMethod InSelectionMethod) { return InElementSelectionHandle.GetSelectionElement(InCurrentSelection, InSelectionMethod); }
};

/**
 * Utility to hold a typed element handle and its associated selection interface and asset editor selection proxy.
 */
struct EDITORFRAMEWORK_API FTypedElementSelectionSetElement
{
public:
	FTypedElementSelectionSetElement() = default;

	FTypedElementSelectionSetElement(TTypedElement<UTypedElementSelectionInterface> InElementSelectionHandle, UTypedElementList* InElementList, UTypedElementAssetEditorSelectionProxy* InAssetEditorSelectionProxy)
		: ElementSelectionHandle(MoveTemp(InElementSelectionHandle))
		, ElementList(InElementList)
		, AssetEditorSelectionProxy(InAssetEditorSelectionProxy)
	{
	}

	FTypedElementSelectionSetElement(const FTypedElementSelectionSetElement&) = default;
	FTypedElementSelectionSetElement& operator=(const FTypedElementSelectionSetElement&) = default;

	FTypedElementSelectionSetElement(FTypedElementSelectionSetElement&&) = default;
	FTypedElementSelectionSetElement& operator=(FTypedElementSelectionSetElement&&) = default;

	FORCEINLINE explicit operator bool() const
	{
		return IsSet();
	}

	FORCEINLINE bool IsSet() const
	{
		return ElementSelectionHandle.IsSet()
			&& ElementList
			&& AssetEditorSelectionProxy;
	}

	//~ See UTypedElementSelectionInterface for API docs
	bool IsElementSelected(const FTypedElementIsSelectedOptions& InSelectionOptions) const { return AssetEditorSelectionProxy->IsElementSelected(ElementSelectionHandle, ElementList, InSelectionOptions); }
	bool CanSelectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionProxy->CanSelectElement(ElementSelectionHandle, InSelectionOptions); }
	bool CanDeselectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionProxy->CanDeselectElement(ElementSelectionHandle, InSelectionOptions); }
	bool SelectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionProxy->SelectElement(ElementSelectionHandle, ElementList, InSelectionOptions); }
	bool DeselectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionProxy->DeselectElement(ElementSelectionHandle, ElementList, InSelectionOptions); }
	bool AllowSelectionModifiers() const { return AssetEditorSelectionProxy->AllowSelectionModifiers(ElementSelectionHandle, ElementList); }
	FTypedElementHandle GetSelectionElement(const ETypedElementSelectionMethod InSelectionMethod) const { return AssetEditorSelectionProxy->GetSelectionElement(ElementSelectionHandle, ElementList, InSelectionMethod); }

private:
	TTypedElement<UTypedElementSelectionInterface> ElementSelectionHandle;
	UTypedElementList* ElementList = nullptr;
	UTypedElementAssetEditorSelectionProxy* AssetEditorSelectionProxy = nullptr;
};

/**
 * A wrapper around an element list that ensures mutation goes via the selection 
 * interfaces, as well as providing some utilities for batching operations.
 */
UCLASS(Transient)
class EDITORFRAMEWORK_API UTypedElementSelectionSet : public UObject
{
	GENERATED_BODY()

public:
	UTypedElementSelectionSet();

	/**
	 * Test to see whether the given element is currently considered selected.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection")
	bool IsElementSelected(const FTypedElementHandle& InElementHandle, const FTypedElementIsSelectedOptions InSelectionOptions) const;

	/**
	 * Test to see whether the given element can be selected.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection")
	bool CanSelectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions) const;

	/**
	 * Test to see whether the given element can be deselected.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection")
	bool CanDeselectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions) const;

	/**
	 * Attempt to select the given element.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection")
	bool SelectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions);

	/**
	 * Attempt to select the given elements.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection")
	bool SelectElements(const TArray<FTypedElementHandle>& InElementHandles, const FTypedElementSelectionOptions InSelectionOptions);
	bool SelectElements(TArrayView<const FTypedElementHandle> InElementHandles, const FTypedElementSelectionOptions InSelectionOptions);

	/**
	 * Attempt to deselect the given element.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection")
	bool DeselectElement(const FTypedElementHandle& InElementHandle, const FTypedElementSelectionOptions InSelectionOptions);

	/**
	 * Attempt to deselect the given elements.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection")
	bool DeselectElements(const TArray<FTypedElementHandle>& InElementHandles, const FTypedElementSelectionOptions InSelectionOptions);
	bool DeselectElements(TArrayView<const FTypedElementHandle> InElementHandles, const FTypedElementSelectionOptions InSelectionOptions);

	/**
	 * Clear the current selection.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection")
	bool ClearSelection(const FTypedElementSelectionOptions InSelectionOptions);

	/**
	 * Test to see whether selection modifiers (Ctrl or Shift) are allowed while selecting this element.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection")
	bool AllowSelectionModifiers(const FTypedElementHandle& InElementHandle) const;

	/**
	 * Given an element, return the element that should actually perform a selection operation.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection")
	FTypedElementHandle GetSelectionElement(const FTypedElementHandle& InElementHandle, const ETypedElementSelectionMethod InSelectionMethod) const;

	/**
	 * Get the underlying element list holding the selection state.
	 */
	const UTypedElementList* GetElementList() const
	{
		return ElementList;
	}

	/**
	 * Get the underlying element list holding the selection state.
	 * @note Mutating the internal element list directly is a bad idea.
	 */
	UTypedElementList* GetMutableElementList()
	{
		return ElementList;
	}

	/**
	 * Register an asset editor selection proxy for the given named element type.
	 */
	void RegisterAssetEditorSelectionProxy(const FName InElementTypeName, UTypedElementAssetEditorSelectionProxy* InAssetEditorSelectionProxy);

private:
	/**
	 * Attempt to resolve the selection interface and asset editor selection proxy for the given element, if any.
	 */
	FTypedElementSelectionSetElement ResolveSelectionSetElement(const FTypedElementHandle& InElementHandle) const;

	/** Underlying element list holding the selection state. */
	UPROPERTY()
	UTypedElementList* ElementList = nullptr;

	/** Array of registered asset editor selection proxies, indexed by ElementTypeId-1. */
	UPROPERTY()
	UTypedElementAssetEditorSelectionProxy* RegisteredAssetEditorSelectionProxies[TypedHandleMaxTypeId - 1];
};
