// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementInterfaceCustomization.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "TypedElementCommonActions.generated.h"

class UTypedElementList;
class UTypedElementSelectionSet;

/**
 * Customization used to allow asset editors (such as the level editor) to override the base behavior of common actions.
 */
class ENGINE_API FTypedElementCommonActionsCustomization
{
public:
	virtual ~FTypedElementCommonActionsCustomization() = default;

	//~ See UTypedElementCommonActions for API docs
	virtual void GetElementsToDelete(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToDelete);
	virtual bool DeleteElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions);
	virtual void DuplicateElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, bool bOffsetLocations, TArray<FTypedElementHandle>& OutNewElements);
};

/**
 * Utility to hold a typed element handle and its associated world interface and common actions customization.
 */
struct ENGINE_API FTypedElementCommonActionsElement
{
public:
	FTypedElementCommonActionsElement() = default;

	FTypedElementCommonActionsElement(TTypedElement<UTypedElementWorldInterface> InElementWorldHandle, FTypedElementCommonActionsCustomization* InCommonActionsCustomization)
		: ElementWorldHandle(MoveTemp(InElementWorldHandle))
		, CommonActionsCustomization(InCommonActionsCustomization)
	{
	}

	FTypedElementCommonActionsElement(const FTypedElementCommonActionsElement&) = default;
	FTypedElementCommonActionsElement& operator=(const FTypedElementCommonActionsElement&) = default;

	FTypedElementCommonActionsElement(FTypedElementCommonActionsElement&&) = default;
	FTypedElementCommonActionsElement& operator=(FTypedElementCommonActionsElement&&) = default;

	FORCEINLINE explicit operator bool() const
	{
		return IsSet();
	}

	FORCEINLINE bool IsSet() const
	{
		return ElementWorldHandle.IsSet()
			&& CommonActionsCustomization;
	}

	//~ See UTypedElementCommonActions for API docs
	void GetElementsToDelete(const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToDelete) { CommonActionsCustomization->GetElementsToDelete(ElementWorldHandle, InSelectionSet, OutElementsToDelete); }

private:
	TTypedElement<UTypedElementWorldInterface> ElementWorldHandle;
	FTypedElementCommonActionsCustomization* CommonActionsCustomization = nullptr;
};

/**
 * A utility to handle higher-level common actions, but default via UTypedElementWorldInterface,
 * but asset editors can customize this behavior via FTypedElementCommonActionsCustomization.
 */
UCLASS(Transient)
class ENGINE_API UTypedElementCommonActions : public UObject, public TTypedElementInterfaceCustomizationRegistry<FTypedElementCommonActionsCustomization>
{
	GENERATED_BODY()

public:
	/**
	 * Get the elements from the given selection set that should be deleted
	 */
	void GetSelectedElementsToDelete(const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToDelete) const;

	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Common")
	bool DeleteElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions);
	bool DeleteElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions);
	bool DeleteElements(const UTypedElementList* ElementList, UWorld* World, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions);

	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Common")
	TArray<FTypedElementHandle> DuplicateElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, bool bOffsetLocations);
	TArray<FTypedElementHandle> DuplicateElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, bool bOffsetLocations);
	TArray<FTypedElementHandle> DuplicateElements(const UTypedElementList* ElementList, UWorld* World, bool bOffsetLocations);

private:
	/**
	 * Attempt to resolve the selection interface and common actions customization for the given element, if any.
	 */
	FTypedElementCommonActionsElement ResolveCommonActionsElement(const FTypedElementHandle& InElementHandle) const;
};
