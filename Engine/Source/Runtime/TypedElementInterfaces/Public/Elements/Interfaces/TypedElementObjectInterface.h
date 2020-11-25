// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "TypedElementObjectInterface.generated.h"

UCLASS(Abstract)
class TYPEDELEMENTINTERFACES_API UTypedElementObjectInterface : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	/**
	 * Get the object instance that this handle represents, if any.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Object")
	virtual UObject* GetObject(const FTypedElementHandle& InElementHandle) { return nullptr; }
};

template <>
struct TTypedElement<UTypedElementObjectInterface> : public TTypedElementBase<UTypedElementObjectInterface>
{
	UObject* GetObject() const { return InterfacePtr->GetObject(*this); }
};
