// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorRestSpaceViewportClient.h"

#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "EngineUtils.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "Behaviors/2DViewportBehaviorTargets.h"
#include "EdModeInteractiveToolsContext.h"
#include "CameraController.h"
#include "Components/DynamicMeshComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "SceneView.h"

namespace UE::Chaos::ClothAsset
{

FChaosClothEditorRestSpaceViewportClient::FChaosClothEditorRestSpaceViewportClient(FEditorModeTools* InModeTools,
	FPreviewScene* InPreviewScene,
	const TWeakPtr<SEditorViewport>& InEditorViewportWidget) :
	FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget)
{
	OverrideNearClipPlane(UE_KINDA_SMALL_NUMBER);
	OverrideFarClipPlane(0);

	BehaviorSet = NewObject<UInputBehaviorSet>();

	// We'll have the priority of our viewport manipulation behaviors be lower (i.e. higher
	// numerically) than both the gizmo default and the tool default.
	constexpr int ViewportBehaviorPriority = 150;

	ScrollBehaviorTarget = MakeUnique<FEditor2DScrollBehaviorTarget>(this);
	UClickDragInputBehavior* const ScrollBehavior = NewObject<UClickDragInputBehavior>();
	ScrollBehavior->Initialize(ScrollBehaviorTarget.Get());
	ScrollBehavior->SetDefaultPriority(ViewportBehaviorPriority);
	ScrollBehavior->SetUseRightMouseButton();
	BehaviorSet->Add(ScrollBehavior);
	BehaviorsFor2DMode.Add(ScrollBehavior);

	ZoomBehaviorTarget = MakeUnique<FEditor2DMouseWheelZoomBehaviorTarget>(this);
	constexpr float CameraFarPlaneWorldZ = -10.0f;
	constexpr float CameraNearPlaneProportionZ = 0.8f;
	ZoomBehaviorTarget->SetCameraFarPlaneWorldZ(CameraFarPlaneWorldZ);
	ZoomBehaviorTarget->SetCameraNearPlaneProportionZ(CameraNearPlaneProportionZ);
	ZoomBehaviorTarget->SetZoomLimits(0.001, 100000);
	UMouseWheelInputBehavior* const ZoomBehavior = NewObject<UMouseWheelInputBehavior>();
	ZoomBehavior->Initialize(ZoomBehaviorTarget.Get());
	ZoomBehavior->SetDefaultPriority(ViewportBehaviorPriority);
	BehaviorSet->Add(ZoomBehavior);
	BehaviorsFor2DMode.Add(ZoomBehavior);

	EngineShowFlags.SetSelectionOutline(true);
	ModeTools->GetInteractiveToolsContext()->InputRouter->RegisterSource(this);
}

void FChaosClothEditorRestSpaceViewportClient::Set2DMode(bool In2DMode)
{
	b2DMode = In2DMode;

	BehaviorSet->RemoveAll();
	ModeTools->GetInteractiveToolsContext()->InputRouter->DeregisterSource(this);

	if (b2DMode)
	{
		for (UInputBehavior* const Behavior : BehaviorsFor2DMode)
		{
			BehaviorSet->Add(Behavior);
		}
		ModeTools->GetInteractiveToolsContext()->InputRouter->RegisterSource(this);

		const double AbsZ = FMath::Abs(ViewTransformPerspective.GetLocation().Z);
		constexpr double CameraFarPlaneWorldZ = -10.0;
		constexpr double CameraNearPlaneProportionZ = 0.8;
		OverrideFarClipPlane(static_cast<float>(AbsZ - CameraFarPlaneWorldZ));
		OverrideNearClipPlane(static_cast<float>(AbsZ * (1.0 - CameraNearPlaneProportionZ)));
	}
	else
	{
		OverrideFarClipPlane(0);
		OverrideNearClipPlane(UE_KINDA_SMALL_NUMBER);
	}

}

const UInputBehaviorSet* FChaosClothEditorRestSpaceViewportClient::GetInputBehaviors() const
{
	return BehaviorSet;
}

// Collects UObjects that we don't want the garbage collecter to clean up
void FChaosClothEditorRestSpaceViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(BehaviorSet);
	Collector.AddReferencedObjects(BehaviorsFor2DMode);
}

bool FChaosClothEditorRestSpaceViewportClient::ShouldOrbitCamera() const
{
	if (b2DMode)
	{
		return false;
	}
	else
	{
		return FEditorViewportClient::ShouldOrbitCamera();
	}
}

bool FChaosClothEditorRestSpaceViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	// See if any tool commands want to handle the key event
	const TSharedPtr<FUICommandList> PinnedToolCommandList = ToolCommandList.Pin();
	if (EventArgs.Event != IE_Released && PinnedToolCommandList.IsValid())
	{
		const FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
		if (PinnedToolCommandList->ProcessCommandBindings(EventArgs.Key, KeyState, (EventArgs.Event == IE_Repeat)))
		{
			return true;
		}
	}

	if (!b2DMode)
	{
		return FEditorViewportClient::InputKey(EventArgs);
	}


	// We'll support disabling input like our base class, even if it does not end up being used.
	if (bDisableInput)
	{
		return true;
	}

	// Our viewport manipulation is placed in the input router that ModeTools manages
	if (ModeTools->InputKey(this, EventArgs.Viewport, EventArgs.Key, EventArgs.Event))
	{
		return true;
	}
	
	//
	// Recreate the click handling from FEditorViewportClient::Internal_InputKey so that we can still have 
	// mouse click, keyboard, etc., but skip the code dealing with camera controls
	//

	bool bHandled = false;

	const FInputEventState InputState(EventArgs.Viewport, EventArgs.Key, EventArgs.Event);
	const FViewport* const InViewport = EventArgs.Viewport;
	const EInputEvent Event = EventArgs.Event;

	const bool bWasCursorVisible = InViewport->IsCursorVisible();
	const bool bWasSoftwareCursorVisible = InViewport->IsSoftwareCursorVisible();

	RequiredCursorVisibiltyAndAppearance.bDontResetCursor = false;
	UpdateRequiredCursorVisibility();

	if (bWasCursorVisible != RequiredCursorVisibiltyAndAppearance.bHardwareCursorVisible || bWasSoftwareCursorVisible != RequiredCursorVisibiltyAndAppearance.bSoftwareCursorVisible)
	{
		bHandled = true;
	}

	// Compute a view.
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InViewport,
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate(IsRealtime()));
	FSceneView* const View = CalcSceneView(&ViewFamily);

	// Start tracking if any mouse button is down and it was a tracking event (MouseButton/Ctrl/Shift/Alt):
	if (InputState.IsAnyMouseButtonDown()
		&& (Event == IE_Pressed || Event == IE_Released)
		&& (InputState.IsMouseButtonEvent() || InputState.IsCtrlButtonEvent() || InputState.IsAltButtonEvent() || InputState.IsShiftButtonEvent()))
	{
		StartTrackingDueToInput(InputState, *View);
		return true;
	}

	// If we are tracking and no mouse button is down and this input event released the mouse button stop tracking and process any clicks if necessary
	if (bIsTracking && !InputState.IsAnyMouseButtonDown() && InputState.IsMouseButtonEvent())
	{
		// Handle possible mouse click viewport
		ProcessClickInViewport(InputState, *View);

		// Stop tracking if no mouse button is down
		StopTracking();

		bHandled |= true;
	}

	// apply the visibility and set the cursor positions
	ApplyRequiredCursorVisibility(true);

	return bHandled;
}


void FChaosClothEditorRestSpaceViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

	// TODO: Add/modify selection if modifier keys are pressed
	USelection* SelectedComponents = ModeTools->GetSelectedComponents();
	SelectedComponents->Modify();
	SelectedComponents->BeginBatchSelectOperation();

	TArray<UDynamicMeshComponent*> PreviouslySelectedComponents;
	SelectedComponents->GetSelectedObjects<UDynamicMeshComponent>(PreviouslySelectedComponents);
	SelectedComponents->DeselectAll(UDynamicMeshComponent::StaticClass());

	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		const HActor* ActorProxy = static_cast<HActor*>(HitProxy);
		if (ActorProxy && ActorProxy->Actor)
		{
			const AActor* Actor = ActorProxy->Actor;
			const TSet<UActorComponent*>& OwnedComponents = Actor->GetComponents();
			for (UActorComponent* Component : OwnedComponents)
			{
				if (UDynamicMeshComponent* DynamicMeshComp = Cast<UDynamicMeshComponent>(Component))
				{
					SelectedComponents->Select(DynamicMeshComp);
					DynamicMeshComp->PushSelectionToProxy();
				}
			}
		}
	}

	SelectedComponents->EndBatchSelectOperation();

	for (UDynamicMeshComponent* Component : PreviouslySelectedComponents)
	{
		Component->PushSelectionToProxy();
	}

}

void FChaosClothEditorRestSpaceViewportClient::SetEditorViewportWidget(TWeakPtr<SEditorViewport> InEditorViewportWidget)
{
	EditorViewportWidget = InEditorViewportWidget;
}

void FChaosClothEditorRestSpaceViewportClient::SetToolCommandList(TWeakPtr<FUICommandList> InToolCommandList)
{
	ToolCommandList = InToolCommandList;
}
} // namespace UE::Chaos::ClothAsset
