// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementHandle.h"
#include "TypedElementSelectionInterface.generated.h"

UCLASS()
class TYPEDELEMENTINTERFACES_API UTypedElementSelectionInterface : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	/**
	 * Test to see whether the given handle is in a valid state to be selected.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|Selection")
	virtual bool IsValidSelection(const FTypedElementHandle& InElementHandle) { return true; }

	/**
	 * Retreive the object instance that should be selected via this handle.
	 * @note This exists to allow the legacy USelection to bridge to an UTypedElementList instance. It should not be used in new code!
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|Selection")
	virtual UObject* Legacy_GetSelectionObject(const FTypedElementHandle& InElementHandle) { return nullptr; }
};

template <>
struct TTypedElement<UTypedElementSelectionInterface> : public TTypedElementBase<UTypedElementSelectionInterface>
{
	bool IsValidSelection() const { return InterfacePtr->IsValidSelection(*this); }
	UObject* Legacy_GetSelectionObject() const { return InterfacePtr->Legacy_GetSelectionObject(*this); }
};
