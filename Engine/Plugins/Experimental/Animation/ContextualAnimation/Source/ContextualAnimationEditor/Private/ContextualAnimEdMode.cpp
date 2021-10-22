// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimEdMode.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"

const FEditorModeID FContextualAnimEdMode::EdModeId = TEXT("ContextualAnimEdMode");

FContextualAnimEdMode::FContextualAnimEdMode()
{
}

FContextualAnimEdMode::~FContextualAnimEdMode()
{
}

bool FContextualAnimEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	bool bHandled = false;

	if (HitProxy != nullptr && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);

		SelectedActor = ActorHitProxy->Actor;

		bHandled = true;
	}

	return bHandled;
}

bool FContextualAnimEdMode::GetHitResultUnderCursor(FHitResult& OutHitResult, FEditorViewportClient* InViewportClient, const FViewportClick& Click) const
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(InViewportClient->Viewport, InViewportClient->GetScene(), InViewportClient->EngineShowFlags).SetRealtimeUpdate(InViewportClient->IsRealtime()));
	FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation Cursor(View, InViewportClient, Click.GetClickPos().X, Click.GetClickPos().Y);
	const auto ViewportType = InViewportClient->GetViewportType();

	const FVector RayStart = Cursor.GetOrigin();
	const FVector RayEnd = RayStart + Cursor.GetDirection() * HALF_WORLD_MAX;

	return InViewportClient->GetWorld()->LineTraceSingleByChannel(OutHitResult, RayStart, RayEnd, ECC_WorldStatic, FCollisionQueryParams::DefaultQueryParam);
}

bool FContextualAnimEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return FEdMode::StartTracking(InViewportClient, InViewport);
}

bool FContextualAnimEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return FEdMode::EndTracking(InViewportClient, InViewport);
}

bool FContextualAnimEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	bool bHandled = false;

	EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();

	if (CurrentAxis != EAxisList::None)
	{
		if (SelectedActor.IsValid())
		{
			SelectedActor->AddActorWorldTransform(FTransform(InRot, InDrag));
			bHandled = true;
		}
	}

	return bHandled;
}

bool FContextualAnimEdMode::AllowWidgetMove()
{
	return ShouldDrawWidget();
}

bool FContextualAnimEdMode::ShouldDrawWidget() const
{
	return SelectedActor.IsValid();
}

FVector FContextualAnimEdMode::GetWidgetLocation() const
{
	if (SelectedActor.IsValid())
	{
		return SelectedActor->GetActorLocation();
	}

	return FVector::ZeroVector;
}
