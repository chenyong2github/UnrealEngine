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
	virtual void GetElementsForAction(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToDelete);
	virtual bool DeleteElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions);
	virtual void DuplicateElements(UTypedElementWorldInterface* InWorldInterface, TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, const FVector& InLocationOffset, TArray<FTypedElementHandle>& OutNewElements);
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
	void GetElementsForAction(const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToDelete) { CommonActionsCustomization->GetElementsForAction(ElementWorldHandle, InSelectionSet, OutElementsToDelete); }

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
	 * Get the elements from the given selection set that a common action should operate on.
	 * @note This allows coarse filtering of the elements based on the selection state (eg, favoring components over actors), however it doesn't mean that the returned elements will actually be valid to perform a given action on.
	 */
	void GetSelectedElementsForAction(const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsForAction) const;

	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Common")
	bool DeleteElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions);
	bool DeleteElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions);
	bool DeleteElements(const UTypedElementList* ElementList, UWorld* World, UTypedElementSelectionSet* SelectionSet, const FTypedElementDeletionOptions& DeletionOptions);

	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Common")
	bool DeleteSelectedElements(UTypedElementSelectionSet* SelectionSet, UWorld* World, const FTypedElementDeletionOptions& DeletionOptions);

	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Common")
	TArray<FTypedElementHandle> DuplicateElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, const FVector& LocationOffset);
	TArray<FTypedElementHandle> DuplicateElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, const FVector& LocationOffset);
	TArray<FTypedElementHandle> DuplicateElements(const UTypedElementList* ElementList, UWorld* World, const FVector& LocationOffset);

	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Common")
	TArray<FTypedElementHandle> DuplicateSelectedElements(const UTypedElementSelectionSet* SelectionSet, UWorld* World, const FVector& LocationOffset);

private:
	/**
	 * Attempt to resolve the selection interface and common actions customization for the given element, if any.
	 */
	FTypedElementCommonActionsElement ResolveCommonActionsElement(const FTypedElementHandle& InElementHandle) const;
};
