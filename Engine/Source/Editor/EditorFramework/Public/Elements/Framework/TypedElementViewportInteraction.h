// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InputState.h"
#include "UnrealWidgetFwd.h"
#include "Elements/Framework/TypedElementInterfaceCustomization.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "TypedElementViewportInteraction.generated.h"

class UTypedElementList;
class UTypedElementSelectionSet;

enum class ETypedElementViewportInteractionWorldType : uint8
{
	Editor,
	PlayInEditor,
};

/**
 * Customization used to allow asset editors (such as the level editor) to override the base behavior of viewport interaction.
 */
class EDITORFRAMEWORK_API FTypedElementViewportInteractionCustomization
{
public:
	virtual ~FTypedElementViewportInteractionCustomization() = default;

	using FElementToMoveFinalizerFunc = TFunction<void(const FTypedElementHandle&)>;
	using FElementToMoveFinalizerMap  = TMap<FTypedElementHandle, FElementToMoveFinalizerFunc>;

	//~ See UTypedElementViewportInteraction for API docs
	virtual void GetElementsToMove(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const ETypedElementViewportInteractionWorldType InWorldType, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToMove, FElementToMoveFinalizerMap& OutElementsToMoveFinalizers);
	virtual bool GetGizmoPivotLocation(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, FVector& OutPivotLocation);
	virtual void PreGizmoManipulationStarted(TArrayView<const FTypedElementHandle> InElementHandles, const UE::Widget::EWidgetMode InWidgetMode);
	virtual void GizmoManipulationStarted(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode);
	virtual void GizmoManipulationDeltaUpdate(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform, const FVector& InPivotLocation);
	virtual void GizmoManipulationStopped(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode);
	virtual void PostGizmoManipulationStopped(TArrayView<const FTypedElementHandle> InElementHandles, const UE::Widget::EWidgetMode InWidgetMode);
	virtual void MirrorElement(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const FVector& InMirrorScale, const FVector& InPivotLocation);
};

/**
 * Utility to hold a typed element handle and its associated world interface and  viewport interaction customization.
 */
struct EDITORFRAMEWORK_API FTypedElementViewportInteractionElement
{
public:
	FTypedElementViewportInteractionElement() = default;

	FTypedElementViewportInteractionElement(TTypedElement<UTypedElementWorldInterface> InElementWorldHandle, FTypedElementViewportInteractionCustomization* InViewportInteractionCustomization)
		: ElementWorldHandle(MoveTemp(InElementWorldHandle))
		, ViewportInteractionCustomization(InViewportInteractionCustomization)
	{
	}

	FTypedElementViewportInteractionElement(const FTypedElementViewportInteractionElement&) = default;
	FTypedElementViewportInteractionElement& operator=(const FTypedElementViewportInteractionElement&) = default;

	FTypedElementViewportInteractionElement(FTypedElementViewportInteractionElement&&) = default;
	FTypedElementViewportInteractionElement& operator=(FTypedElementViewportInteractionElement&&) = default;

	FORCEINLINE explicit operator bool() const
	{
		return IsSet();
	}

	FORCEINLINE bool IsSet() const
	{
		return ElementWorldHandle.IsSet()
			&& ViewportInteractionCustomization;
	}

	//~ See UTypedElementViewportInteraction for API docs
	void GetElementsToMove(const ETypedElementViewportInteractionWorldType InWorldType, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToMove, FTypedElementViewportInteractionCustomization::FElementToMoveFinalizerMap& OutElementsToMoveFinalizers) { ViewportInteractionCustomization->GetElementsToMove(ElementWorldHandle, InWorldType, InSelectionSet, OutElementsToMove, OutElementsToMoveFinalizers); }
	bool GetGizmoPivotLocation(const UE::Widget::EWidgetMode InWidgetMode, FVector& OutPivotLocation) const { return ViewportInteractionCustomization->GetGizmoPivotLocation(ElementWorldHandle, InWidgetMode, OutPivotLocation); }
	void GizmoManipulationStarted(const UE::Widget::EWidgetMode InWidgetMode) const { ViewportInteractionCustomization->GizmoManipulationStarted(ElementWorldHandle, InWidgetMode); }
	void GizmoManipulationDeltaUpdate(const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform, const FVector& InPivotLocation) const { ViewportInteractionCustomization->GizmoManipulationDeltaUpdate(ElementWorldHandle, InWidgetMode, InDragAxis, InInputState, InDeltaTransform, InPivotLocation); }
	void GizmoManipulationStopped(const UE::Widget::EWidgetMode InWidgetMode) const { ViewportInteractionCustomization->GizmoManipulationStopped(ElementWorldHandle, InWidgetMode); }
	void MirrorElement(const FVector& InMirrorScale, const FVector& InPivotLocation) const { ViewportInteractionCustomization->MirrorElement(ElementWorldHandle, InMirrorScale, InPivotLocation); }

private:
	TTypedElement<UTypedElementWorldInterface> ElementWorldHandle;
	FTypedElementViewportInteractionCustomization* ViewportInteractionCustomization = nullptr;
};

/**
 * A utility to handle higher-level viewport interactions, but default via UTypedElementWorldInterface,
 * but asset editors can customize this behavior via FTypedElementViewportInteractionCustomization.
 */
UCLASS(Transient)
class EDITORFRAMEWORK_API UTypedElementViewportInteraction : public UObject, public TTypedElementInterfaceCustomizationRegistry<FTypedElementViewportInteractionCustomization>
{
	GENERATED_BODY()

public:
	/**
	 * Get the elements from the given selection set that can be moved (eg, by a gizmo).
	 */
	void GetSelectedElementsToMove(const UTypedElementSelectionSet* InSelectionSet, const ETypedElementViewportInteractionWorldType InWorldType, UTypedElementList* OutElementsToMove) const;

	/**
	 * Notify that the gizmo is potentially about to start manipulating the transform of the given set of elements (calculated from calling GetSelectedElementsToMove).
	 */
	void BeginGizmoManipulation(const UTypedElementList* InElementsToMove, const UE::Widget::EWidgetMode InWidgetMode);

	/**
	 * Notify that the gizmo has manipulated the transform of the given set of elements (calculated from calling GetSelectedElementsToMove) by the given delta.
	 */
	void UpdateGizmoManipulation(const UTypedElementList* InElementsToMove, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform);
	
	/**
	 * Notify that the gizmo has finished manipulating the transform of the given set of elements (calculated from calling GetSelectedElementsToMove).
	 */
	void EndGizmoManipulation(const UTypedElementList* InElementsToMove, const UE::Widget::EWidgetMode InWidgetMode);

	/**
	 * Apply the given delta to the specified element.
	 * This performs a similar operation to an in-flight gizmo manipulation, but without any pre/post-change notification.
	 */
	void ApplyDeltaToElement(const FTypedElementHandle& InElementHandle, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform);

	/**
	 * Apply the given mirror scale to the specified element.
	 */
	void MirrorElement(const FTypedElementHandle& InElementHandle, const FVector& InMirrorScale);

private:
	/**
	 * Attempt to resolve the selection interface and viewport interaction customization for the given element, if any.
	 */
	FTypedElementViewportInteractionElement ResolveViewportInteractionElement(const FTypedElementHandle& InElementHandle) const;
};
