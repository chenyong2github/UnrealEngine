// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Templates/SubclassOf.h"
#include "Containers/ArrayView.h"
#include "Templates/UniquePtr.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "TypedElementList.generated.h"

class UTypedElementList;
class UTypedElementRegistry;

namespace TypedElementList_Private
{

TYPEDELEMENTFRAMEWORK_API void GetElementImpl(const UTypedElementRegistry* InRegistry, const FTypedElementHandle& InElementHandle, const UClass* InBaseInterfaceType, FTypedElement& OutElement);

template <typename BaseInterfaceType>
FORCEINLINE void GetElement(const UTypedElementRegistry* InRegistry, const FTypedElementHandle& InElementHandle, TTypedElement<BaseInterfaceType>& OutElement)
{
	static_assert(sizeof(TTypedElement<BaseInterfaceType>) == sizeof(FTypedElement), "All TTypedElement instances must be the same size for this cast implementation to work!");
	GetElementImpl(InRegistry, InElementHandle, BaseInterfaceType::StaticClass(), reinterpret_cast<FTypedElement&>(OutElement));
}

template <typename BaseInterfaceType>
FORCEINLINE TTypedElement<BaseInterfaceType> GetElement(const UTypedElementRegistry* InRegistry, const FTypedElementHandle& InElementHandle)
{
	TTypedElement<BaseInterfaceType> Element;
	GetElement(InRegistry, InElementHandle, Element);
	return Element;
}

} // namespace TypedElementList_Private

/**
 * Interface to allow external systems (such as USelection) to receive immediate sync notifications as an element list is changed.
 * This exists purely as a bridging mechanism and shouldn't be relied on for new code. It is lazily created as needed.
 */
class TYPEDELEMENTFRAMEWORK_API FTypedElementListLegacySync
{
public:
	enum class ESyncType : uint8
	{
		/**
		 * An element was added to the element list.
		 * The ElementHandle argument will be set to the element that was added.
		 */
		Added,

		/**
		 * An element was removed from the element list.
		 * The ElementHandle argument will be set to the element that was removed.
		 */
		Removed,

		/**
		 * The element list was modified in an unknown way.
		 * The ElementHandle argument will be unset.
		 */
		Modified,

		/**
		 * The element list was cleared.
		 * The ElementHandle argument will be unset.
		 */
		Cleared,

		/**
		 * The element list was modified as part of a batch or bulk operation.
		 * The ElementHandle argument will be unset.
		 * @note A batch operation will emit internal (bIsWithinBatchOperation=true) Added, Removed, Modified and Cleared updates during the batch, 
		 *       so if you respond to those internal updates you may choose to ignore this one. Otherwise you should treat it the same as Modified.
		 */
		BatchComplete,
	};
	
	FTypedElementListLegacySync(const UTypedElementList* InElementList);

	void Private_EmitSyncEvent(const ESyncType InSyncType, const FTypedElementHandle& InElementHandle = FTypedElementHandle());

	DECLARE_EVENT_FourParams(FTypedElementListLegacySync, FOnSyncEvent, const UTypedElementList* /*InElementList*/, ESyncType /*InSyncType*/, const FTypedElementHandle& /*InElementHandle*/, bool /*bIsWithinBatchOperation*/);
	FOnSyncEvent& OnSyncEvent();

	bool IsRunningBatchOperation() const;
	void BeginBatchOperation();
	void EndBatchOperation(const bool InNotify = true);
	bool IsBatchOperationDirty() const;
	void ForceBatchOperationDirty();

private:
	const UTypedElementList* ElementList;

	FOnSyncEvent OnSyncEventDelegate;

	int32 NumOpenBatchOperations = 0;
	bool bBatchOperationIsDirty = false;
};

/**
 * Helper to batch immediate sync notifications for legacy code.
 * Does nothing if no legacy sync has been created for the given instance.
 */
class TYPEDELEMENTFRAMEWORK_API FTypedElementListLegacySyncScopedBatch
{
public:
	explicit FTypedElementListLegacySyncScopedBatch(const UTypedElementList* InElementList, const bool InNotify = true);
	~FTypedElementListLegacySyncScopedBatch();

	FTypedElementListLegacySyncScopedBatch(const FTypedElementListLegacySyncScopedBatch&) = delete;
	FTypedElementListLegacySyncScopedBatch& operator=(const FTypedElementListLegacySyncScopedBatch&) = delete;

	FTypedElementListLegacySyncScopedBatch(FTypedElementListLegacySyncScopedBatch&&) = delete;
	FTypedElementListLegacySyncScopedBatch& operator=(FTypedElementListLegacySyncScopedBatch&&) = delete;

	bool IsDirty() const;
	void ForceDirty();

private:
	FTypedElementListLegacySync* ElementListLegacySync = nullptr;
	bool bNotify = true;
};

/**
 * A list of element handles.
 * Provides high-level access to groups of elements, including accessing elements that implement specific interfaces.
 */
UCLASS(Transient)
class TYPEDELEMENTFRAMEWORK_API UTypedElementList : public UObject
{
	GENERATED_BODY()

public:
	//~ UObject interface
	virtual void BeginDestroy() override;

	/**
	 * Internal function used by the element registry to create an element list instance.
	 */
	static UTypedElementList* Private_CreateElementList(UTypedElementRegistry* InRegistry);

	/**
	 * Clone this list instance.
	 * @note Only copies elements; does not copy any bindings!
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|List")
	UTypedElementList* Clone() const;

	/**
	 * Get the element handle at the given index.
	 * @note Use IsValidIndex to test for validity.
	 */
	FORCEINLINE FTypedElementHandle operator[](const int32 InIndex) const
	{
		return GetElementHandleAt(InIndex);
	}

	/**
	 * Get the element handle at the given index.
	 * @note Use IsValidIndex to test for validity.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|List")
	FORCEINLINE FTypedElementHandle GetElementHandleAt(const int32 InIndex) const
	{
		return ElementHandles[InIndex];
	}

	/**
	 * Get the element at the given index.
	 * @note Use IsValidIndex to test for validity.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE TTypedElement<BaseInterfaceType> GetElementAt(const int32 InIndex) const
	{
		return GetElement<BaseInterfaceType>(GetElementHandleAt(InIndex));
	}

	/**
	 * Get the element at the given index.
	 * @note Use IsValidIndex to test for validity.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE void GetElementAt(const int32 InIndex, TTypedElement<BaseInterfaceType>& OutElement) const
	{
		GetElement(GetElementHandleAt(InIndex), OutElement);
	}

	/**
	 * Get the element from the given handle.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE TTypedElement<BaseInterfaceType> GetElement(const FTypedElementHandle& InElementHandle) const
	{
		return TypedElementList_Private::GetElement<BaseInterfaceType>(Registry.Get(), InElementHandle);
	}

	/**
	 * Get the element from the given handle.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE void GetElement(const FTypedElementHandle& InElementHandle, TTypedElement<BaseInterfaceType>& OutElement) const
	{
		TypedElementList_Private::GetElement(Registry.Get(), InElementHandle, OutElement);
	}

	/**
	 * Get the first element implementing the given interface.
	 */
	template <typename BaseInterfaceType>
	TTypedElement<BaseInterfaceType> GetTopElement() const
	{
		TTypedElement<BaseInterfaceType> TempElement;
		for (int32 ElementIndex = 0; ElementIndex < Num(); ++ElementIndex)
		{
			GetElementAt(ElementIndex, TempElement);
			if (TempElement)
			{
				break;
			}
		}
		return TempElement;
	}

	/**
	 * Get the last element implementing the given interface.
	 */
	template <typename BaseInterfaceType>
	TTypedElement<BaseInterfaceType> GetBottomElement() const
	{
		TTypedElement<BaseInterfaceType> TempElement;
		for (int32 ElementIndex = Num() - 1; ElementIndex >= 0; --ElementIndex)
		{
			GetElementAt(ElementIndex, TempElement);
			if (TempElement)
			{
				break;
			}
		}
		return TempElement;
	}

	/**
	 * Get the element interface from the given handle.
	 */
	template <typename BaseInterfaceType>
	BaseInterfaceType* GetElementInterface(const FTypedElementHandle& InElementHandle) const
	{
		return static_cast<BaseInterfaceType*>(GetElementInterface(InElementHandle, BaseInterfaceType::StaticClass()));
	}

	/**
	 * Get the element interface from the given handle.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|List")
	UTypedElementInterface* GetElementInterface(const FTypedElementHandle& InElementHandle, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const;

	/**
	 * Test whether there are elements in this list, optionally filtering to elements that implement the given interface.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|List")
	bool HasElements(const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const;

	/**
	 * Count the number of elements in this list, optionally filtering to elements that implement the given interface.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|List")
	int32 CountElements(const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const;

	/**
	 * Get the handle of every element in this list, optionally filtering to elements that implement the given interface.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="TypedElementFramework|List")
	TArray<FTypedElementHandle> GetElementHandles(const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const;

	/**
	 * Get the handle of every element in this list, optionally filtering to elements that implement the given interface.
	 */
	template <typename ArrayAllocator>
	void GetElementHandles(TArray<FTypedElementHandle, ArrayAllocator>& OutArray, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const
	{
		OutArray.Reset();
		OutArray.Reserve(ElementHandles.Num());
		ForEachElementHandle([&OutArray](const FTypedElementHandle& InElementHandle)
		{
			OutArray.Add(InElementHandle);
			return true;
		}, InBaseInterfaceType);
	}

	/**
	 * Enumerate the handle of every element in this list, optionally filtering to elements that implement the given interface.
	 * @note Return true from the callback to continue enumeration.
	 */
	void ForEachElementHandle(TFunctionRef<bool(const FTypedElementHandle&)> InCallback, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType = nullptr) const;

	/**
	 * Enumerate the elements in this list that implement the given interface.
	 * @note Return true from the callback to continue enumeration.
	 */
	template <typename BaseInterfaceType>
	void ForEachElement(TFunctionRef<bool(const TTypedElement<BaseInterfaceType>&)> InCallback) const
	{
		TTypedElement<BaseInterfaceType> TempElement;
		for (const FTypedElementHandle& ElementHandle : ElementHandles)
		{
			GetElement(ElementHandle, TempElement);
			if (TempElement && !InCallback(TempElement))
			{
				break;
			}
		}
	}

	/**
	 * Is the given index a valid entry within this element list?
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|List")
	FORCEINLINE bool IsValidIndex(const int32 InIndex) const
	{
		return ElementHandles.IsValidIndex(InIndex);
	}

	/**
	 * Get the number of entries within this element list.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|List")
	FORCEINLINE int32 Num() const
	{
		return ElementHandles.Num();
	}

	/**
	 * Shrink this element list storage to avoid slack.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|List")
	FORCEINLINE void Shrink()
	{
		ElementCombinedIds.Shrink();
		ElementHandles.Shrink();
	}

	/**
	 * Pre-allocate enough memory in this element list to store the given number of entries.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|List")
	FORCEINLINE void Reserve(const int32 InSize)
	{
		ElementCombinedIds.Reserve(InSize);
		ElementHandles.Reserve(InSize);
	}

	/**
	 * Remove all entries from this element list, potentially leaving space allocated for the given number of entries.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|List")
	FORCEINLINE void Empty(const int32 InSlack = 0)
	{
		NoteListMayChange();
		ElementCombinedIds.Empty(InSlack);
		ElementHandles.Empty(InSlack);
		NoteListChanged(EChangeType::Cleared);
	}

	/**
	 * Remove all entries from this element list, preserving existing allocations.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|List")
	FORCEINLINE void Reset()
	{
		NoteListMayChange();
		ElementCombinedIds.Reset();
		ElementHandles.Reset();
		NoteListChanged(EChangeType::Cleared);
	}

	/**
	 * Does this element list contain an entry for the given element ID?
	 */
	FORCEINLINE bool Contains(const FTypedElementId& InElementId) const
	{
		return ContainsElementImpl(InElementId);
	}

	/**
	 * Does this element list contain an entry for the given element handle?
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|List")
	FORCEINLINE bool Contains(const FTypedElementHandle& InElementHandle) const
	{
		return ContainsElementImpl(InElementHandle.GetId());
	}

	/**
	 * Does this element list contain an entry for the given element owner?
	 */
	template <typename ElementDataType>
	FORCEINLINE bool Contains(const TTypedElementOwner<ElementDataType>& InElementOwner)
	{
		return ContainsElementImpl(InElementOwner.GetId());
	}

	/**
	 * Add the given element handle to this element list, if it isn't already in the list.
	 * @return True if the element handle was added, false if it is already in the list.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|List")
	FORCEINLINE bool Add(const FTypedElementHandle& InElementHandle)
	{
		return AddElementImpl(CopyTemp(InElementHandle));
	}

	/**
	 * Add the given element handle to this element list, if it isn't already in the list.
	 * @return True if the element handle was added, false if it is already in the list.
	 */
	FORCEINLINE bool Add(FTypedElementHandle&& InElementHandle)
	{
		return AddElementImpl(MoveTemp(InElementHandle));
	}

	/**
	 * Add the given element owner to this element list, if it isn't already in the list.
	 * @return True if the element owner was added, false if it is already in the list.
	 */
	template <typename ElementDataType>
	FORCEINLINE bool Add(const TTypedElementOwner<ElementDataType>& InElementOwner)
	{
		return AddElementImpl(InElementOwner.AcquireHandle());
	}

	/**
	 * Append the given element handles to this element list, for any that already in the list.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|List")
	FORCEINLINE void Append(const TArray<FTypedElementHandle>& InElementHandles)
	{
		Append(MakeArrayView(InElementHandles));
	}
	
	/**
	 * Append the given element handles to this element list, for any that already in the list.
	 */
	void Append(TArrayView<const FTypedElementHandle> InElementHandles)
	{
		FTypedElementListLegacySyncScopedBatch LegacySyncBatch(this);

		Reserve(Num() + InElementHandles.Num());
		for (const FTypedElementHandle& ElementHandle : InElementHandles)
		{
			AddElementImpl(CopyTemp(ElementHandle));
		}
	}

	/**
	 * Append the given element owners to this element list, for any that already in the list.
	 */
	template <typename ElementDataType>
	FORCEINLINE void Append(const TArray<TTypedElementOwner<ElementDataType>>& InElementOwners)
	{
		Append(MakeArrayView(InElementOwners));
	}

	/**
	 * Append the given element owners to this element list, for any that already in the list.
	 */
	template <typename ElementDataType>
	void Append(TArrayView<const TTypedElementOwner<ElementDataType>> InElementOwners)
	{
		FTypedElementListLegacySyncScopedBatch LegacySyncBatch(this);

		Reserve(Num() + InElementOwners.Num());
		for (const TTypedElementOwner<ElementDataType>& ElementOwner : InElementOwners)
		{
			AddElementImpl(ElementOwner.AcquireHandle());
		}
	}

	/**
	 * Remove the given element ID from this element list, if it is in the list.
	 * @return True if the element ID was removed, false if it isn't in the list.
	 */
	FORCEINLINE bool Remove(const FTypedElementId& InElementId)
	{
		return RemoveElementImpl(InElementId);
	}

	/**
	 * Remove the given element handle from this element list, if it is in the list.
	 * @return True if the element handle was removed, false if it isn't in the list.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|List")
	FORCEINLINE bool Remove(const FTypedElementHandle& InElementHandle)
	{
		return RemoveElementImpl(InElementHandle.GetId());
	}

	/**
	 * Remove the given element owner from this element list, if it is in the list.
	 * @return True if the element owner was removed, false if it isn't in the list.
	 */
	template <typename ElementDataType>
	FORCEINLINE bool Remove(const TTypedElementOwner<ElementDataType>& InElementOwner)
	{
		return RemoveElementImpl(InElementOwner.GetId());
	}

	/**
	 * Remove any element handles that match the given predicate from this element list.
	 * @return The number of element handles removed.
	 */
	FORCEINLINE int32 RemoveAll(TFunctionRef<bool(const FTypedElementHandle&)> InPredicate)
	{
		return RemoveAllElementsImpl(InPredicate);
	}

	/**
	 * Remove any elements that match the given predicate from this element list.
	 * @return The number of elements removed.
	 */
	template <typename BaseInterfaceType>
	int32 RemoveAll(TFunctionRef<bool(const TTypedElement<BaseInterfaceType>&)> InPredicate)
	{
		TTypedElement<BaseInterfaceType> TempElement;
		return RemoveAllElementsImpl([this, &TempElement, &InPredicate](const FTypedElementHandle& InElementHandle)
		{
			GetElement(InElementHandle, TempElement);
			return TempElement && InPredicate(TempElement);
		});
	}

	/**
	 * Access the delegate that is invoked whenever this element list is potentially about to change.
	 * @note This may be called even if no actual change happens, though once a change does happen it won't be called again until after the next call to NotifyPendingChanges.
	 */
	DECLARE_EVENT_OneParam(UTypedElementList, FOnPreChange, const UTypedElementList* /*InElementList*/);
	FOnPreChange& OnPreChange()
	{
		return OnPreChangeDelegate;
	}

	/**
	 * Access the delegate that is invoked whenever this element list has been changed.
	 * @note This is called automatically at the end of each frame, but can also be manually invoked by NotifyPendingChanges.
	 */
	DECLARE_EVENT_OneParam(UTypedElementList, FOnChanged, const UTypedElementList* /*InElementList*/);
	FOnChanged& OnChanged()
	{
		return OnChangedDelegate;
	}

	/**
	 * Invoke the delegate called whenever this element list has been changed.
	 */
	void NotifyPendingChanges();

	/**
	 * Clear whether there are pending changes for OnChangedDelegate to notify for, without emitting a notification.
	 */
	void ClearPendingChanges();

	/**
	 * Access the interface to allow external systems (such as USelection) to receive immediate sync notifications as an element list is changed.
	 * This exists purely as a bridging mechanism and shouldn't be relied on for new code. It is lazily created as needed.
	 */
	FTypedElementListLegacySync& Legacy_GetSync();

	/**
	 * Access the interface to allow external systems (such as USelection) to receive immediate sync notifications as an element list is changed.
	 * This exists purely as a bridging mechanism and shouldn't be relied on for new code. This will return null if no legacy sync has been created for this instance.
	 */
	FTypedElementListLegacySync* Legacy_GetSyncPtr() const;

private:
	enum class EChangeType : uint8
	{
		/**
		* An element was added to the element list.
		* The ElementHandle argument will be set to the element that was added.
		*/
		Added,

		/**
		* An element was removed from the element list.
		* The ElementHandle argument will be set to the element that was removed.
		*/
		Removed,

		/**
		* The element list was cleared.
		* The ElementHandle argument will be unset.
		*/
		Cleared,
	};

	void Initialize(UTypedElementRegistry* InRegistry);

	bool AddElementImpl(FTypedElementHandle&& InElementHandle);
	bool RemoveElementImpl(const FTypedElementId& InElementId);
	int32 RemoveAllElementsImpl(TFunctionRef<bool(const FTypedElementHandle&)> InPredicate);
	bool ContainsElementImpl(const FTypedElementId& InElementId) const;

	void NoteListMayChange();
	void NoteListChanged(const EChangeType InChangeType, const FTypedElementHandle& InElementHandle = FTypedElementHandle());

	/**
	 * Element registry this element list is associated with.
	 */
	TWeakObjectPtr<UTypedElementRegistry> Registry;

	/**
	 * Set of combined ID values that are currently present in this element list.
	 * Used to perform optimized querying of which elements are in this list, and to avoid adding duplicate entries.
	 */
	TSet<FTypedHandleCombinedId> ElementCombinedIds;

	/**
	 * Array of element handles present in this element list.
	 * These are stored in the same order that they are added, and the set above can be used to optimize certain queries.
	 */
	TArray<FTypedElementHandle> ElementHandles;

	/**
	 * Delegate that is invoked whenever this element list is potentially about to change.
	 */
	FOnPreChange OnPreChangeDelegate;

	/**
	 * Delegate that is invoked whenever this element list has been changed.
	 */
	FOnChanged OnChangedDelegate;

	/**
	 * Whether there are pending changes for OnChangedDelegate to notify for.
	 */
	bool bHasPendingNotify = false;

	/**
	 * Interface to allow external systems (such as USelection) to receive immediate sync notifications as an element list is changed.
	 * This exists purely as a bridging mechanism and shouldn't be relied on for new code. It is lazily created as needed.
	 */
	TUniquePtr<FTypedElementListLegacySync> LegacySync;
};
