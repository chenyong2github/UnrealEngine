// Copyright Epic Games, Inc. All Rights Reserved.

#include "DrawAndRevolveTool.h"

#include "AssetGenerationUtil.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "CompositionOps/CurveSweepOp.h"
#include "Generators/SweepGenerator.h"
#include "InteractiveToolManager.h" // To use SceneState.ToolManager
#include "Mechanics/CollectSurfacePathMechanic.h"
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "Properties/MeshMaterialProperties.h"
#include "Selection/ToolSelectionUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UDrawAndRevolveTool"

// Tool builder

bool UDrawAndRevolveToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (this->AssetAPI != nullptr);;
}

UInteractiveTool* UDrawAndRevolveToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UDrawAndRevolveTool* NewTool = NewObject<UDrawAndRevolveTool>(SceneState.ToolManager);

	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}


// Operator factory

TUniquePtr<FDynamicMeshOperator> URevolveOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FCurveSweepOp> CurveSweepOp = MakeUnique<FCurveSweepOp>();

	// Assemble profile curve
	CurveSweepOp->ProfileCurve.Reserve(RevolveTool->DrawProfileCurveMechanic->HitPath.Num() + 2); // extra space for top/bottom caps
	for (FFrame3d Frame : RevolveTool->DrawProfileCurveMechanic->HitPath)
	{
		CurveSweepOp->ProfileCurve.Add(Frame.Origin);
	}
	CurveSweepOp->bProfileCurveIsClosed = RevolveTool->DrawProfileCurveMechanic->LoopWasClosed();

	// If we are capping the top and bottom, we just add a couple extra vertices and mark the curve as being closed
	if (!CurveSweepOp->bProfileCurveIsClosed && RevolveTool->Settings->bConnectOpenProfileToAxis)
	{
		// Project first and last points onto the revolution axis.
		double DistanceAlongAxis = RevolveTool->RevolutionAxisDirection.Dot(
			RevolveTool->DrawProfileCurveMechanic->HitPath.Last().Origin - RevolveTool->RevolutionAxisOrigin);
		FVector3d ProjectedPoint = RevolveTool->RevolutionAxisOrigin + (RevolveTool->RevolutionAxisDirection * DistanceAlongAxis);

		CurveSweepOp->ProfileCurve.Add(ProjectedPoint);

		DistanceAlongAxis = RevolveTool->RevolutionAxisDirection.Dot(
			RevolveTool->DrawProfileCurveMechanic->HitPath[0].Origin - RevolveTool->RevolutionAxisOrigin);
		ProjectedPoint = RevolveTool->RevolutionAxisOrigin + (RevolveTool->RevolutionAxisDirection * DistanceAlongAxis);

		CurveSweepOp->ProfileCurve.Add(ProjectedPoint);
		CurveSweepOp->bProfileCurveIsClosed = true;
	}

	RevolveTool->Settings->ApplyToCurveSweepOp(*RevolveTool->MaterialProperties,
		RevolveTool->RevolutionAxisOrigin, RevolveTool->RevolutionAxisDirection, *CurveSweepOp);

	return CurveSweepOp;
}

// Tool itself

bool UDrawAndRevolveTool::CanAccept() const
{
	return Preview != nullptr && Preview->HaveValidResult();
}

void UDrawAndRevolveTool::Setup()
{
	UInteractiveTool::Setup();

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartRevolveTool", "Draw and a profile curve and it will be revolved around the purple draw plane axis. "
			"Ctrl+click repositions draw plane and axis. The curve is ended by clicking the end again or connecting to its start."),
		EToolMessageLevel::UserNotification);

	Settings = NewObject<URevolveToolProperties>(this, TEXT("Revolve Tool Settings"));
	Settings->RestoreProperties(this);
	Settings->AllowedToEditDrawPlane = 1;
	AddToolPropertySource(Settings);

	MaterialProperties = NewObject<UNewMeshMaterialProperties>(this);
	AddToolPropertySource(MaterialProperties);
	MaterialProperties->RestoreProperties(this);

	DrawProfileCurveMechanic = NewObject<UCollectSurfacePathMechanic>(this);
	DrawProfileCurveMechanic->Setup(this);

	UpdateRevolutionAxis(Settings->DrawPlaneAndAxis);

	double SnapTolerance = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD();
	DrawProfileCurveMechanic->SpatialSnapPointsFunc = [this, SnapTolerance](FVector3d Position1, FVector3d Position2)
	{
		//TODO: optimize for ortho
		return ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, Position1, Position2, SnapTolerance);
	};
	DrawProfileCurveMechanic->SetDoubleClickOrCloseLoopMode();
	FFrame3d ProfileDrawPlane(Settings->DrawPlaneAndAxis);
	DrawProfileCurveMechanic->InitializePlaneSurface(ProfileDrawPlane);

	// The click behavior forwards clicks to the DrawProfileCurveMechanic
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>(this);
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	// The hover behavior forwards hover to DrawProfileCurveMechanic (for the preview point)
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	// The plane mechanic lets us update the plane in which we draw the profile curve, as long as we haven't
	// started adding points to it already.
	PlaneMechanic = NewObject<UConstructionPlaneMechanic>(this);
	PlaneMechanic->Setup(this);
	PlaneMechanic->Initialize(TargetWorld, ProfileDrawPlane);
	PlaneMechanic->UpdateClickPriority(ClickBehavior->GetPriority().MakeHigher());
	PlaneMechanic->CanUpdatePlaneFunc = [this]() 
	{ 
		return DrawProfileCurveMechanic->HitPath.Num() == 0; 
	};
	PlaneMechanic->OnPlaneChanged.AddLambda([this]() {
		Settings->DrawPlaneAndAxis = PlaneMechanic->Plane.ToFTransform();
		DrawProfileCurveMechanic->InitializePlaneSurface(PlaneMechanic->Plane);
		UpdateRevolutionAxis(Settings->DrawPlaneAndAxis);
		});
	PlaneMechanic->SetEnableGridSnaping(Settings->bSnapToWorldGrid);
}

void UDrawAndRevolveTool::UpdateRevolutionAxis(const FTransform& PlaneTransform)
{
	RevolutionAxisOrigin = PlaneTransform.GetLocation();
	RevolutionAxisDirection = PlaneTransform.GetRotation().GetAxisX();
}

void UDrawAndRevolveTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	MaterialProperties->SaveProperties(this);

	PlaneMechanic->Shutdown();
	DrawProfileCurveMechanic->Shutdown();

	if (Preview)
	{
		if (ShutdownType == EToolShutdownType::Accept)
		{
			GenerateAsset(Preview->Shutdown());
		}
		else
		{
			Preview->Cancel();
		}
	}
}

void UDrawAndRevolveTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("RevolveToolTransactionName", "Revolve Tool"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld, Result.Mesh.Get(), Result.Transform, TEXT("RevolveResult"), MaterialProperties->Material);

	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}

	GetToolManager()->EndUndoTransaction();
}

void UDrawAndRevolveTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (!bProfileCurveComplete && DrawProfileCurveMechanic->TryAddPointFromRay(ClickPos.WorldRay))
	{
		GetToolManager()->EmitObjectChange(this, MakeUnique<FRevolveToolStateChange>(), LOCTEXT("ProfileCurvePoint", "Profile Curve Change"));
		
		Settings->AllowedToEditDrawPlane = (DrawProfileCurveMechanic->HitPath.Num() == 0);
		
		if (DrawProfileCurveMechanic->IsDone())
		{
			bProfileCurveComplete = true;
			StartPreview();
		}
	}
}

void UDrawAndRevolveTool::StartPreview()
{
	URevolveOperatorFactory* RevolveOpCreator = NewObject<URevolveOperatorFactory>();
	RevolveOpCreator->RevolveTool = this;

	// Normally we wouldn't give the object a name, but since we may destroy the preview using undo,
	// the ability to reuse the non-cleaned up memory is useful. Careful if copy-pasting this!
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(RevolveOpCreator, "RevolveToolPreview");

	Preview->Setup(TargetWorld, RevolveOpCreator);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	Preview->ConfigureMaterials(MaterialProperties->Material,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
	Preview->PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);

	Preview->SetVisibility(true);
	Preview->InvalidateResult();
}

FInputRayHit UDrawAndRevolveTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	if (!bProfileCurveComplete)
	{
		FFrame3d HitPoint;
		if (DrawProfileCurveMechanic->IsHitByRay(ClickPos.WorldRay, HitPoint))
		{
			return FInputRayHit(FRay3d(ClickPos.WorldRay).Project(HitPoint.Origin));
		}
	}

	// background capture, if nothing else is hit
	return FInputRayHit(TNumericLimits<float>::Max());
}

FInputRayHit UDrawAndRevolveTool::BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos)
{
	if (!bProfileCurveComplete)
	{
		FFrame3d HitPoint;
		if (DrawProfileCurveMechanic->IsHitByRay(DevicePos.WorldRay, HitPoint))
		{
			return FInputRayHit(FRay3d(DevicePos.WorldRay).Project(HitPoint.Origin));
		}
	}

	// background capture, if nothing else is hit
	return FInputRayHit(TNumericLimits<float>::Max());
}

bool UDrawAndRevolveTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (!bProfileCurveComplete)
	{
		DrawProfileCurveMechanic->UpdatePreviewPoint(DevicePos.WorldRay);
	}
	return true;
}

void UDrawAndRevolveTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(URevolveToolProperties, DrawPlaneAndAxis)))
	{
		FFrame3d ProfileDrawPlane(Settings->DrawPlaneAndAxis); // Casting to FFrame3d
		DrawProfileCurveMechanic->InitializePlaneSurface(ProfileDrawPlane);
		PlaneMechanic->SetPlaneWithoutBroadcast(ProfileDrawPlane);
		UpdateRevolutionAxis(Settings->DrawPlaneAndAxis);
	}

	PlaneMechanic->SetEnableGridSnaping(Settings->bSnapToWorldGrid);

	if (Preview)
	{
		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNewMeshMaterialProperties, Material)))
		{
			Preview->ConfigureMaterials(MaterialProperties->Material,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
		}

		Preview->PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
		Preview->InvalidateResult();
	}
}


void UDrawAndRevolveTool::OnTick(float DeltaTime)
{
	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->Tick(DeltaTime);
	}

	if (Preview)
	{
		Preview->Tick(DeltaTime);
	}
}


void UDrawAndRevolveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->Render(RenderAPI);

		// Draw the axis of rotation
		float PdiScale = CameraState.GetPDIScalingFactor();
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

		FColor AxisColor(240, 16, 240);
		double AxisThickness = 1.0 * PdiScale;
		double AxisHalfLength = ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(CameraState, RevolutionAxisOrigin, 90);

		FVector3d StartPoint = RevolutionAxisOrigin - (RevolutionAxisDirection * (AxisHalfLength * PdiScale));
		FVector3d EndPoint = RevolutionAxisOrigin + (RevolutionAxisDirection * (AxisHalfLength * PdiScale));

		PDI->DrawLine((FVector)StartPoint, (FVector)EndPoint, AxisColor, SDPG_Foreground, 
			AxisThickness, 0.0f, true);
	}

	if (DrawProfileCurveMechanic != nullptr)
	{
		DrawProfileCurveMechanic->Render(RenderAPI);
	}
}

// Undo support

void UDrawAndRevolveTool::UndoCurrentOperation()
{
	if (bProfileCurveComplete)
	{
		// Curve is no longer complete
		bProfileCurveComplete = false;

		// Cancel and destroy the preview mesh
		if (Preview)
		{
			Preview->Cancel();
			Preview = nullptr;
		}
	}

	DrawProfileCurveMechanic->PopLastPoint();

	Settings->AllowedToEditDrawPlane = (DrawProfileCurveMechanic->HitPath.Num() == 0);
}

void FRevolveToolStateChange::Revert(UObject* Object)
{
	Cast<UDrawAndRevolveTool>(Object)->UndoCurrentOperation();
	bHaveDoneUndo = true;
}
bool FRevolveToolStateChange::HasExpired(UObject* Object) const
{
	return bHaveDoneUndo;
}
FString FRevolveToolStateChange::ToString() const
{
	return TEXT("FRevolveToolStateChange");
}

#undef LOCTEXT_NAMESPACE
