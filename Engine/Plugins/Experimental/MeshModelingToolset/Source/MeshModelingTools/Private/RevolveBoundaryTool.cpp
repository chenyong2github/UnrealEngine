// Copyright Epic Games, Inc. All Rights Reserved.

#include "RevolveBoundaryTool.h"

#include "AssetGenerationUtil.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "CoreMinimal.h"
#include "CompositionOps/CurveSweepOp.h"
#include "InteractiveToolManager.h"
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "Selection/PolygonSelectionMechanic.h"
#include "GroupTopology.h"
#include "ToolBuilderUtil.h"
#include "Selection/ToolSelectionUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "URevolveBoundaryTool"

// Tool builder

bool URevolveBoundaryToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return this->AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* URevolveBoundaryToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	URevolveBoundaryTool* NewTool = NewObject<URevolveBoundaryTool>(SceneState.ToolManager);

	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}


// Operator factory

TUniquePtr<FDynamicMeshOperator> URevolveBoundaryOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FCurveSweepOp> CurveSweepOp = MakeUnique<FCurveSweepOp>();
	
	// Assemble profile curve
	const FGroupTopologySelection& ActiveSelection = RevolveBoundaryTool->SelectionMechanic->GetActiveSelection();
	if (ActiveSelection.SelectedEdgeIDs.Num() == 1)
	{
		int32 EdgeID = ActiveSelection.GetASelectedEdgeID();
		if (RevolveBoundaryTool->Topology->IsBoundaryEdge(EdgeID))
		{
			const TArray<int32>& VertexIndices = RevolveBoundaryTool->Topology->GetGroupEdgeVertices(EdgeID);
			FTransform ToWorld = RevolveBoundaryTool->ComponentTarget->GetWorldTransform();

			// Boundary loop includes the last vertex twice, so stop early.
			CurveSweepOp->ProfileCurve.Reserve(VertexIndices.Num() - 1);
			for (int32 i = 0; i < VertexIndices.Num()-1; ++i)
			{
				int32 VertIndex = VertexIndices[i];

				CurveSweepOp->ProfileCurve.Add(ToWorld.TransformPosition((FVector)RevolveBoundaryTool->OriginalMesh->GetVertex(VertIndex)));
			}
			CurveSweepOp->bProfileCurveIsClosed = true;
		}
	}

	RevolveBoundaryTool->Settings->ApplyToCurveSweepOp(*RevolveBoundaryTool->MaterialProperties,
		RevolveBoundaryTool->RevolutionAxisOrigin, RevolveBoundaryTool->RevolutionAxisDirection, *CurveSweepOp);

	return CurveSweepOp;
}


// Tool itself

void URevolveBoundaryTool::Setup()
{
	UMeshBoundaryToolBase::Setup();

	Settings = NewObject<URevolveBoundaryToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	MaterialProperties = NewObject<UNewMeshMaterialProperties>(this);
	AddToolPropertySource(MaterialProperties);
	MaterialProperties->RestoreProperties(this);

	UpdateRevolutionAxis();

	// The plane mechanic is used for the revolution axis
	PlaneMechanic = NewObject<UConstructionPlaneMechanic>(this);
	PlaneMechanic->Setup(this);
	PlaneMechanic->Initialize(TargetWorld, FFrame3d(Settings->AxisOrigin, 
		FRotator(Settings->AxisPitch, Settings->AxisYaw, 0).Quaternion()));
	PlaneMechanic->UpdateClickPriority(LoopSelectClickBehavior->GetPriority().MakeLower());
	PlaneMechanic->bShowGrid = false;
	PlaneMechanic->OnPlaneChanged.AddLambda([this]() {
		Settings->AxisOrigin = (FVector)PlaneMechanic->Plane.Origin;
		FRotator AxisOrientation = ((FQuat)PlaneMechanic->Plane.Rotation).Rotator();
		Settings->AxisPitch = AxisOrientation.Pitch;
		Settings->AxisYaw = AxisOrientation.Yaw;
		UpdateRevolutionAxis();
		});

	PlaneMechanic->SetEnableGridSnaping(Settings->bSnapToWorldGrid);

	LoopSelectClickBehavior->Modifiers.RegisterModifier(AlignAxisModifier, FInputDeviceState::IsCtrlKeyDown);

	ComponentTarget->SetOwnerVisibility(Settings->bDisplayOriginalMesh);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartRevolveBoundaryTool", "This tool revolves the mesh boundary around the axis to create a new mesh. Ctrl+click will reposition the revolution axis, potentially aligning it with an edge."),
		EToolMessageLevel::UserNotification);
	if (Topology->Edges.Num() == 1)
	{
		FGroupTopologySelection Selection;
		Selection.SelectedEdgeIDs.Add(0);
		SelectionMechanic->SetSelection(Selection);
		StartPreview();
	}
	else if (Topology->Edges.Num() == 0)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("NoBoundaryLoops", "This mesh does not have any boundary loops to display and revolve. Delete some faces or use a different mesh."),
			EToolMessageLevel::UserWarning);
	}
	else
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartRevolveBoundaryToolMultipleBoundaries", "Your mesh has multiple boundaries- Click the one you wish to use"),
			EToolMessageLevel::UserWarning);
	}
}

void URevolveBoundaryTool::OnUpdateModifierState(int ModifierId, bool bIsOn)
{
	if (ModifierId == AlignAxisModifier)
	{
		bAlignAxisOnClick = bIsOn;
	}
}

void URevolveBoundaryTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	// Update selection only if we clicked on something. We don't want to be able to
	// clear a selection with a click.
	FHitResult HitResult;
	if (SelectionMechanic->TopologyHitTest(ClickPos.WorldRay, HitResult))
	{
		FVector3d LocalHitPosition, LocalHitNormal;
		SelectionMechanic->UpdateSelection(ClickPos.WorldRay, LocalHitPosition, LocalHitNormal);

		// Clear the "multiple boundaries" warning, since we've selected one.
		GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);

		// If Ctrl is pressed, we also want to align the revolution axis to the edge that we clicked
		if (bAlignAxisOnClick)
		{
			const FGroupTopologySelection& Selection = SelectionMechanic->GetActiveSelection();
			int32 ClickedEid = Topology->GetGroupEdgeEdges(Selection.GetASelectedEdgeID())[HitResult.Item];
			
			FVector3d VertexA, VertexB;
			OriginalMesh->GetEdgeV(ClickedEid, VertexA, VertexB);
			FTransform ToWorldTranform = ComponentTarget->GetWorldTransform();
			FLine3d EdgeLine = FLine3d::FromPoints(ToWorldTranform.TransformPosition((FVector)VertexA), 
				ToWorldTranform.TransformPosition((FVector)VertexB));
			
			FFrame3d RevolutionAxisFrame;
			RevolutionAxisFrame.Origin = EdgeLine.NearestPoint(HitResult.ImpactPoint);
			RevolutionAxisFrame.AlignAxis(0, EdgeLine.Direction);

			PlaneMechanic->SetPlaneWithoutBroadcast(RevolutionAxisFrame);

			Settings->AxisOrigin = (FVector)RevolutionAxisFrame.Origin;
			FRotator AxisOrientation = ((FQuat)RevolutionAxisFrame.Rotation).Rotator();
			Settings->AxisPitch = AxisOrientation.Pitch;
			Settings->AxisYaw = AxisOrientation.Yaw;
			UpdateRevolutionAxis();
		}

		// Update the preview
		if (Preview == nullptr)
		{
			StartPreview();
		}
		else
		{
			Preview->InvalidateResult();
		}
	}
}

bool URevolveBoundaryTool::CanAccept() const
{
	return Preview != nullptr && Preview->HaveValidResult();
}

/** 
 * Uses the settings stored in the properties object to update the revolution axis
 */
void URevolveBoundaryTool::UpdateRevolutionAxis()
{
	RevolutionAxisOrigin = Settings->AxisOrigin;
	RevolutionAxisDirection = FRotator(Settings->AxisPitch, Settings->AxisYaw, 0).RotateVector(FVector(1, 0, 0));
	if (Preview)
	{
		Preview->InvalidateResult();
	}
}

void URevolveBoundaryTool::StartPreview()
{
	URevolveBoundaryOperatorFactory* RevolveBoundaryOpCreator = NewObject<URevolveBoundaryOperatorFactory>();
	RevolveBoundaryOpCreator->RevolveBoundaryTool = this;

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(RevolveBoundaryOpCreator);
	Preview->Setup(TargetWorld, RevolveBoundaryOpCreator);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	Preview->ConfigureMaterials(MaterialProperties->Material.Get(),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
	Preview->PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);

	Preview->SetVisibility(true);
	Preview->InvalidateResult();
}

void URevolveBoundaryTool::Shutdown(EToolShutdownType ShutdownType)
{
	UMeshBoundaryToolBase::Shutdown(ShutdownType);

	Settings->SaveProperties(this);
	MaterialProperties->SaveProperties(this);

	PlaneMechanic->Shutdown();

	ComponentTarget->SetOwnerVisibility(true);

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

void URevolveBoundaryTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("RevolveBoundaryToolTransactionName", "Revolve Boundary Tool"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld, Result.Mesh.Get(), Result.Transform, TEXT("RevolveBoundaryResult"), MaterialProperties->Material.Get());

	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}

	GetToolManager()->EndUndoTransaction();
}

void URevolveBoundaryTool::OnTick(float DeltaTime)
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

void URevolveBoundaryTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshBoundaryToolBase::Render(RenderAPI);

	FViewCameraState CameraState;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->Render(RenderAPI);

		// Draw the axis of rotation
		float PdiScale = CameraState.GetPDIScalingFactor();
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

		FColor AxisColor(240, 16, 240);
		double AxisThickness = 1 * PdiScale;
		double AxisHalfLength = ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(CameraState, RevolutionAxisOrigin, 90);

		FVector3d StartPoint = RevolutionAxisOrigin - (RevolutionAxisDirection * (AxisHalfLength * PdiScale));
		FVector3d EndPoint = RevolutionAxisOrigin + (RevolutionAxisDirection * (AxisHalfLength * PdiScale));

		PDI->DrawLine((FVector)StartPoint, (FVector)EndPoint, AxisColor, SDPG_Foreground,
			AxisThickness, 0.0f, true);
	}
}

void URevolveBoundaryTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	PlaneMechanic->SetPlaneWithoutBroadcast(FFrame3d(Settings->AxisOrigin, 
		FRotator(Settings->AxisPitch, Settings->AxisYaw, 0).Quaternion()));
	UpdateRevolutionAxis();

	ComponentTarget->SetOwnerVisibility(Settings->bDisplayOriginalMesh);
	PlaneMechanic->SetEnableGridSnaping(Settings->bSnapToWorldGrid);

	if (Preview)
	{
		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNewMeshMaterialProperties, Material)))
		{
			Preview->ConfigureMaterials(MaterialProperties->Material.Get(),
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
		}

		Preview->PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
		Preview->InvalidateResult();
	}
}

#undef LOCTEXT_NAMESPACE
