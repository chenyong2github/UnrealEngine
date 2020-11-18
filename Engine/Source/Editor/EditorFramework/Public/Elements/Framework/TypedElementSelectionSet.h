// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/TypedElementListObjectUtil.h"
#include "Elements/Framework/TypedElementAssetEditorCustomization.h"
#include "Elements/Interfaces/TypedElementSelectionInterface.h"
#include "TypedElementSelectionSet.generated.h"

/**
 * Customization type used to allow asset editors (such as the level editor) to override the base behavior of element selection,
 * by injecting extra pre/post selection logic around the call into the selection interface for an element type.
 */
class EDITORFRAMEWORK_API FTypedElementAssetEditorSelectionCustomization
{
public:
	virtual ~FTypedElementAssetEditorSelectionCustomization() = default;

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
 * Utility to hold a typed element handle and its associated selection interface and asset editor selection customization.
 */
struct EDITORFRAMEWORK_API FTypedElementSelectionSetElement
{
public:
	FTypedElementSelectionSetElement() = default;

	FTypedElementSelectionSetElement(TTypedElement<UTypedElementSelectionInterface> InElementSelectionHandle, UTypedElementList* InElementList, FTypedElementAssetEditorSelectionCustomization* InAssetEditorSelectionCustomization)
		: ElementSelectionHandle(MoveTemp(InElementSelectionHandle))
		, ElementList(InElementList)
		, AssetEditorSelectionCustomization(InAssetEditorSelectionCustomization)
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
			&& AssetEditorSelectionCustomization;
	}

	//~ See UTypedElementSelectionInterface for API docs
	bool IsElementSelected(const FTypedElementIsSelectedOptions& InSelectionOptions) const { return AssetEditorSelectionCustomization->IsElementSelected(ElementSelectionHandle, ElementList, InSelectionOptions); }
	bool CanSelectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionCustomization->CanSelectElement(ElementSelectionHandle, InSelectionOptions); }
	bool CanDeselectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionCustomization->CanDeselectElement(ElementSelectionHandle, InSelectionOptions); }
	bool SelectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionCustomization->SelectElement(ElementSelectionHandle, ElementList, InSelectionOptions); }
	bool DeselectElement(const FTypedElementSelectionOptions& InSelectionOptions) const { return AssetEditorSelectionCustomization->DeselectElement(ElementSelectionHandle, ElementList, InSelectionOptions); }
	bool AllowSelectionModifiers() const { return AssetEditorSelectionCustomization->AllowSelectionModifiers(ElementSelectionHandle, ElementList); }
	FTypedElementHandle GetSelectionElement(const ETypedElementSelectionMethod InSelectionMethod) const { return AssetEditorSelectionCustomization->GetSelectionElement(ElementSelectionHandle, ElementList, InSelectionMethod); }

private:
	TTypedElement<UTypedElementSelectionInterface> ElementSelectionHandle;
	UTypedElementList* ElementList = nullptr;
	FTypedElementAssetEditorSelectionCustomization* AssetEditorSelectionCustomization = nullptr;
};

/**
 * A wrapper around an element list that ensures mutation goes via the selection 
 * interfaces, as well as providing some utilities for batching operations.
 */
UCLASS(Transient)
class EDITORFRAMEWORK_API UTypedElementSelectionSet : public UObject, public TTypedElementAssetEditorCustomizationRegistry<FTypedElementAssetEditorSelectionCustomization>
{
	GENERATED_BODY()

public:
	UTypedElementSelectionSet();

	//~ UObject interface
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
	virtual void Serialize(FArchive& Ar) override;

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
	 * Get the handle of every selected element, optionally filtering to elements that implement the given interface.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|Selection")
	TArray<FTypedElementHandle> GetSelectedElementHandles(const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const
	{
		return ElementList->GetElementHandles(InBaseInterfaceType);
	}

	/**
	 * Get the handle of every selected element, optionally filtering to elements that implement the given interface.
	 */
	template <typename ArrayAllocator>
	void GetSelectedElementHandles(TArray<FTypedElementHandle, ArrayAllocator>& OutArray, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const
	{
		ElementList->GetElementHandles(OutArray, InBaseInterfaceType);
	}

	/**
	 * Enumerate the handle of every selected element, optionally filtering to elements that implement the given interface.
	 * @note Return true from the callback to continue enumeration.
	 */
	void ForEachSelectedElementHandle(TFunctionRef<bool(const FTypedElementHandle&)> InCallback, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const
	{
		ElementList->ForEachElementHandle(InCallback, InBaseInterfaceType);
	}

	/**
	 * Enumerate the selected elements that implement the given interface.
	 * @note Return true from the callback to continue enumeration.
	 */
	template <typename BaseInterfaceType>
	void ForEachSelectedElement(TFunctionRef<bool(const TTypedElement<BaseInterfaceType>&)> InCallback) const
	{
		ElementList->ForEachElement<BaseInterfaceType>(InCallback);
	}

	/**
	 * Get the first selected element implementing the given interface.
	 */
	template <typename BaseInterfaceType>
	TTypedElement<BaseInterfaceType> GetTopSelectedElement() const
	{
		return ElementList->GetTopElement<BaseInterfaceType>();
	}

	/**
	 * Get the last selected element implementing the given interface.
	 */
	template <typename BaseInterfaceType>
	TTypedElement<BaseInterfaceType> GetBottomSelectedElement() const
	{
		return ElementList->GetBottomElement<BaseInterfaceType>();
	}

	/**
	 * Get the array of selected objects from the currently selected elements.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|Selection")
	TArray<UObject*> GetSelectedObjects(const UClass* InRequiredClass = nullptr) const
	{
		return TypedElementListObjectUtil::GetObjects(ElementList, InRequiredClass);
	}

	/**
	 * Get the array of selected objects from the currently selected elements.
	 */
	template <typename RequiredClassType>
	TArray<RequiredClassType*> GetSelectedObjects() const
	{
		return TypedElementListObjectUtil::GetObjects<RequiredClassType>(ElementList);
	}

	/**
	 * Enumerate the selected objects from the currently selected elements.
	 * @note Return true from the callback to continue enumeration.
	 */
	void ForEachSelectedObject(TFunctionRef<bool(UObject*)> InCallback, const UClass* InRequiredClass = nullptr) const
	{
		TypedElementListObjectUtil::ForEachObject(ElementList, InCallback, InRequiredClass);
	}

	/**
	 * Enumerate the selected objects from the currently selected elements.
	 * @note Return true from the callback to continue enumeration.
	 */
	template <typename RequiredClassType>
	void ForEachSelectedObject(TFunctionRef<bool(RequiredClassType*)> InCallback) const
	{
		TypedElementListObjectUtil::ForEachObject<RequiredClassType>(ElementList, InCallback);
	}

	/**
	 * Get the first selected object of the given type.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection")
	UObject* GetTopSelectedObject(const UClass* InRequiredClass = nullptr) const
	{
		return TypedElementListObjectUtil::GetTopObject(ElementList, InRequiredClass);
	}

	/**
	 * Get the first selected object of the given type.
	 */
	template <typename RequiredClassType>
	RequiredClassType* GetTopSelectedObject() const
	{
		return TypedElementListObjectUtil::GetTopObject<RequiredClassType>(ElementList);
	}

	/**
	 * Get the last selected object of the given type.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection")
	UObject* GetBottomSelectedObject(const UClass* InRequiredClass = nullptr) const
	{
		return TypedElementListObjectUtil::GetBottomObject(ElementList, InRequiredClass);
	}

	/**
	 * Get the last selected object of the given type.
	 */
	template <typename RequiredClassType>
	RequiredClassType* GetBottomSelectedObject() const
	{
		return TypedElementListObjectUtil::GetBottomObject<RequiredClassType>(ElementList);
	}

	/**
	 * Access the delegate that is invoked whenever this element list is potentially about to change.
	 * @note This may be called even if no actual change happens, though once a change does happen it won't be called again until after the next call to NotifyPendingChanges.
	 */
	DECLARE_EVENT_OneParam(UTypedElementSelectionSet, FOnPreChange, const UTypedElementSelectionSet* /*InElementSelectionSet*/);
	FOnPreChange& OnPreChange()
	{
		return OnPreChangeDelegate;
	}

	/**
	 * Access the delegate that is invoked whenever the underlying element list has been changed.
	 * @note This is called automatically at the end of each frame, but can also be manually invoked by NotifyPendingChanges.
	 */
	DECLARE_EVENT_OneParam(UTypedElementSelectionSet, FOnChanged, const UTypedElementSelectionSet* /*InElementSelectionSet*/);
	FOnChanged& OnChanged()
	{
		return OnChangedDelegate;
	}

	/**
	 * Invoke the delegate called whenever the underlying element list has been changed.
	 */
	void NotifyPendingChanges()
	{
		ElementList->NotifyPendingChanges();
	}

	/**
	 * Clear whether there are pending changes for OnChangedDelegate to notify for, without emitting a notification.
	 */
	void ClearPendingChanges()
	{
		ElementList->ClearPendingChanges();
	}

	/**
	 * Access the interface to allow external systems (such as USelection) to receive immediate sync notifications as the underlying element list is changed.
	 * This exists purely as a bridging mechanism and shouldn't be relied on for new code. It is lazily created as needed.
	 */
	FTypedElementListLegacySync& Legacy_GetElementListSync()
	{
		return ElementList->Legacy_GetSync();
	}

	/**
	 * Access the interface to allow external systems (such as USelection) to receive immediate sync notifications as the underlying element list is changed.
	 * This exists purely as a bridging mechanism and shouldn't be relied on for new code. This will return null if no legacy sync has been created for this instance.
	 */
	FTypedElementListLegacySync* Legacy_GetElementListSyncPtr()
	{
		return ElementList->Legacy_GetSyncPtr();
	}

	/**
	 * Get the underlying element list holding the selection state.
	 */
	const UTypedElementList* GetElementList() const
	{
		return ElementList;
	}

private:
	/**
	 * Attempt to resolve the selection interface and asset editor selection customization for the given element, if any.
	 */
	FTypedElementSelectionSetElement ResolveSelectionSetElement(const FTypedElementHandle& InElementHandle) const;

	/**
	 * Proxy the internal OnPreChange event from the underlying element list.
	 */
	void OnElementListPreChange(const UTypedElementList* InElementList);

	/**
	 * Proxy the internal OnChanged event from the underlying element list.
	 */
	void OnElementListChanged(const UTypedElementList* InElementList);

	/** Underlying element list holding the selection state. */
	UPROPERTY()
	UTypedElementList* ElementList = nullptr;

	/** Delegate that is invoked whenever the underlying element list is potentially about to change. */
	FOnPreChange OnPreChangeDelegate;

	/** Delegate that is invoked whenever the underlying element list has been changed. */
	FOnChanged OnChangedDelegate;

	/** Set when we are currently restoring the selection state from a transaction (undo/redo) */
	bool bIsRestoringFromTransaction = false;
};
