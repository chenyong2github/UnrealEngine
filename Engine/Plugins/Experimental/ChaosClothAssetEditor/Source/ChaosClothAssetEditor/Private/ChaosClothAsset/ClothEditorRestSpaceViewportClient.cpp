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

void FChaosClothEditorRestSpaceViewportClient::SetConstructionViewMode(EClothPatternVertexType InViewMode)
{
	ConstructionViewMode = InViewMode;

	BehaviorSet->RemoveAll();

	if (ConstructionViewMode == EClothPatternVertexType::Sim2D)
	{
		for (UInputBehavior* const Behavior : BehaviorsFor2DMode)
		{
			BehaviorSet->Add(Behavior);
		}

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

	ModeTools->GetInteractiveToolsContext()->InputRouter->DeregisterSource(this);
	ModeTools->GetInteractiveToolsContext()->InputRouter->RegisterSource(this);
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
	if (ConstructionViewMode == EClothPatternVertexType::Sim2D)
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
	if (ConstructionViewMode != EClothPatternVertexType::Sim2D)
	{
		return FEditorViewportClient::InputKey(EventArgs);
	}

	// We'll support disabling input like our base class, even if it does not end up being used.
	if (bDisableInput)
	{
		return true;
	}

	// Our viewport manipulation is placed in the input router that ModeTools manages
	return ModeTools->InputKey(this, EventArgs.Viewport, EventArgs.Key, EventArgs.Event);
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
