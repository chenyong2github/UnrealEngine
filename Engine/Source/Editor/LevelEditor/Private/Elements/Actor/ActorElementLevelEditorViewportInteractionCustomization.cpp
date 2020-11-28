// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementLevelEditorViewportInteractionCustomization.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Component/ComponentElementSelectionInterface.h"
#include "Elements/Component/ComponentElementLevelEditorViewportInteractionCustomization.h"

#include "Editor.h"
#include "Engine/Brush.h"
#include "Editor/GroupActor.h"
#include "ActorGroupingUtils.h"
#include "LevelEditorViewport.h"

FActorElementLevelEditorViewportInteractionCustomization::FActorElementLevelEditorViewportInteractionCustomization(FLevelEditorViewportClient* InLevelEditorViewportClient)
	: LevelEditorViewportClient(InLevelEditorViewportClient)
{
}

void FActorElementLevelEditorViewportInteractionCustomization::GetElementsToMove(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const ETypedElementViewportInteractionWorldType InWorldType, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToMove)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InElementWorldHandle);

	if (UComponentElementSelectionInterface::HasSelectedComponents(InSelectionSet->GetElementList()))
	{
		// If we have components selected then we will move those rather than the actors
		// The component may still choose to move its owner actor rather than itself
		return;
	}

	if (CanMoveActorInViewport(Actor, InWorldType))
	{
		AppendActorsToMove(Actor, InSelectionSet, OutElementsToMove);
	}
}

void FActorElementLevelEditorViewportInteractionCustomization::GizmoManipulationStarted(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InElementWorldHandle);

	// Notify that this actor is beginning to move
	GEditor->BroadcastBeginObjectMovement(*Actor);

	// Broadcast Pre Edit change notification, we can't call PreEditChange directly on Actor or ActorComponent from here since it will unregister the components until PostEditChange
	if (FProperty* TransformProperty = FComponentElementLevelEditorViewportInteractionCustomization::GetEditTransformProperty(InWidgetMode))
	{
		FEditPropertyChain PropertyChain;
		PropertyChain.AddHead(TransformProperty);
		FCoreUObjectDelegates::OnPreObjectPropertyChanged.Broadcast(Actor, PropertyChain);
	}
}

void FActorElementLevelEditorViewportInteractionCustomization::GizmoManipulationDeltaUpdate(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode, const EAxisList::Type InDragAxis, const FInputDeviceState& InInputState, const FTransform& InDeltaTransform, const FVector& InPivotLocation)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InElementWorldHandle);

	GEditor->NoteActorMovement();

	FTransform ModifiedDeltaTransform = InDeltaTransform;

	{
		FVector AdjustedScale = ModifiedDeltaTransform.GetScale3D();

		// If we are scaling, we may need to change the scaling factor a bit to properly align to the grid
		if (AdjustedScale.IsNearlyZero())
		{
			// We don't scale actors when we only have a very small scale change
			AdjustedScale = FVector::ZeroVector;
		}
		else if (!GEditor->UsePercentageBasedScaling())
		{
			ModifyScale(Actor, InDragAxis, AdjustedScale, Actor->IsA<ABrush>());
		}

		ModifiedDeltaTransform.SetScale3D(AdjustedScale);
	}

	FActorElementEditorViewportInteractionCustomization::GizmoManipulationDeltaUpdate(InElementWorldHandle, InWidgetMode, InDragAxis, InInputState, ModifiedDeltaTransform, InPivotLocation);

	// Update the cameras from their locked actor (if any) only if the viewport is real-time enabled
	LevelEditorViewportClient->UpdateLockedActorViewports(Actor, true);
}

void FActorElementLevelEditorViewportInteractionCustomization::GizmoManipulationStopped(const TTypedElement<UTypedElementWorldInterface>& InElementWorldHandle, const UE::Widget::EWidgetMode InWidgetMode)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InElementWorldHandle);

	// Broadcast Post Edit change notification, we can't call PostEditChangeProperty directly on Actor or ActorComponent from here since it wasn't pair with a proper PreEditChange
	if (FProperty* TransformProperty = FComponentElementLevelEditorViewportInteractionCustomization::GetEditTransformProperty(InWidgetMode))
	{
		FPropertyChangedEvent PropertyChangedEvent(TransformProperty, EPropertyChangeType::ValueSet);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Actor, PropertyChangedEvent);
	}

	Actor->PostEditMove(true);
	GEditor->BroadcastEndObjectMovement(*Actor);
}

void FActorElementLevelEditorViewportInteractionCustomization::PostGizmoManipulationStopped(TArrayView<const TTypedElement<UTypedElementWorldInterface>> InElementWorldHandles, const UE::Widget::EWidgetMode InWidgetMode)
{
	TArray<AActor*> MovedActors = ActorElementDataUtil::GetActorsFromHandlesChecked(InElementWorldHandles);
	GEditor->BroadcastActorsMoved(MovedActors);
}

void FActorElementLevelEditorViewportInteractionCustomization::ModifyScale(AActor* InActor, const EAxisList::Type InDragAxis, FVector& ScaleDelta, bool bCheckSmallExtent) const
{
	if (InActor->GetRootComponent())
	{
		const FVector CurrentScale = InActor->GetRootComponent()->GetRelativeScale3D();

		const FBox LocalBox = InActor->GetComponentsBoundingBox(true);
		const FVector ScaledExtents = LocalBox.GetExtent() * CurrentScale;
		const FTransform PreDragTransform = LevelEditorViewportClient->CachePreDragActorTransform(InActor);

		FComponentElementLevelEditorViewportInteractionCustomization::ValidateScale(PreDragTransform.GetScale3D(), InDragAxis, CurrentScale, ScaledExtents, ScaleDelta, bCheckSmallExtent);

		if (ScaleDelta.IsNearlyZero())
		{
			ScaleDelta = FVector::ZeroVector;
		}
	}
}

bool FActorElementLevelEditorViewportInteractionCustomization::CanMoveActorInViewport(const AActor* InActor, const ETypedElementViewportInteractionWorldType InWorldType)
{
	if (!GEditor || !InActor)
	{
		return false;
	}

	// The actor cannot be location locked
	if (InActor->bLockLocation)
	{
		return false;
	}

	// The actor needs to be in the current viewport world
	if (GEditor->PlayWorld)
	{
		const UWorld* CurrentWorld = InActor->GetWorld();
		const UWorld* RequiredWorld = InWorldType == ETypedElementViewportInteractionWorldType::PlayInEditor ? GEditor->PlayWorld : GEditor->EditorWorld;
		if (CurrentWorld != RequiredWorld)
		{
			return false;
		}
	}

	return true;
}

void FActorElementLevelEditorViewportInteractionCustomization::AppendActorsToMove(AActor* InActor, const UTypedElementSelectionSet* InSelectionSet, UTypedElementList* OutElementsToMove)
{
	auto AddActorElement = [&OutElementsToMove](AActor* InActorToAdd)
	{
		FTypedElementHandle ActorElementHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActorToAdd);
		if (ActorElementHandle)
		{
			OutElementsToMove->Add(MoveTemp(ActorElementHandle));
		}
	};

	AGroupActor* ParentGroup = AGroupActor::GetRootForActor(InActor, true, true);
	if (ParentGroup && UActorGroupingUtils::IsGroupingActive())
	{
		ParentGroup->ForEachMovableActorInGroup(InSelectionSet, AddActorElement);
	}
	else
	{
		AddActorElement(InActor);
	}
}
