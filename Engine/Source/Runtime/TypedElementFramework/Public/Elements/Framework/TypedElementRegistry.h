// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "Containers/ArrayView.h"
#include "Containers/SortedMap.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementList.h"
#include "TypedElementRegistry.generated.h"

/**
 * Registry of element types and their associated interfaces, along with the elements that represent their instances.
 */
UCLASS(Transient)
class TYPEDELEMENTFRAMEWORK_API UTypedElementRegistry : public UObject
{
	GENERATED_BODY()

public:
	UTypedElementRegistry();

	//~ UObject interface
	virtual void FinishDestroy() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/**
	 * Initialize the singleton instance of the registry used in most cases.
	 */
	static void Private_InitializeInstance();

	/**
	 * Shutdown the singleton instance of the registry used in most cases.
	 */
	static void Private_ShutdownInstance();

	/**
	 * Get the singleton instance of the registry used in most cases.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Registry")
	static UTypedElementRegistry* GetInstance();

	/**
	 * Event fired when references to one element should be replaced with a reference to a different element.
	 */
	using FOnElementReplaced_Payload = TArrayView<const TTuple<FTypedElementHandle, FTypedElementHandle>>; // Alias for the macro below
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnElementReplaced, FOnElementReplaced_Payload /*ReplacedElements*/);
	FOnElementReplaced& OnElementReplaced()
	{
		return OnElementReplacedDelegate;
	}

	/**
	 * Event fired when an element has been internally updated and data cached from it should be refreshed.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnElementUpdated, TArrayView<const FTypedElementHandle> /*UpdatedElements*/);
	FOnElementUpdated& OnElementUpdated()
	{
		return OnElementUpdatedDelegate;
	}

	/**
	 * Event fired prior to processing any elements that were previously marked for deferred destruction.
	 * @see ProcessDeferredElementsToDestroy.
	 */
	FSimpleMulticastDelegate& OnProcessingDeferredElementsToDestroy()
	{
		return OnProcessingDeferredElementsToDestroyDelegate;
	}

	/**
	 * Get the element type ID for the associated element type name, if any.
	 * @return The element type ID, or 0 if the given name wasn't registered.
	 */
	FORCEINLINE FTypedHandleTypeId GetRegisteredElementTypeId(const FName InElementTypeName) const
	{
		FReadScopeLock RegisteredElementTypesLock(RegisteredElementTypesRW);

		const FTypedHandleTypeId* TypeId = RegisteredElementTypesNameToId.Find(InElementTypeName);
		return TypeId ? *TypeId : 0;
	}

	/**
	 * Register an element type that doesn't require any additional payload data.
	 */
	FORCEINLINE void RegisterElementType(const FName InElementTypeName)
	{
		RegisterElementTypeImpl(InElementTypeName, MakeUnique<TRegisteredElementType<void>>());
	}

	/**
	 * Register an element type that has additional payload data.
	 */
	template <typename ElementDataType>
	FORCEINLINE void RegisterElementType(const FName InElementTypeName)
	{
		RegisterElementTypeImpl(InElementTypeName, MakeUnique<TRegisteredElementType<ElementDataType>>());
	}

	/**
	 * Register that an element interface is supported for the given type, which must have previously been registered via RegisterElementType.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE void RegisterElementInterface(const FName InElementTypeName, UTypedElementInterface* InElementInterface, const bool InAllowOverride = false)
	{
		RegisterElementInterfaceImpl(InElementTypeName, InElementInterface, BaseInterfaceType::StaticClass(), InAllowOverride);
	}

	/**
	 * Get the element interface supported by the given type, or null if there is no support for this interface.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE BaseInterfaceType* GetElementInterface(const FTypedHandleTypeId InElementTypeId) const
	{
		return static_cast<BaseInterfaceType*>(GetElementInterfaceImpl(InElementTypeId, BaseInterfaceType::StaticClass()));
	}

	/**
	 * Get the element interface supported by the given handle, or null if there is no support for this interface.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE BaseInterfaceType* GetElementInterface(const FTypedElementHandle& InElementHandle) const
	{
		return static_cast<BaseInterfaceType*>(GetElementInterfaceImpl(InElementHandle.GetId().GetTypeId(), BaseInterfaceType::StaticClass()));
	}

	/**
	 * Get the element interface supported by the given handle, or null if there is no support for this interface.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Registry")
	FORCEINLINE UTypedElementInterface* GetElementInterface(const FTypedElementHandle& InElementHandle, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
	{
		return GetElementInterfaceImpl(InElementHandle.GetId().GetTypeId(), InBaseInterfaceType);
	}

	/**
	 * Create an element that doesn't require any additional payload data.
	 * @note The associated handle ID should be something that can externally be used to uniquely identify this element, until DestroyElementHandle is called on this handle.
	 */
	FORCEINLINE FTypedElementOwner CreateElement(const FName InElementTypeName, const FTypedHandleElementId InElementId)
	{
		return CreateElementImpl<void>(InElementTypeName, InElementId);
	}

	/**
	 * Create an element that has additional payload data.
	 * @note Allocation of the payload data and the associated handle ID are managed internally, and the data will remain valid until DestroyElementHandle is called on this handle.
	 */
	template <typename ElementDataType>
	FORCEINLINE TTypedElementOwner<ElementDataType> CreateElement(const FName InElementTypeName)
	{
		return CreateElementImpl<ElementDataType>(InElementTypeName, INDEX_NONE);
	}

	/**
	 * Destroy an element.
	 * @note Destruction is deferred until the next call to ProcessDeferredElementsToDestroy.
	 */
	FORCEINLINE void DestroyElement(FTypedElementOwner& InOutElementOwner)
	{
		return DestroyElementImpl<void>(InOutElementOwner);
	}

	/**
	 * Destroy an element.
	 * @note Destruction is deferred until the next call to ProcessDeferredElementsToDestroy.
	 */
	template <typename ElementDataType>
	FORCEINLINE void DestroyElement(TTypedElementOwner<ElementDataType>& InOutElementOwner)
	{
		return DestroyElementImpl<ElementDataType>(InOutElementOwner);
	}

	/**
	 * Process any elements that were previously marked for deferred destruction.
	 * @note This is automatically called at the end of each frame, but may also be called manually.
	 * @note This is automatically called post-GC, unless auto-GC destruction has been disabled (@see FDisableElementDestructionOnGC).
	 */
	void ProcessDeferredElementsToDestroy();

	/**
	 * Release an element ID that was previously acquired from an existing handle.
	 */
	void ReleaseElementId(FTypedElementId& InOutElementId);

	/**
	 * Get an element handle from its minimal ID.
	 */
	FTypedElementHandle GetElementHandle(const FTypedElementId& InElementId) const;

	/**
	 * Get an element that implements the given interface from its minimal ID.
	 */
	FORCEINLINE FTypedElement GetElement(const FTypedElementId& InElementId, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
	{
		FTypedElement Element;
		GetElementImpl(InElementId, InBaseInterfaceType, Element);
		return Element;
	}

	/**
	 * Get an element that implements the given interface from its minimal ID.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE TTypedElement<BaseInterfaceType> GetElement(const FTypedElementId& InElementId) const
	{
		TTypedElement<BaseInterfaceType> Element;
		GetElementImpl(InElementId, BaseInterfaceType::StaticClass(), Element);
		return Element;
	}

	/**
	 * Get an element that implements the given interface from its handle.
	 */
	FORCEINLINE FTypedElement GetElement(const FTypedElementHandle& InElementHandle, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
	{
		FTypedElement Element;
		GetElementImpl(InElementHandle, InBaseInterfaceType, Element);
		return Element;
	}

	/**
	 * Get an element that implements the given interface from its handle.
	 */
	template <typename BaseInterfaceType>
	FORCEINLINE TTypedElement<BaseInterfaceType> GetElement(const FTypedElementHandle& InElementHandle) const
	{
		TTypedElement<BaseInterfaceType> Element;
		GetElementImpl(InElementHandle, BaseInterfaceType::StaticClass(), Element);
		return Element;
	}

	/**
	 * Create an empty list of elements associated with this registry.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Registry")
	FORCEINLINE UTypedElementList* CreateElementList()
	{
		return UTypedElementList::Private_CreateElementList(this);
	}

	/**
	 * Create an empty list of elements associated with this registry, populated from the given minimal IDs that are valid.
	 */
	UTypedElementList* CreateElementList(TArrayView<const FTypedElementId> InElementIds);

	/**
	 * Create an empty list of elements associated with this registry, populated from the given handles that are valid.
	 */
	UTypedElementList* CreateElementList(TArrayView<const FTypedElementHandle> InElementHandles);

	/**
	 * Create an empty list of elements associated with this registry, populated from the given owners that are valid.
	 */
	template <typename ElementDataType>
	FORCEINLINE UTypedElementList* CreateElementList(const TArray<TTypedElementOwner<ElementDataType>>& InElementOwners)
	{
		return CreateElementList(MakeArrayView(InElementOwners));
	}

	/**
	 * Create an empty list of elements associated with this registry, populated from the given owners that are valid.
	 */
	template <typename ElementDataType>
	UTypedElementList* CreateElementList(TArrayView<const TTypedElementOwner<ElementDataType>> InElementOwners)
	{
		UTypedElementList* ElementList = CreateElementList();
		ElementList->Append(InElementOwners);
		return ElementList;
	}

	void Private_OnElementListCreated(UTypedElementList* InElementList)
	{
		FWriteScopeLock ActiveElementListsLock(ActiveElementListsRW);
		ActiveElementLists.Add(InElementList);
	}

	void Private_OnElementListDestroyed(UTypedElementList* InElementList)
	{
		FWriteScopeLock ActiveElementListsLock(ActiveElementListsRW);
		ActiveElementLists.Remove(InElementList);
	}

	// Note: Access for UTypedElementList
	FORCEINLINE void Private_GetElementImpl(const FTypedElementHandle& InElementHandle, const UClass* InBaseInterfaceType, FTypedElement& OutElement) const
	{
		GetElementImpl(InElementHandle, InBaseInterfaceType, OutElement);
	}

	/** Struct to disable auto-GC reference collection within a scope */
	struct FDisableElementDestructionOnGC
	{
	public:
		explicit FDisableElementDestructionOnGC(UTypedElementRegistry* InRegistry)
			: Registry(InRegistry)
		{
			checkf(Registry, TEXT("FDisableElementDestructionOnGC must be used with a valid registry!"));
			Registry->IncrementDisableElementDestructionOnGCCount();
		}

		~FDisableElementDestructionOnGC()
		{
			Registry->DecrementDisableElementDestructionOnGCCount();
		}

		FDisableElementDestructionOnGC(const FDisableElementDestructionOnGC&) = delete;
		FDisableElementDestructionOnGC& operator=(const FDisableElementDestructionOnGC&) = delete;

		FDisableElementDestructionOnGC(FDisableElementDestructionOnGC&&) = delete;
		FDisableElementDestructionOnGC& operator=(FDisableElementDestructionOnGC&&) = delete;

	private:
		UTypedElementRegistry* Registry = nullptr;
	};

private:
	struct FRegisteredElementType
	{
		virtual ~FRegisteredElementType() = default;

		virtual FTypedElementInternalData& AddDataForElement(FTypedHandleElementId& InOutElementId) = 0;
		virtual void RemoveDataForElement(const FTypedHandleElementId InElementId, const FTypedElementInternalData* InExpectedDataPtr, const bool bDefer) = 0;
		virtual const FTypedElementInternalData& GetDataForElement(const FTypedHandleElementId InElementId) const = 0;
		virtual void ProcessDeferredElementsToRemove() = 0;
		virtual void SetDataTypeId(const FTypedHandleTypeId InTypeId) = 0;
		virtual FTypedHandleTypeId GetDataTypeId() const = 0;
		virtual FName GetDataTypeName() const = 0;

		FTypedHandleTypeId TypeId = 0;
		FName TypeName;
		TSortedMap<FName, UTypedElementInterface*, FDefaultAllocator, FNameFastLess> Interfaces;
	};

	template <typename ElementDataType>
	struct TRegisteredElementType : public FRegisteredElementType
	{
		virtual ~TRegisteredElementType() = default;
		
		virtual FTypedElementInternalData& AddDataForElement(FTypedHandleElementId& InOutElementId) override
		{
			return HandleDataStore.AddDataForElement(TypeId, InOutElementId);
		}

		virtual void RemoveDataForElement(const FTypedHandleElementId InElementId, const FTypedElementInternalData* InExpectedDataPtr, const bool bDefer) override
		{
			if (bDefer)
			{
				InExpectedDataPtr->StoreDestructionRequestCallstack();

				FScopeLock DeferredElementsToRemoveLock(&DeferredElementsToRemoveCS);
				DeferredElementsToRemove.Add(MakeTuple(InElementId, InExpectedDataPtr));
			}
			else
			{
				HandleDataStore.RemoveDataForElement(InElementId, InExpectedDataPtr);
			}
		}

		virtual const FTypedElementInternalData& GetDataForElement(const FTypedHandleElementId InElementId) const override
		{
			return HandleDataStore.GetDataForElement(InElementId);
		}

		virtual void ProcessDeferredElementsToRemove() override
		{
			FScopeLock DeferredElementsToRemoveLock(&DeferredElementsToRemoveCS);
			for (const FDeferredElementToRemove& DeferredElementToRemove : DeferredElementsToRemove)
			{
				HandleDataStore.RemoveDataForElement(DeferredElementToRemove.Get<0>(), DeferredElementToRemove.Get<1>());
			}
			DeferredElementsToRemove.Reset();
		}

		virtual void SetDataTypeId(const FTypedHandleTypeId InTypeId) override
		{
			TTypedElementInternalDataStore<ElementDataType>::SetStaticDataTypeId(InTypeId);
		}

		virtual FTypedHandleTypeId GetDataTypeId() const override
		{
			return TTypedElementInternalDataStore<ElementDataType>::StaticDataTypeId();
		}

		virtual FName GetDataTypeName() const override
		{
			return TTypedElementInternalDataStore<ElementDataType>::StaticDataTypeName();
		}

		TTypedElementInternalDataStore<ElementDataType> HandleDataStore;

		using FDeferredElementToRemove = TTuple<FTypedHandleElementId, const FTypedElementInternalData*>;
		FCriticalSection DeferredElementsToRemoveCS;
		TArray<FDeferredElementToRemove> DeferredElementsToRemove;
	};

	void RegisterElementTypeImpl(const FName InElementTypeName, TUniquePtr<FRegisteredElementType>&& InRegisteredElementType);
	void RegisterElementInterfaceImpl(const FName InElementTypeName, UTypedElementInterface* InElementInterface, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType, const bool InAllowOverride);
	UTypedElementInterface* GetElementInterfaceImpl(const FTypedHandleTypeId InElementTypeId, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const;

	template <typename ElementDataType>
	TTypedElementOwner<ElementDataType> CreateElementImpl(const FName InElementTypeName, const FTypedHandleElementId InElementId)
	{
		FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromName(InElementTypeName);
		checkf(RegisteredElementType, TEXT("Element type '%s' has not been registered!"), *InElementTypeName.ToString());

		checkf(RegisteredElementType->GetDataTypeId() == TTypedElementInternalDataStore<ElementDataType>::StaticDataTypeId(), TEXT("Element type '%s' uses '%s' as its handle data type, but '%s' was requested!"), *InElementTypeName.ToString(), *RegisteredElementType->GetDataTypeName().ToString(), *TTypedElementInternalDataStore<ElementDataType>::StaticDataTypeName().ToString());

		FTypedHandleElementId NewElementId = InElementId;
		FTypedElementInternalData& NewElementData = RegisteredElementType->AddDataForElement(NewElementId);

		TTypedElementOwner<ElementDataType> ElementOwner;
		ElementOwner.Private_InitializeAddRef(static_cast<TTypedElementInternalData<ElementDataType>&>(NewElementData));

		return ElementOwner;
	}

	template <typename ElementDataType>
	void DestroyElementImpl(TTypedElementOwner<ElementDataType>& InOutElementOwner)
	{
		FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromId(InOutElementOwner.GetId().GetTypeId());
		checkf(RegisteredElementType, TEXT("Element type ID '%d' has not been registered!"), InOutElementOwner.GetId().GetTypeId());

		RegisteredElementType->RemoveDataForElement(InOutElementOwner.GetId().GetElementId(), InOutElementOwner.Private_GetInternalData(), /*bDefer*/true);
		InOutElementOwner.Private_DestroyNoRef();
	}

	template <typename BaseInterfaceType>
	void GetElementImpl(const FTypedElementId& InElementId, const UClass* InBaseInterfaceType, TTypedElement<BaseInterfaceType>& OutElement) const
	{
		OutElement.Private_DestroyReleaseRef();

		if (InElementId)
		{
			FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromId(InElementId.GetTypeId());
			checkf(RegisteredElementType, TEXT("Element type ID '%d' has not been registered!"), InElementId.GetTypeId());

			OutElement.Private_InitializeAddRef(RegisteredElementType->GetDataForElement(InElementId.GetElementId()), static_cast<BaseInterfaceType*>(RegisteredElementType->Interfaces.FindRef(InBaseInterfaceType->GetFName())));
		}
	}

	template <typename BaseInterfaceType>
	void GetElementImpl(const FTypedElementHandle& InElementHandle, const UClass* InBaseInterfaceType, TTypedElement<BaseInterfaceType>& OutElement) const
	{
		OutElement.Private_DestroyReleaseRef();

		if (InElementHandle)
		{
			FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromId(InElementHandle.GetId().GetTypeId());
			checkf(RegisteredElementType, TEXT("Element type ID '%d' has not been registered!"), InElementHandle.GetId().GetTypeId());

			OutElement.Private_InitializeAddRef(*InElementHandle.Private_GetInternalData(), static_cast<BaseInterfaceType*>(RegisteredElementType->Interfaces.FindRef(InBaseInterfaceType->GetFName())));
		}
	}

	void AddRegisteredElementType(TUniquePtr<FRegisteredElementType>&& InRegisteredElementType)
	{
		checkf(InRegisteredElementType->TypeId > 0, TEXT("Element type ID was unassigned!"));
		checkf(!GetRegisteredElementTypeFromId(InRegisteredElementType->TypeId), TEXT("Element type '%d' has already been registered!"), InRegisteredElementType->TypeId);
		checkf(!GetRegisteredElementTypeFromName(InRegisteredElementType->TypeName), TEXT("Element type '%s' has already been registered!"), *InRegisteredElementType->TypeName.ToString());

		{
			FWriteScopeLock RegisteredElementTypesLock(RegisteredElementTypesRW);

			RegisteredElementTypesNameToId.Add(InRegisteredElementType->TypeName, InRegisteredElementType->TypeId);
			RegisteredElementTypes[InRegisteredElementType->TypeId - 1] = MoveTemp(InRegisteredElementType);
		}
	}

	FRegisteredElementType* GetRegisteredElementTypeFromId(const FTypedHandleTypeId InTypeId) const
	{
		FReadScopeLock RegisteredElementTypesLock(RegisteredElementTypesRW);

		return InTypeId > 0
			? RegisteredElementTypes[InTypeId - 1].Get()
			: nullptr;
	}

	FRegisteredElementType* GetRegisteredElementTypeFromName(const FName& InTypeName) const
	{
		FReadScopeLock RegisteredElementTypesLock(RegisteredElementTypesRW);

		if (const FTypedHandleTypeId* TypeId = RegisteredElementTypesNameToId.Find(InTypeName))
		{
			return RegisteredElementTypes[(*TypeId) - 1].Get();
		}

		return nullptr;
	}

	void NotifyElementListPendingChanges();

	void OnBeginFrame();
	void OnEndFrame();
	void OnPostGarbageCollect();

	FORCEINLINE void IncrementDisableElementDestructionOnGCCount()
	{
		checkf(DisableElementDestructionOnGCCount != MAX_uint8, TEXT("DisableElementDestructionOnGCCount overflow!"));
		++DisableElementDestructionOnGCCount;
	}

	FORCEINLINE void DecrementDisableElementDestructionOnGCCount()
	{
		checkf(DisableElementDestructionOnGCCount != 0, TEXT("DisableElementDestructionOnGCCount underflow!"));
		--DisableElementDestructionOnGCCount;
	}

	mutable FRWLock RegisteredElementTypesRW;
	TUniquePtr<FRegisteredElementType> RegisteredElementTypes[TypedHandleMaxTypeId - 1];
	TSortedMap<FName, FTypedHandleTypeId, FDefaultAllocator, FNameFastLess> RegisteredElementTypesNameToId;

	mutable FRWLock ActiveElementListsRW;
	TSet<UTypedElementList*> ActiveElementLists;

	uint8 DisableElementDestructionOnGCCount = 0;
	bool bIsWithinFrame = false;

	FOnElementReplaced OnElementReplacedDelegate;
	FOnElementUpdated OnElementUpdatedDelegate;
	FSimpleMulticastDelegate OnProcessingDeferredElementsToDestroyDelegate;
};
