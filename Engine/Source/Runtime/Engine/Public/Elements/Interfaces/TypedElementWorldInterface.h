// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "TypedElementWorldInterface.generated.h"

class UWorld;
struct FCollisionShape;

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
	 * Get the bounds of this element, if any.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|World")
	virtual bool GetBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds) { return false; }

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

	/**
	 * Get the transform of this element relative to its parent, if any.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementInterfaces|World")
	virtual bool GetRelativeTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform) { return GetWorldTransform(InElementHandle, OutTransform); }
	
	/**
	 * Attempt to set the transform of this element relative to its parent.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|World")
	virtual bool SetRelativeTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform) { return SetWorldTransform(InElementHandle, InTransform); }

	/**
	 * Notify that this element is about to be moved.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|World")
	virtual void NotifyMovementStarted(const FTypedElementHandle& InElementHandle) {}

	/**
	 * Notify that this element is currently being moved.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|World")
	virtual void NotifyMovementOngoing(const FTypedElementHandle& InElementHandle) {}

	/**
	 * Notify that this element is done being moved.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|World")
	virtual void NotifyMovementEnded(const FTypedElementHandle& InElementHandle) {}

	/**
	 * Attempt to find a suitable (non-intersecting) transform for the given element at the given point.
	 */
	virtual bool FindSuitableTransformAtPoint(const FTypedElementHandle& InElementHandle, const FTransform& InPotentialTransform, FTransform& OutSuitableTransform)
	{
		OutSuitableTransform = InPotentialTransform;
		return true;
	}

	/**
	 * Attempt to find a suitable (non-intersecting) transform for the given element along the given path.
	 */
	virtual bool FindSuitableTransformAlongPath(const FTypedElementHandle& InElementHandle, const FVector& InPathStart, const FVector& InPathEnd, const FCollisionShape& InTestShape, TArrayView<const FTypedElementHandle> InElementsToIgnore, FTransform& OutSuitableTransform)
	{
		return false;
	}

	/**
	 * Duplicate the given element.
	 * @note Default version calls DuplicateElements with a single element.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementInterfaces|World")
	virtual FTypedElementHandle DuplicateElement(const FTypedElementHandle& InElementHandle, UWorld* InWorld, bool bOffsetLocations)
	{
		TArray<FTypedElementHandle> NewElements;
		DuplicateElements(MakeArrayView(&InElementHandle, 1), InWorld, bOffsetLocations, NewElements);
		return NewElements.Num() > 0 ? MoveTemp(NewElements[0]) : FTypedElementHandle();
	}

	/**
	 * Duplicate the given set of elements.
	 * @note If you want to duplicate an array of elements that are potentially different types, you probably want to use the higher-level UEngineElementsLibrary::DuplicateElements function instead.
	 */
	virtual void DuplicateElements(TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, bool bOffsetLocations, TArray<FTypedElementHandle>& OutNewElements)
	{
	}
};

template <>
struct TTypedElement<UTypedElementWorldInterface> : public TTypedElementBase<UTypedElementWorldInterface>
{
	bool CanEditElement() const { return InterfacePtr->CanEditElement(*this); }
	UWorld* GetOwnerWorld() const { return InterfacePtr->GetOwnerWorld(*this); }
	bool GetBounds(FBoxSphereBounds& OutBounds) const { return InterfacePtr->GetBounds(*this, OutBounds); }
	bool GetWorldTransform(FTransform& OutTransform) const { return InterfacePtr->GetWorldTransform(*this, OutTransform); }
	bool SetWorldTransform(const FTransform& InTransform) const { return InterfacePtr->SetWorldTransform(*this, InTransform); }
	bool GetRelativeTransform(FTransform& OutTransform) const { return InterfacePtr->GetRelativeTransform(*this, OutTransform); }
	bool SetRelativeTransform(const FTransform& InTransform) const { return InterfacePtr->SetRelativeTransform(*this, InTransform); }
	void NotifyMovementStarted() const { InterfacePtr->NotifyMovementStarted(*this); }
	void NotifyMovementOngoing() const { InterfacePtr->NotifyMovementOngoing(*this); }
	void NotifyMovementEnded() const { InterfacePtr->NotifyMovementEnded(*this); }
	bool FindSuitableTransformAtPoint(const FTransform& InPotentialTransform, FTransform& OutSuitableTransform) const { return InterfacePtr->FindSuitableTransformAtPoint(*this, InPotentialTransform, OutSuitableTransform); }
	bool FindSuitableTransformAlongPath(const FVector& InPathStart, const FVector& InPathEnd, const FCollisionShape& InTestShape, TArrayView<const FTypedElementHandle> InElementsToIgnore, FTransform& OutSuitableTransform) const { return InterfacePtr->FindSuitableTransformAlongPath(*this, InPathStart, InPathEnd, InTestShape, InElementsToIgnore, OutSuitableTransform); }
	FTypedElementHandle DuplicateElement(UWorld* InWorld, bool bOffsetLocations) const { return InterfacePtr->DuplicateElement(*this, InWorld, bOffsetLocations); }
};
