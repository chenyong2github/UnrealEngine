// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementEditorViewportInteractionCustomization.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "Toolkits/IToolkitHost.h"
#include "AI/NavigationSystemBase.h"

bool FActorElementEditorViewportInteractionCustomization::GetGizmoPivotLocation(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, FVector& OutPivotLocation)
{
	if (const IToolkitHost* ToolkitHostPtr = GetToolkitHost())
	{
		OutPivotLocation = ToolkitHostPtr->GetEditorModeManager().PivotLocation;
		return true;
	}
	
	return FTypedElementAssetEditorViewportInteractionCustomization::GetGizmoPivotLocation(InElementWorldHandle, InWidgetMode, OutPivotLocation);
}

void FActorElementEditorViewportInteractionCustomization::GizmoManipulationDeltaUpdate(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform, const FVector& InPivotLocation)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InElementWorldHandle);

	const FVector DeltaTranslation = InDeltaTransform.GetTranslation();
	const FRotator DeltaRotation = InDeltaTransform.Rotator();
	const FVector DeltaScale3D = InDeltaTransform.GetScale3D();

	FActorElementEditorViewportInteractionCustomization::ApplyDeltaToActor(Actor, /*bDelta*/true, &DeltaTranslation, &DeltaRotation, &DeltaScale3D, InPivotLocation, InInputState);
}

void FActorElementEditorViewportInteractionCustomization::ApplyDeltaToActor(AActor* InActor, const bool InIsDelta, const FVector* InDeltaTranslationPtr, const FRotator* InDeltaRotationPtr, const FVector* InDeltaScalePtr, const FVector& InPivotLocation, const FInputDeviceState& InInputState)
{
	const bool bIsSimulatingInEditor = GEditor->IsSimulatingInEditor();

	if (GEditor->IsDeltaModificationEnabled())
	{
		InActor->Modify();
	}

	FNavigationLockContext LockNavigationUpdates(InActor->GetWorld(), ENavigationLockReason::ContinuousEditorMove);

	bool bTranslationOnly = true;

	///////////////////
	// Rotation

	// Unfortunately this can't be moved into ABrush::EditorApplyRotation, as that would
	// create a dependence in Engine on Editor.
	if (InDeltaRotationPtr)
	{
		const FRotator& InDeltaRot = *InDeltaRotationPtr;
		const bool bRotatingActor = !InIsDelta || !InDeltaRot.IsZero();
		if (bRotatingActor)
		{
			bTranslationOnly = false;

			if (InIsDelta)
			{
				if (InActor->GetRootComponent())
				{
					const FRotator OriginalRotation = InActor->GetRootComponent()->GetComponentRotation();

					InActor->EditorApplyRotation(InDeltaRot, InInputState.bAltKeyDown, InInputState.bShiftKeyDown, InInputState.bCtrlKeyDown);

					// Check to see if we should transform the rigid body
					UPrimitiveComponent* RootPrimitiveComponent = Cast<UPrimitiveComponent>(InActor->GetRootComponent());
					if (bIsSimulatingInEditor && GIsPlayInEditorWorld && RootPrimitiveComponent)
					{
						FRotator ActorRotWind, ActorRotRem;
						OriginalRotation.GetWindingAndRemainder(ActorRotWind, ActorRotRem);

						const FQuat ActorQ = ActorRotRem.Quaternion();
						const FQuat DeltaQ = InDeltaRot.Quaternion();
						const FQuat ResultQ = DeltaQ * ActorQ;

						const FRotator NewActorRotRem = FRotator(ResultQ);
						FRotator DeltaRot = NewActorRotRem - ActorRotRem;
						DeltaRot.Normalize();

						// @todo SIE: Not taking into account possible offset between root component and actor
						RootPrimitiveComponent->SetWorldRotation(OriginalRotation + DeltaRot);
					}
				}

				FVector NewActorLocation = InActor->GetActorLocation();
				NewActorLocation -= InPivotLocation;
				NewActorLocation = FRotationMatrix(InDeltaRot).TransformPosition(NewActorLocation);
				NewActorLocation += InPivotLocation;
				NewActorLocation -= InActor->GetActorLocation();
				InActor->EditorApplyTranslation(NewActorLocation, InInputState.bAltKeyDown, InInputState.bShiftKeyDown, InInputState.bCtrlKeyDown);
			}
			else
			{
				InActor->SetActorRotation(InDeltaRot);
			}
		}
	}

	///////////////////
	// Translation
	if (InDeltaTranslationPtr)
	{
		if (InIsDelta)
		{
			if (InActor->GetRootComponent())
			{
				const FVector OriginalLocation = InActor->GetRootComponent()->GetComponentLocation();

				InActor->EditorApplyTranslation(*InDeltaTranslationPtr, InInputState.bAltKeyDown, InInputState.bShiftKeyDown, InInputState.bCtrlKeyDown);

				// Check to see if we should transform the rigid body
				UPrimitiveComponent* RootPrimitiveComponent = Cast<UPrimitiveComponent>(InActor->GetRootComponent());
				if (bIsSimulatingInEditor && GIsPlayInEditorWorld && RootPrimitiveComponent)
				{
					// @todo SIE: Not taking into account possible offset between root component and actor
					RootPrimitiveComponent->SetWorldLocation(OriginalLocation + *InDeltaTranslationPtr);
				}
			}
		}
		else
		{
			InActor->SetActorLocation(*InDeltaTranslationPtr, false);
		}
	}

	///////////////////
	// Scaling
	if (InDeltaScalePtr)
	{
		const FVector& InDeltaScale = *InDeltaScalePtr;
		const bool bScalingActor = !InIsDelta || !InDeltaScale.IsNearlyZero(0.000001f);
		if (bScalingActor)
		{
			bTranslationOnly = false;

			FVector ModifiedScale = InDeltaScale;

			// Note: With the new additive scaling method, this is handled in FLevelEditorViewportClient::ModifyScale
			if (GEditor->UsePercentageBasedScaling())
			{
				// Get actor box extents
				const FBox BoundingBox = InActor->GetComponentsBoundingBox(true);
				const FVector BoundsExtents = BoundingBox.GetExtent();

				// Make sure scale on actors is clamped to a minimum and maximum size.
				const float MinThreshold = 1.0f;

				for (int32 Idx = 0; Idx < 3; Idx++)
				{
					if ((FMath::Pow(BoundsExtents[Idx], 2)) > BIG_NUMBER)
					{
						ModifiedScale[Idx] = 0.0f;
					}
					else if (SMALL_NUMBER < BoundsExtents[Idx])
					{
						const bool bBelowAllowableScaleThreshold = ((InDeltaScale[Idx] + 1.0f) * BoundsExtents[Idx]) < MinThreshold;

						if (bBelowAllowableScaleThreshold)
						{
							ModifiedScale[Idx] = (MinThreshold / BoundsExtents[Idx]) - 1.0f;
						}
					}
				}
			}

			if (InIsDelta)
			{
				// Flag actors to use old-style scaling or not
				// @todo: Remove this hack once we have decided on the scaling method to use.
				AActor::bUsePercentageBasedScaling = GEditor->UsePercentageBasedScaling();

				InActor->EditorApplyScale(
					ModifiedScale,
					&InPivotLocation,
					InInputState.bAltKeyDown,
					InInputState.bShiftKeyDown,
					InInputState.bCtrlKeyDown
					);

			}
			else if (InActor->GetRootComponent())
			{
				InActor->GetRootComponent()->SetRelativeScale3D(InDeltaScale);
			}
		}
	}

	// Update the actor before leaving.
	InActor->MarkPackageDirty();
	if (!GIsDemoMode)
	{
		InActor->InvalidateLightingCacheDetailed(bTranslationOnly);
	}
	InActor->PostEditMove(false);
}
