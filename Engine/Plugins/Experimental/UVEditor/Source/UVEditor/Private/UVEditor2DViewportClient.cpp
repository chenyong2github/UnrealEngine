// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditor2DViewportClient.h"

#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
//#include "Drawing/DensityAdjustingGrid.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"

FUVEditor2DViewportClient::FUVEditor2DViewportClient(FEditorModeTools* InModeTools,
	FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget)
{
	// Don't draw the little XYZ drawing in the corner.
	bDrawAxes = false;

	// We want our near clip plane to be quite close so that we can zoom in further.
	OverrideNearClipPlane(KINDA_SMALL_NUMBER);

	// Set up viewport manipulation behaviors:

	BehaviorSet = NewObject<UInputBehaviorSet>();

	// We'll have the priority of our viewport manipulation behaviors be lower (i.e. higher
	// numerically) than both the gizmo default and the tool default.
	static constexpr int DEFAULT_VIEWPORT_BEHAVIOR_PRIORITY = 150;

	ScrollBehaviorTarget = MakeUnique<FUVEditor2DScrollBehaviorTarget>(this);
	UClickDragInputBehavior* ScrollBehavior = NewObject<UClickDragInputBehavior>();
	ScrollBehavior->Initialize(ScrollBehaviorTarget.Get());
	ScrollBehavior->SetDefaultPriority(DEFAULT_VIEWPORT_BEHAVIOR_PRIORITY);
	ScrollBehavior->SetUseRightMouseButton();
	BehaviorSet->Add(ScrollBehavior);

	ZoomBehaviorTarget = MakeUnique<FUVEditor2DMouseWheelZoomBehaviorTarget>(this);
	UMouseWheelInputBehavior* ZoomBehavior = NewObject<UMouseWheelInputBehavior>();
	ZoomBehavior->Initialize(ZoomBehaviorTarget.Get());
	ZoomBehavior->SetDefaultPriority(DEFAULT_VIEWPORT_BEHAVIOR_PRIORITY);
	BehaviorSet->Add(ZoomBehavior);

	ModeTools->GetInteractiveToolsContext()->InputRouter->RegisterSource(this);
}

const UInputBehaviorSet* FUVEditor2DViewportClient::GetInputBehaviors() const
{
	return BehaviorSet;
}

// Collects UObjects that we don't want the garbage collecter to throw away under us
void FUVEditor2DViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(BehaviorSet);
	//Collector.AddReferencedObject(Grid);
}

bool FUVEditor2DViewportClient::InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float/*AmountDepressed*/, bool/*Gamepad*/)
{
	// We'll support disabling input like our base class, even if it does not end up being used.
	if (bDisableInput)
	{
		return true;
	}

	// Our viewport manipulation is placed in the input router that ModeTools manages
	return ModeTools->InputKey(this, Viewport, Key, Event);
}

// Note that this function gets called from the super class Draw(FViewport*, FCanvas*) overload to draw the scene.
// We don't override that top-level function so that it can do whatever view calculations it needs to do.
void FUVEditor2DViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	// Draw the axes
	const float AxisThickness = 1.0;
	PDI->DrawLine(FVector(-HALF_WORLD_MAX, 0, 0), FVector(HALF_WORLD_MAX, 0, 0), FLinearColor::Red, SDPG_World, AxisThickness, 0, true);
	PDI->DrawLine(FVector(0, -HALF_WORLD_MAX, 0), FVector(0, HALF_WORLD_MAX, 0), FLinearColor::Green, SDPG_World, AxisThickness, 0, true);

	// TODO: Eventually we want a proper grid. For now, draw the unit boundary
	const float UVScale = 1000;
	PDI->DrawLine(FVector(UVScale, 0, 0), FVector(UVScale, UVScale, 0), FLinearColor::Gray, SDPG_World, AxisThickness, 0, true);
	PDI->DrawLine(FVector(0, UVScale, 0), FVector(UVScale, UVScale, 0), FLinearColor::Gray, SDPG_World, AxisThickness, 0, true);
	//Grid->Render(View, PDI);

	// TODO: Draw a little UV axis thing in the lower left, like the XYZ things that normal viewports have.

	// Calls ModeTools draw/render functions
	FEditorViewportClient::Draw(View, PDI);
}

