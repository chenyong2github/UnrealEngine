// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "TypedElementHierarchyInterface.generated.h"

UCLASS(Abstract)
class TYPEDELEMENTRUNTIME_API UTypedElementHierarchyInterface : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	/**
	 * Get the logical parent of this element, if any.
	 * eg) A component might return its actor, or a static mesh instance might return its ISM component.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Hierarchy")
	virtual FTypedElementHandle GetParentElement(const FTypedElementHandle& InElementHandle, const bool bAllowCreate = true) { return FTypedElementHandle(); }

	/**
	 * Get the logical children of this element, if any.
	 * eg) An actor might return its component, or an ISM component might return its static mesh instances.
	 *
	 * @note Appends to OutElementHandles.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Hierarchy")
	virtual void GetChildElements(const FTypedElementHandle& InElementHandle, TArray<FTypedElementHandle>& OutElementHandles, const bool bAllowCreate = true) {}
};

template <>
struct TTypedElement<UTypedElementHierarchyInterface> : public TTypedElementBase<UTypedElementHierarchyInterface>
{
	FTypedElementHandle GetParentElement(const bool bAllowCreate = true) const { return InterfacePtr->GetParentElement(*this, bAllowCreate); }
	void GetChildElements(TArray<FTypedElementHandle>& OutElementHandles, const bool bAllowCreate = true) const { return InterfacePtr->GetChildElements(*this, OutElementHandles, bAllowCreate); }
};
