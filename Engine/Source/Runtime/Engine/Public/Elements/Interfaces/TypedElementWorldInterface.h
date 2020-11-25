// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "TypedElementWorldInterface.generated.h"

class UWorld;

UCLASS(Abstract)
class ENGINE_API UTypedElementWorldInterface : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	/**
	 * Can this element actually be edited in the world?
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|World")
	virtual bool CanEditElement(const FTypedElementHandle& InElementHandle) { return true; }

	/**
	 * Get the owner world associated with this element, if any.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|World")
	virtual UWorld* GetOwnerWorld(const FTypedElementHandle& InElementHandle) { return nullptr; }

	/**
	 * Get the bounds of this element within its owner world, if any.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|World")
	virtual bool GetWorldBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds) { return false; }

	/**
	 * Get the transform of this element within its owner world, if any.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|World")
	virtual bool GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform) { return false; }
	
	/**
	 * Attempt to set the transform of this element within its owner world.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|World")
	virtual bool SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform) { return false; }
};

template <>
struct TTypedElement<UTypedElementWorldInterface> : public TTypedElementBase<UTypedElementWorldInterface>
{
	bool CanEditElement() const { return InterfacePtr->CanEditElement(*this); }
	UWorld* GetOwnerWorld() const { return InterfacePtr->GetOwnerWorld(*this); }
	bool GetWorldBounds(FBoxSphereBounds& OutBounds) const { return InterfacePtr->GetWorldBounds(*this, OutBounds); }
	bool GetWorldTransform(FTransform& OutTransform) const { return InterfacePtr->GetWorldTransform(*this, OutTransform); }
	bool SetWorldTransform(const FTransform& InTransform) const { return InterfacePtr->SetWorldTransform(*this, InTransform); }
};
