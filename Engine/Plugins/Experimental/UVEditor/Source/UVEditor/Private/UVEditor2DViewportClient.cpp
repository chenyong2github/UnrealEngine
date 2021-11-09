// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditor2DViewportClient.h"

#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "ContextObjectStore.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "Drawing/MeshDebugDrawing.h"
#include "FrameTypes.h"
#include "UVEditorMode.h"

FUVEditor2DViewportClient::FUVEditor2DViewportClient(FEditorModeTools* InModeTools,
	FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget)
{
	ShowWidget(false);

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

	UContextObjectStore* ContextStore = ModeTools->GetInteractiveToolsContext()->ContextObjectStore;
	ViewportButtonsAPI = ContextStore->FindContext<UUVToolViewportButtonsAPI>();
	if (!ViewportButtonsAPI)
	{
		ViewportButtonsAPI = NewObject<UUVToolViewportButtonsAPI>();
		ContextStore->AddContextObject(ViewportButtonsAPI);
	}
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
	Collector.AddReferencedObject(ViewportButtonsAPI);
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
	// Basic scaling amount
	const float UVScale = UUVEditorMode::GetUVMeshScalingFactor();
	
	// Line thickness parameters
	const float AxisThickness = 2;
	const float GridMajorThickness = 1.0;

    // Line color scheme parameters
	const FLinearColor XAxisColor = FLinearColor::Red;
	const FLinearColor YAxisColor = FLinearColor::Green;
	const FLinearColor GridMajorColor = FLinearColor::Gray;
	const FLinearColor GridMinorColor = FLinearColor::Gray;

	// Determine important geometry of the viewport for creating grid lines
	FVector WorldCenterPoint( 0,0,0 );
	FVector4 WorldToScreenCenter = View->WorldToScreen(WorldCenterPoint);	
	float ZoomFactor = WorldToScreenCenter.W;
	FVector4 MaxScreen(1 * ZoomFactor, 1 * ZoomFactor, 0, 1);
	FVector4 MinScreen(-1 * ZoomFactor, -1 * ZoomFactor, 0, 1);
	FVector WorldBoundsMax = View->ScreenToWorld(MaxScreen);
	FVector WorldBoundsMin = View->ScreenToWorld(MinScreen);
	FVector ViewLoc = GetViewLocation();
	ViewLoc.Z = 0.0; // We are treating the scene like a 2D plane, so we'll clamp the Z position here to 
	               // 0 as a simple projection step just in case.

	// Prevent grid from drawing if we are too close or too far, in order to avoid potential graphical issues.
	if (ZoomFactor < 100000 && ZoomFactor > 1)
	{
		// Setup and call grid calling function
		UE::Geometry::FFrame3f LocalFrame((FVector3f)ViewLoc);
		FTransform Transform;
		TArray<FColor> Colors;
		Colors.Push(GridMajorColor.ToRGBE());
		Colors.Push(GridMinorColor.ToRGBE());
		MeshDebugDraw::DrawHierarchicalGrid(UVScale, ZoomFactor / UVScale,
			500, // Maximum density of lines to draw per level before skipping the level
			WorldBoundsMax, WorldBoundsMin,
			3, // Number of levels to draw
			4, // Number of subdivisions per level
			Colors,
			LocalFrame, GridMajorThickness, true,
			PDI, Transform);
	}

	float AxisExtent = FMathf::Max(UVScale, FMathf::Min(WorldBoundsMax.Y, WorldBoundsMax.X));

	// Draw colored axis lines
	PDI->DrawLine(FVector(0, 0, 0), FVector(AxisExtent, 0, 0), FLinearColor::Red, SDPG_World, AxisThickness, 0, true);
	PDI->DrawLine(FVector(0, 0, 0), FVector(0, AxisExtent, 0), FLinearColor::Green, SDPG_World, AxisThickness, 0, true);

	// TODO: Draw a little UV axis thing in the lower left, like the XYZ things that normal viewports have.

	// Calls ModeTools draw/render functions
	FEditorViewportClient::Draw(View, PDI);
}

bool FUVEditor2DViewportClient::ShouldOrbitCamera() const
{
	return false; // The UV Editor's 2D viewport should never orbit.
}

void FUVEditor2DViewportClient::SetWidgetMode(UE::Widget::EWidgetMode NewMode)
{
	if (ViewportButtonsAPI)
	{
		switch (NewMode)
		{
		case UE::Widget::EWidgetMode::WM_None:
			ViewportButtonsAPI->SetGizmoMode(UUVToolViewportButtonsAPI::EGizmoMode::Select);
			break;
		case UE::Widget::EWidgetMode::WM_Translate:
			ViewportButtonsAPI->SetGizmoMode(UUVToolViewportButtonsAPI::EGizmoMode::Transform);
			break;
		default:
			// Do nothing
			break;
		}
	}
}

bool FUVEditor2DViewportClient::AreWidgetButtonsEnabled() const
{
	return ViewportButtonsAPI && ViewportButtonsAPI->AreGizmoButtonsEnabled();
}

bool FUVEditor2DViewportClient::CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const
{
	if (!AreWidgetButtonsEnabled())
	{
		return false;
	}

	return NewMode == UE::Widget::EWidgetMode::WM_None 
		|| NewMode == UE::Widget::EWidgetMode::WM_Translate;
}

UE::Widget::EWidgetMode FUVEditor2DViewportClient::GetWidgetMode() const
{
	if (!AreWidgetButtonsEnabled())
	{
		return UE::Widget::EWidgetMode::WM_None;
	}

	switch (ViewportButtonsAPI->GetGizmoMode())
	{
	case UUVToolViewportButtonsAPI::EGizmoMode::Select:
		return UE::Widget::EWidgetMode::WM_None;
		break;
	case UUVToolViewportButtonsAPI::EGizmoMode::Transform:
		return UE::Widget::EWidgetMode::WM_Translate;
		break;
	default:
		return UE::Widget::EWidgetMode::WM_None;
		break;
	}
}

bool FUVEditor2DViewportClient::AreSelectionButtonsEnabled() const
{
	return ViewportButtonsAPI && ViewportButtonsAPI->AreSelectionButtonsEnabled();
}

void FUVEditor2DViewportClient::SetSelectionMode(UUVToolViewportButtonsAPI::ESelectionMode NewMode)
{
	if (ViewportButtonsAPI)
	{
		ViewportButtonsAPI->SetSelectionMode(NewMode);
	}
}

UUVToolViewportButtonsAPI::ESelectionMode FUVEditor2DViewportClient::GetSelectionMode() const
{
	if (!AreSelectionButtonsEnabled())
	{
		return UUVToolViewportButtonsAPI::ESelectionMode::None;
	}

	return ViewportButtonsAPI->GetSelectionMode();
}