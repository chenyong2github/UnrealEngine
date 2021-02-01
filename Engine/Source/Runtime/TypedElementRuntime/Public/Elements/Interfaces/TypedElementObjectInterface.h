// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "TypedElementObjectInterface.generated.h"

UCLASS(Abstract)
class TYPEDELEMENTRUNTIME_API UTypedElementObjectInterface : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	/**
	 * Get the object instance that this handle represents, if any.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|Object")
	virtual UObject* GetObject(const FTypedElementHandle& InElementHandle);

	/**
	 * Gets the object instance's class that the handle represents, if any. 
	 */
	UFUNCTION(BlueprintPure, Category = "TypedElementInterfaces|Object")
	UClass* GetObjectClass(const FTypedElementHandle& InElementHandle);

	/**
	 * Attempts to cast the given handle to another class.
	 *
	 * @returns the casted object if successful, otherwise nullptr.
	 */
	template <class CastTo>
	CastTo* GetObjectAs(const FTypedElementHandle& InElementHandle)
	{
		return Cast<CastTo>(GetObject(InElementHandle));
	}

	/**
	 * Attempts to cast the given handle to another class, if it can also be casted to TargetClass.
	 * This is intended for use in cases where the calling code may only need an actor pointer, but also wants to be sure it's a specific type of actor.
	 *
	 * @returns the casted object if successful, otherwise nullptr.
	 */
	template <class CastTo = UObject>
	CastTo* GetObjectAs(const FTypedElementHandle& InElementHandle, TSubclassOf<CastTo> TargetClass)
	{
		if (!TargetClass)
		{
			return nullptr;
		}

		UObject* ObjectPtr = GetObject(InElementHandle);
		if (ObjectPtr && ObjectPtr->IsA(TargetClass))
		{
			return (CastTo*)ObjectPtr;
		}

		return nullptr;
	}
};

template <>
struct TTypedElement<UTypedElementObjectInterface> : public TTypedElementBase<UTypedElementObjectInterface>
{
	UObject* GetObject() const { return InterfacePtr->GetObject(*this); }
	UClass* GetObjectClass() const { return InterfacePtr->GetObjectClass(*this); }

	template <class CastTo>
	CastTo* GetObjectAs() const { return InterfacePtr->GetObjectAs<CastTo>(*this); }

	template <class CastTo>
	CastTo* GetObjectAs(TSubclassOf<CastTo> TargetClass) const { return InterfacePtr->GetObjectAs<CastTo>(*this, TargetClass); }
};
