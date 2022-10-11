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

constexpr float CameraFarPlaneWorldZ = -10.0f;
constexpr float CameraNearPlaneProportionZ = 0.8f;
constexpr float DefaultNearClipPlaneDistance = KINDA_SMALL_NUMBER;
constexpr float DefaultFarClipPlaneDistance = UE_FLOAT_HUGE_DISTANCE / 4.0f;

FChaosClothEditorRestSpaceViewportClient::FChaosClothEditorRestSpaceViewportClient(FEditorModeTools* InModeTools,
	FPreviewScene* InPreviewScene,
	const TWeakPtr<SEditorViewport>& InEditorViewportWidget) :
	FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget)
{
	OverrideNearClipPlane(DefaultNearClipPlaneDistance);
	OverrideFarClipPlane(DefaultFarClipPlaneDistance);

	BehaviorSet = NewObject<UInputBehaviorSet>();

	// We'll have the priority of our viewport manipulation behaviors be lower (i.e. higher
	// numerically) than both the gizmo default and the tool default.
	static constexpr int DEFAULT_VIEWPORT_BEHAVIOR_PRIORITY = 150;

	ScrollBehaviorTarget = MakeUnique<FEditor2DScrollBehaviorTarget>(this);
	UClickDragInputBehavior* ScrollBehavior = NewObject<UClickDragInputBehavior>();
	ScrollBehavior->Initialize(ScrollBehaviorTarget.Get());
	ScrollBehavior->SetDefaultPriority(DEFAULT_VIEWPORT_BEHAVIOR_PRIORITY);
	ScrollBehavior->SetUseRightMouseButton();
	BehaviorSet->Add(ScrollBehavior);
	SavedBehaviors.Add(ScrollBehavior);

	ZoomBehaviorTarget = MakeUnique<FEditor2DMouseWheelZoomBehaviorTarget>(this);
	ZoomBehaviorTarget->SetCameraFarPlaneWorldZ(CameraFarPlaneWorldZ);
	ZoomBehaviorTarget->SetCameraNearPlaneProportionZ(CameraNearPlaneProportionZ);
	ZoomBehaviorTarget->SetZoomLimits(0.001, 100000);
	UMouseWheelInputBehavior* ZoomBehavior = NewObject<UMouseWheelInputBehavior>();
	ZoomBehavior->Initialize(ZoomBehaviorTarget.Get());
	ZoomBehavior->SetDefaultPriority(DEFAULT_VIEWPORT_BEHAVIOR_PRIORITY);
	BehaviorSet->Add(ZoomBehavior);
	SavedBehaviors.Add(ZoomBehavior);

	ModeTools->GetInteractiveToolsContext()->InputRouter->RegisterSource(this);
}

void FChaosClothEditorRestSpaceViewportClient::Set2DMode(bool In2DMode)
{
	b2DMode = In2DMode;

	BehaviorSet->RemoveAll();
	ModeTools->GetInteractiveToolsContext()->InputRouter->DeregisterSource(this);

	if (b2DMode)
	{
		for (UInputBehavior* Behavior : SavedBehaviors)
		{
			BehaviorSet->Add(Behavior);
		}
		ModeTools->GetInteractiveToolsContext()->InputRouter->RegisterSource(this);
	}
	else
	{
		OverrideNearClipPlane(DefaultNearClipPlaneDistance);
		OverrideFarClipPlane(DefaultFarClipPlaneDistance);
	}

}

const UInputBehaviorSet* FChaosClothEditorRestSpaceViewportClient::GetInputBehaviors() const
{
	return BehaviorSet;
}

// Collects UObjects that we don't want the garbage collecter to throw away under us
void FChaosClothEditorRestSpaceViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(BehaviorSet);
	Collector.AddReferencedObjects(SavedBehaviors);
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
	// selection work, but skip the code dealing with camera controls
	//

	bool bHandled = false;

	const FInputEventState InputState(EventArgs.Viewport, EventArgs.Key, EventArgs.Event);
	const FViewport* InViewport = EventArgs.Viewport;
	const EInputEvent Event = EventArgs.Event;

	const bool bWasCursorVisible = InViewport->IsCursorVisible();
	const bool bWasSoftwareCursorVisible = InViewport->IsSoftwareCursorVisible();

	const bool AltDown = InputState.IsAltButtonPressed();
	const bool ShiftDown = InputState.IsShiftButtonPressed();
	const bool ControlDown = InputState.IsCtrlButtonPressed();

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
	FSceneView* View = CalcSceneView(&ViewFamily);

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
	SelectedComponents->DeselectAll();

	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		const HActor* ActorProxy = static_cast<HActor*>(HitProxy);
		if (ActorProxy && ActorProxy->Actor )
		{
			const AActor* Actor = ActorProxy->Actor;
			const TSet<UActorComponent*>& OwnedComponents = Actor->GetComponents();
			for (UActorComponent* Component : OwnedComponents)
			{
				if (UDynamicMeshComponent* DynamicMeshComp = Cast<UDynamicMeshComponent>(Component))
				{
					SelectedComponents->Select(DynamicMeshComp);
				}
			}
		}
	}

	SelectedComponents->EndBatchSelectOperation();

}

void FChaosClothEditorRestSpaceViewportClient::SetEditorViewportWidget(TWeakPtr<SEditorViewport> InEditorViewportWidget)
{
	EditorViewportWidget = InEditorViewportWidget;
}

void FChaosClothEditorRestSpaceViewportClient::SetToolCommandList(TWeakPtr<FUICommandList> InToolCommandList)
{
	ToolCommandList = InToolCommandList;
}