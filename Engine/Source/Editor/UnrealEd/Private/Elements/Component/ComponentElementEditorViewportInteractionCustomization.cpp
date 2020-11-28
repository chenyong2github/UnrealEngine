// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementEditorViewportInteractionCustomization.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/SceneComponent.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorSupportDelegates.h"

bool FComponentElementEditorViewportInteractionCustomization::GetGizmoPivotLocation(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, FVector& OutPivotLocation)
{
	const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementWorldHandle);

	if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		// If necessary, transform the editor pivot location to be relative to the component's parent
		const bool bIsRootComponent = SceneComponent->GetOwner()->GetRootComponent() == SceneComponent;
		OutPivotLocation = bIsRootComponent || !SceneComponent->GetAttachParent() ? GEditor->GetPivotLocation() : SceneComponent->GetAttachParent()->GetComponentToWorld().Inverse().TransformPosition(GEditor->GetPivotLocation());
		return true;
	}

	return false;
}

void FComponentElementEditorViewportInteractionCustomization::GizmoManipulationDeltaUpdate(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform, const FVector& InPivotLocation)
{
	UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementWorldHandle);

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		const FVector DeltaTranslation = InDeltaTransform.GetTranslation();
		const FRotator DeltaRotation = InDeltaTransform.Rotator();
		const FVector DeltaScale3D = InDeltaTransform.GetScale3D();

		FComponentElementEditorViewportInteractionCustomization::ApplyDeltaToComponent(SceneComponent, /*bDelta*/true, &DeltaTranslation, &DeltaRotation, &DeltaScale3D, InPivotLocation, InInputState);
	}
}

void FComponentElementEditorViewportInteractionCustomization::ApplyDeltaToComponent(USceneComponent* InComponent, const bool InIsDelta, const FVector* InDeltaTranslationPtr, const FRotator* InDeltaRotationPtr, const FVector* InDeltaScalePtr, const FVector& InPivotLocation, const FInputDeviceState& InInputState)
{
	if (GEditor->IsDeltaModificationEnabled())
	{
		InComponent->Modify();
	}

	///////////////////
	// Rotation
	if (InDeltaRotationPtr)
	{
		const FRotator& InDeltaRot = *InDeltaRotationPtr;
		const bool bRotatingComp = !InIsDelta || !InDeltaRot.IsZero();
		if (bRotatingComp)
		{
			if (InIsDelta)
			{
				const FRotator Rot = InComponent->GetRelativeRotation();
				FRotator ActorRotWind, ActorRotRem;
				Rot.GetWindingAndRemainder(ActorRotWind, ActorRotRem);
				const FQuat ActorQ = ActorRotRem.Quaternion();
				const FQuat DeltaQ = InDeltaRot.Quaternion();
				const FQuat ResultQ = DeltaQ * ActorQ;

				FRotator NewActorRotRem = FRotator(ResultQ);
				ActorRotRem.SetClosestToMe(NewActorRotRem);
				FRotator DeltaRot = NewActorRotRem - ActorRotRem;
				DeltaRot.Normalize();
				InComponent->SetRelativeRotationExact(Rot + DeltaRot);
			}
			else
			{
				InComponent->SetRelativeRotationExact(InDeltaRot);
			}

			if (InIsDelta)
			{
				FVector NewCompLocation = InComponent->GetRelativeLocation();
				NewCompLocation -= InPivotLocation;
				NewCompLocation = FRotationMatrix(InDeltaRot).TransformPosition(NewCompLocation);
				NewCompLocation += InPivotLocation;
				InComponent->SetRelativeLocation(NewCompLocation);
			}
		}
	}

	///////////////////
	// Translation
	if (InDeltaTranslationPtr)
	{
		if (InIsDelta)
		{
			InComponent->SetRelativeLocation(InComponent->GetRelativeLocation() + *InDeltaTranslationPtr);
		}
		else
		{
			InComponent->SetRelativeLocation(*InDeltaTranslationPtr);
		}
	}

	///////////////////
	// Scaling
	if (InDeltaScalePtr)
	{
		const FVector& InDeltaScale = *InDeltaScalePtr;
		const bool bScalingComp = !InIsDelta || !InDeltaScale.IsNearlyZero(0.000001f);
		if (bScalingComp)
		{
			if (InIsDelta)
			{
				InComponent->SetRelativeScale3D(InComponent->GetRelativeScale3D() + InDeltaScale);

				FVector NewCompLocation = InComponent->GetRelativeLocation();
				NewCompLocation -= InPivotLocation;
				NewCompLocation += FScaleMatrix(InDeltaScale).TransformPosition(NewCompLocation);
				NewCompLocation += InPivotLocation;
				InComponent->SetRelativeLocation(NewCompLocation);
			}
			else
			{
				InComponent->SetRelativeScale3D(InDeltaScale);
			}
		}
	}

	// Update the actor before leaving.
	InComponent->MarkPackageDirty();

	InComponent->PostEditComponentMove(false);

	// Fire callbacks
	FEditorSupportDelegates::RefreshPropertyWindows.Broadcast();
	FEditorSupportDelegates::UpdateUI.Broadcast();
}
