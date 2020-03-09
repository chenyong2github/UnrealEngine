// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mechanics/ConstructionPlaneMechanic.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "Selection/SelectClickedAction.h"
#include "BaseGizmos/TransformGizmo.h"
#include "Drawing/MeshDebugDrawing.h"


void UConstructionPlaneMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);
}

void UConstructionPlaneMechanic::Shutdown()
{
	GetParentTool()->GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(GetParentTool());
}



void UConstructionPlaneMechanic::Initialize(UWorld* TargetWorld, const FFrame3d& InitialPlane)
{
	Plane = InitialPlane;

	// create proxy and gizmo
	UInteractiveGizmoManager* GizmoManager = GetParentTool()->GetToolManager()->GetPairedGizmoManager();

	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformGizmo = GizmoManager->CreateCustomTransformGizmo(
		ETransformGizmoSubElements::StandardTranslateRotate, GetParentTool());
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UConstructionPlaneMechanic::TransformChanged);

	PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy);
	PlaneTransformGizmo->SetNewGizmoTransform(Plane.ToFTransform());

	// click to set plane behavior
	FSelectClickedAction* SetPlaneAction = new FSelectClickedAction();
	SetPlaneAction->World = TargetWorld;
	SetPlaneAction->OnClickedPositionFunc = [this, SetPlaneAction](const FHitResult& Hit)
	{
		SetDrawPlaneFromWorldPos(FVector3d(Hit.ImpactPoint), FVector3d(Hit.ImpactNormal), SetPlaneAction->bShiftModifierToggle);
	};
	SetPlaneAction->ExternalCanClickPredicate = [this]() { return this->CanUpdatePlaneFunc(); };

	SetPointInWorldConnector = TUniquePtr<IClickBehaviorTarget>(SetPlaneAction);

	ClickToSetPlaneBehavior = NewObject<USingleClickInputBehavior>();
	ClickToSetPlaneBehavior->ModifierCheckFunc = FInputDeviceState::IsCtrlKeyDown;
	ClickToSetPlaneBehavior->Modifiers.RegisterModifier(FSelectClickedAction::ShiftModifier, FInputDeviceState::IsShiftKeyDown);
	ClickToSetPlaneBehavior->Initialize(SetPointInWorldConnector.Get());

	GetParentTool()->AddInputBehavior(ClickToSetPlaneBehavior);
}



void UConstructionPlaneMechanic::SetEnableGridSnaping(bool bEnable)
{
	bEnableSnapToWorldGrid = bEnable;
}

void UConstructionPlaneMechanic::UpdateClickPriority(FInputCapturePriority NewPriority)
{
	ensure(ClickToSetPlaneBehavior != nullptr);
	if (ClickToSetPlaneBehavior != nullptr)
	{
		ClickToSetPlaneBehavior->SetDefaultPriority(NewPriority);
	}
}

void UConstructionPlaneMechanic::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	Plane.Rotation = FQuaterniond(Transform.GetRotation());
	Plane.Origin = FVector3d(Transform.GetTranslation());

	OnPlaneChanged.Broadcast();
}


void UConstructionPlaneMechanic::SetDrawPlaneFromWorldPos(const FVector3d& Position, const FVector3d& Normal, bool bIgnoreNormal)
{
	Plane.Origin = Position;
	if (bIgnoreNormal == false)
	{
		Plane.AlignAxis(2, Normal);
	}
	PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy);
	PlaneTransformGizmo->SetNewGizmoTransform(Plane.ToFTransform());

	OnPlaneChanged.Broadcast();
}


void UConstructionPlaneMechanic::Tick(float DeltaTime)
{
	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->bSnapToWorldGrid = bEnableSnapToWorldGrid;
	}
}

void UConstructionPlaneMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (bShowGrid)
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		FColor GridColor(128, 128, 128, 32);
		float GridThickness = 0.5f;
		float GridLineSpacing = 25.0f;   // @todo should be relative to view
		int NumGridLines = 10;

		FFrame3f DrawFrame(Plane);
		MeshDebugDraw::DrawSimpleGrid(DrawFrame, NumGridLines, GridLineSpacing, GridThickness, GridColor, false, PDI, FTransform::Identity);
	}
}
