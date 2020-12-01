// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementViewportInteraction.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Framework/TypedElementRegistry.h"

void FTypedElementAssetEditorViewportInteractionCustomization::GetElementsToMove(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const ETypedElementViewportInteractionWorldType InWorldType, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToMove)
{
	OutElementsToMove->Add(InElementWorldHandle);
}

bool FTypedElementAssetEditorViewportInteractionCustomization::GetGizmoPivotLocation(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, FVector& OutPivotLocation)
{
	FTransform ElementWorldTransform;
	if (InElementWorldHandle.GetWorldTransform(ElementWorldTransform))
	{
		OutPivotLocation = ElementWorldTransform.GetTranslation();
		return true;
	}

	return false;
}

void FTypedElementAssetEditorViewportInteractionCustomization::PreGizmoManipulationStarted(TArrayView<const FTypedElementHandle> InElementHandles, const UE::Widget::EWidgetMode InWidgetMode)
{
}

void FTypedElementAssetEditorViewportInteractionCustomization::GizmoManipulationStarted(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode)
{
	InElementWorldHandle.NotifyMovementStarted();
}

void FTypedElementAssetEditorViewportInteractionCustomization::GizmoManipulationDeltaUpdate(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform, const FVector& InPivotLocation)
{
	FTransform ElementWorldTransform;
	if (InElementWorldHandle.GetWorldTransform(ElementWorldTransform))
	{
		// Apply delta rotation around the pivot location
		{
			const FQuat DeltaRotation = InDeltaTransform.GetRotation();
			if (!DeltaRotation.Rotator().IsZero())
			{
				ElementWorldTransform.SetRotation(ElementWorldTransform.GetRotation() * DeltaRotation);

				FVector ElementLocation = ElementWorldTransform.GetTranslation();
				ElementLocation -= InPivotLocation;
				ElementLocation = FRotationMatrix::Make(DeltaRotation).TransformPosition(ElementLocation);
				ElementLocation += InPivotLocation;
				ElementWorldTransform.SetTranslation(ElementLocation);
			}
		}

		// Apply delta translation
		{
			const FVector DeltaTranslation = InDeltaTransform.GetTranslation();
			ElementWorldTransform.SetTranslation(ElementWorldTransform.GetTranslation() + DeltaTranslation);
		}

		// Apply delta scaling around the pivot location
		{
			const FVector DeltaScale3D = InDeltaTransform.GetScale3D();
			if (!DeltaScale3D.IsNearlyZero(0.000001f))
			{
				ElementWorldTransform.SetScale3D(ElementWorldTransform.GetScale3D() + DeltaScale3D);

				FVector ElementLocation = ElementWorldTransform.GetTranslation();
				ElementLocation -= InPivotLocation;
				ElementLocation += FScaleMatrix::Make(DeltaScale3D).TransformPosition(ElementLocation);
				ElementLocation += InPivotLocation;
				ElementWorldTransform.SetTranslation(ElementLocation);
			}
		}

		InElementWorldHandle.SetWorldTransform(ElementWorldTransform);
		InElementWorldHandle.NotifyMovementOngoing();
	}
}

void FTypedElementAssetEditorViewportInteractionCustomization::GizmoManipulationStopped(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode)
{
	InElementWorldHandle.NotifyMovementEnded();
}

void FTypedElementAssetEditorViewportInteractionCustomization::PostGizmoManipulationStopped(TArrayView<const FTypedElementHandle> InElementHandles, const UE::Widget::EWidgetMode InWidgetMode)
{
}

void FTypedElementAssetEditorViewportInteractionCustomization::MirrorElement(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const FVector& InMirrorScale, const FVector& InPivotLocation)
{
	FTransform ElementWorldTransform;
	if (InElementWorldHandle.GetWorldTransform(ElementWorldTransform))
	{
		InElementWorldHandle.NotifyMovementStarted();

		// Apply mirrored rotation
		{
			// Revert the handedness of the rotation, but make up for it in the scaling
			// Arbitrarily choose the X axis to remain fixed
			const FMatrix TempRot = FRotationMatrix::Make(ElementWorldTransform.GetRotation());
			const FMatrix NewRot(
				-TempRot.GetScaledAxis(EAxis::X) * InMirrorScale, 
				TempRot.GetScaledAxis(EAxis::Y) * InMirrorScale, 
				TempRot.GetScaledAxis(EAxis::Z) * InMirrorScale, 
				FVector::ZeroVector
				);
			ElementWorldTransform.SetRotation(NewRot.ToQuat());
		}

		// Apply mirrored location around the pivot location
		{
			FVector Loc = ElementWorldTransform.GetTranslation();
			Loc -= InPivotLocation;
			Loc *= InMirrorScale;
			Loc += InPivotLocation;
			ElementWorldTransform.SetTranslation(Loc);
		}

		InElementWorldHandle.SetWorldTransform(ElementWorldTransform);

		// Apply mirrored relative scale
		{
			FTransform ElementRelativeTransform;
			if (InElementWorldHandle.GetRelativeTransform(ElementRelativeTransform))
			{
				FVector Scale3D = ElementRelativeTransform.GetScale3D();
				Scale3D.X = -Scale3D.X;
				ElementRelativeTransform.SetScale3D(Scale3D);

				InElementWorldHandle.SetRelativeTransform(ElementRelativeTransform);
			}
		}

		InElementWorldHandle.NotifyMovementEnded();
	}
}


void UTypedElementViewportInteraction::GetSelectedElementsToMove(const UTypedElementSelectionSet* InSelectionSet, const ETypedElementViewportInteractionWorldType InWorldType, UTypedElementList* OutElementsToMove) const
{
	OutElementsToMove->Reset();
	InSelectionSet->ForEachSelectedElement<UTypedElementWorldInterface>([this, InSelectionSet, InWorldType, OutElementsToMove](const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle)
	{
		if (InElementWorldHandle.CanEditElement())
		{
			FTypedElementViewportInteractionElement ViewportInteractionElement(InElementWorldHandle, GetAssetEditorCustomizationByTypeId(InElementWorldHandle.GetId().GetTypeId()));
			check(ViewportInteractionElement.IsSet());
			ViewportInteractionElement.GetElementsToMove(InWorldType, InSelectionSet, OutElementsToMove);
		}
		return true;
	});
}

void UTypedElementViewportInteraction::BeginGizmoManipulation(const UTypedElementList* InElementsToMove, const UE::Widget::EWidgetMode InWidgetMode)
{
	{
		TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToMoveByType;
		BatchElementsByType(InElementsToMove, ElementsToMoveByType);

		for (const auto& ElementsByTypePair : ElementsToMoveByType)
		{
			FTypedElementAssetEditorViewportInteractionCustomization* AssetEditorViewportInteractionCustomization = GetAssetEditorCustomizationByTypeId(ElementsByTypePair.Key);
			check(AssetEditorViewportInteractionCustomization);
			AssetEditorViewportInteractionCustomization->PreGizmoManipulationStarted(ElementsByTypePair.Value, InWidgetMode);
		}
	}

	InElementsToMove->ForEachElementHandle([this, InWidgetMode](const FTypedElementHandle& InElementToMove)
	{
		FTypedElementViewportInteractionElement ViewportInteractionElement = ResolveViewportInteractionElement(InElementToMove);
		if (ViewportInteractionElement)
		{
			ViewportInteractionElement.GizmoManipulationStarted(InWidgetMode);
		}
		return true;
	});
}

void UTypedElementViewportInteraction::UpdateGizmoManipulation(const UTypedElementList* InElementsToMove, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform)
{
	InElementsToMove->ForEachElementHandle([this, InWidgetMode, InDragAxis, &InInputState, &InDeltaTransform](const FTypedElementHandle& InElementToMove)
	{
		FTypedElementViewportInteractionElement ViewportInteractionElement = ResolveViewportInteractionElement(InElementToMove);
		if (ViewportInteractionElement)
		{
			FVector PivotLocation = FVector::ZeroVector;
			ViewportInteractionElement.GetGizmoPivotLocation(InWidgetMode, PivotLocation);
			ViewportInteractionElement.GizmoManipulationDeltaUpdate(InWidgetMode, InDragAxis, InInputState, InDeltaTransform, PivotLocation);
		}
		return true;
	});
}

void UTypedElementViewportInteraction::EndGizmoManipulation(const UTypedElementList* InElementsToMove, const UE::Widget::EWidgetMode InWidgetMode)
{
	InElementsToMove->ForEachElementHandle([this, InWidgetMode](const FTypedElementHandle& InElementToMove)
	{
		FTypedElementViewportInteractionElement ViewportInteractionElement = ResolveViewportInteractionElement(InElementToMove);
		if (ViewportInteractionElement)
		{
			ViewportInteractionElement.GizmoManipulationStopped(InWidgetMode);
		}
		return true;
	});
	
	{
		TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToMoveByType;
		BatchElementsByType(InElementsToMove, ElementsToMoveByType);

		for (const auto& ElementsByTypePair : ElementsToMoveByType)
		{
			FTypedElementAssetEditorViewportInteractionCustomization* AssetEditorViewportInteractionCustomization = GetAssetEditorCustomizationByTypeId(ElementsByTypePair.Key);
			check(AssetEditorViewportInteractionCustomization);
			AssetEditorViewportInteractionCustomization->PostGizmoManipulationStopped(ElementsByTypePair.Value, InWidgetMode);
		}
	}
}

void UTypedElementViewportInteraction::ApplyDeltaToElement(const FTypedElementHandle& InElementHandle, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform)
{
	FTypedElementViewportInteractionElement ViewportInteractionElement = ResolveViewportInteractionElement(InElementHandle);
	if (ViewportInteractionElement)
	{
		FVector PivotLocation = FVector::ZeroVector;
		ViewportInteractionElement.GetGizmoPivotLocation(InWidgetMode, PivotLocation);
		ViewportInteractionElement.GizmoManipulationDeltaUpdate(InWidgetMode, InDragAxis, InInputState, InDeltaTransform, PivotLocation);
	}
}

void UTypedElementViewportInteraction::MirrorElement(const FTypedElementHandle& InElementHandle, const FVector& InMirrorScale)
{
	FTypedElementViewportInteractionElement ViewportInteractionElement = ResolveViewportInteractionElement(InElementHandle);
	if (ViewportInteractionElement)
	{
		FVector PivotLocation = FVector::ZeroVector;
		ViewportInteractionElement.GetGizmoPivotLocation(UE::Widget::WM_None, PivotLocation);
		ViewportInteractionElement.MirrorElement(InMirrorScale, PivotLocation);
	}
}

FTypedElementViewportInteractionElement UTypedElementViewportInteraction::ResolveViewportInteractionElement(const FTypedElementHandle& InElementHandle) const
{
	return InElementHandle
		? FTypedElementViewportInteractionElement(UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(InElementHandle), GetAssetEditorCustomizationByTypeId(InElementHandle.GetId().GetTypeId()))
		: FTypedElementViewportInteractionElement();
}

void UTypedElementViewportInteraction::BatchElementsByType(const UTypedElementList* InElementsToMove, TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>>& OutElementsByType)
{
	OutElementsByType.Reset();
	InElementsToMove->ForEachElementHandle([&OutElementsByType](const FTypedElementHandle& InElementHandle)
	{
		TArray<FTypedElementHandle>& ElementsForType = OutElementsByType.FindOrAdd(InElementHandle.GetId().GetTypeId());
		ElementsForType.Add(InElementHandle);
		return true;
	});
}
