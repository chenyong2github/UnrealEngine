// Copyright Epic Games, Inc. All Rights Reserved.

#include "PolygonOnMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh3.h"
#include "ToolSceneQueriesUtil.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "ToolDataVisualizer.h"
#include "Util/ColorConstants.h"
#include "Drawing/LineSetComponent.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"



#define LOCTEXT_NAMESPACE "UPolygonOnMeshTool"


/*
 * ToolBuilder
 */


bool UPolygonOnMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UPolygonOnMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UPolygonOnMeshTool* NewTool = NewObject<UPolygonOnMeshTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	NewTool->SetWorld(SceneState.World);
	return NewTool;
}




/*
 * Tool
 */

void UPolygonOnMeshToolActionPropertySet::PostAction(EPolygonOnMeshToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}




UPolygonOnMeshTool::UPolygonOnMeshTool()
{
	SetToolDisplayName(LOCTEXT("PolygonOnMeshToolName", "Polygon Cut Tool"));
}

void UPolygonOnMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UPolygonOnMeshTool::Setup()
{
	UInteractiveTool::Setup();


	// register click and hover behaviors
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>(this);
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	WorldTransform = FTransform3d(ComponentTarget->GetWorldTransform());

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	BasicProperties = NewObject<UPolygonOnMeshToolProperties>(this);
	AddToolPropertySource(BasicProperties);

	ActionProperties = NewObject<UPolygonOnMeshToolActionPropertySet>(this);
	ActionProperties->Initialize(this);
	AddToolPropertySource(ActionProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreview();

	DrawnLineSet = NewObject<ULineSetComponent>(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetupAttachment(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	DrawnLineSet->RegisterComponent();

	Preview->OnOpCompleted.AddLambda(
		[this](const FDynamicMeshOperator* Op)
		{
			const FEmbedPolygonsOp* PolygonsOp = (const FEmbedPolygonsOp*)(Op);
			EmbeddedEdges = PolygonsOp->EmbeddedEdges;
			bEmbedSucceeded = PolygonsOp->bEmbedSucceeded;
		}
	);
	Preview->OnMeshUpdated.AddLambda(
		[this](const UMeshOpPreviewWithBackgroundCompute*)
		{
			GetToolManager()->PostInvalidation();
			UpdateVisualization();
		}
	);

	DrawPlaneWorld = FFrame3d(WorldTransform.GetTranslation());

	PlaneMechanic = NewObject<UConstructionPlaneMechanic>(this);
	PlaneMechanic->Setup(this);
	PlaneMechanic->Initialize(TargetWorld, DrawPlaneWorld);
	//PlaneMechanic->UpdateClickPriority(ClickBehavior->GetPriority().MakeHigher());
	PlaneMechanic->OnPlaneChanged.AddLambda([this]() {
		DrawPlaneWorld = PlaneMechanic->Plane;
		UpdateDrawPlane();
	});
	PlaneMechanic->SetPlaneCtrlClickBehaviorTarget->InvisibleComponentsToHitTest.Add(ComponentTarget->GetOwnerComponent());

	// Convert input mesh description to dynamic mesh
	OriginalDynamicMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);
	// TODO: consider adding an AABB tree construction here?  tradeoff vs doing a raycast against full every time a param change happens ...

	LastDrawnPolygon = FPolygon2d();
	UpdatePolygonType();
	UpdateDrawPlane();

	GetToolManager()->DisplayMessage(
		LOCTEXT("PolygonOnMeshToolDescription", "Cut the Mesh with a swept Polygon, creating a Hole or new Polygroup. Use the Draw Polygon button to draw a custom polygon on the work plane. Ctrl-click to reposition the work plane."),
		EToolMessageLevel::UserNotification);
}


void UPolygonOnMeshTool::UpdateVisualization()
{
	FColor PartialPathEdgeColor(240, 15, 15);
	float PartialPathEdgeThickness = 2.0;
	float PartialPathEdgeDepthBias = 2.0f;

	const FDynamicMesh3* TargetMesh = Preview->PreviewMesh->GetPreviewDynamicMesh();
	FVector3d A, B;

	DrawnLineSet->Clear();
	if (!bEmbedSucceeded)
	{
		for (int EID : EmbeddedEdges)
		{
			TargetMesh->GetEdgeV(EID, A, B);
			DrawnLineSet->AddLine((FVector)A, (FVector)B, PartialPathEdgeColor, PartialPathEdgeThickness, PartialPathEdgeDepthBias);
		}
	}
}


void UPolygonOnMeshTool::UpdatePolygonType()
{
	if (BasicProperties->Shape == EPolygonType::Circle)
	{
		ActivePolygon = FPolygon2d::MakeCircle(BasicProperties->Width*0.5, BasicProperties->Subdivisions);
	}
	else if (BasicProperties->Shape == EPolygonType::Square)
	{
		ActivePolygon = FPolygon2d::MakeRectangle(FVector2d::Zero(), BasicProperties->Width, BasicProperties->Width);
	}
	else if (BasicProperties->Shape == EPolygonType::Rectangle)
	{
		ActivePolygon = FPolygon2d::MakeRectangle(FVector2d::Zero(), BasicProperties->Width, BasicProperties->Height);
	}
	else if (BasicProperties->Shape == EPolygonType::RoundRect)
	{
		double Corner = BasicProperties->CornerRatio * FMath::Min(BasicProperties->Width, BasicProperties->Height) * 0.49;
		ActivePolygon = FPolygon2d::MakeRoundedRectangle(FVector2d::Zero(), BasicProperties->Width, BasicProperties->Height, Corner, BasicProperties->Subdivisions);
	}
	else if (BasicProperties->Shape == EPolygonType::Custom)
	{
		if (LastDrawnPolygon.VertexCount() == 0)
		{
			GetToolManager()->DisplayMessage(LOCTEXT("PolygonOnMeshDrawMessage", "Click the Draw Polygon button to draw a custom polygon"), EToolMessageLevel::UserWarning);
			ActivePolygon = FPolygon2d::MakeCircle(BasicProperties->Width*0.5, BasicProperties->Subdivisions);
		}
		else
		{
			ActivePolygon = LastDrawnPolygon;
		}
	}
}

void UPolygonOnMeshTool::UpdateDrawPlane()
{
	Preview->InvalidateResult();
}


void UPolygonOnMeshTool::SetupPreview()
{
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this);
	Preview->Setup(this->TargetWorld, this);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials(MaterialSet.Materials,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	Preview->SetVisibility(true);
}


void UPolygonOnMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	PlaneMechanic->Shutdown();
	if (DrawPolygonMechanic != nullptr)
	{
		DrawPolygonMechanic->Shutdown();
	}

	// Restore (unhide) the source meshes
	ComponentTarget->SetOwnerVisibility(true);

	TArray<FDynamicMeshOpResult> Results;
	Results.Emplace(Preview->Shutdown());
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}
}


TUniquePtr<FDynamicMeshOperator> UPolygonOnMeshTool::MakeNewOperator()
{
	TUniquePtr<FEmbedPolygonsOp> EmbedOp = MakeUnique<FEmbedPolygonsOp>();
	EmbedOp->bDiscardAttributes = false;
	EmbedOp->Operation = BasicProperties->Operation;

	FFrame3d LocalFrame = DrawPlaneWorld;
	FTransform3d ToLocal = WorldTransform.Inverse();
	LocalFrame.Transform(ToLocal);
	EmbedOp->PolygonFrame = LocalFrame;
	
	FVector2d LocalFrameScale(ToLocal.TransformVector(LocalFrame.X()).Length(), ToLocal.TransformVector(LocalFrame.Y()).Length());
	LocalFrameScale *= BasicProperties->PolygonScale;
	EmbedOp->EmbedPolygon = ActivePolygon;
	EmbedOp->EmbedPolygon.Scale(LocalFrameScale, FVector2d::Zero());

	// TODO: scale any extrude by ToLocal.TransformVector(LocalFrame.Z()).Length() ??
	// EmbedOp->ExtrudeDistance = Tool->BasicProperties->ExtrudeDistance;

	EmbedOp->OriginalMesh = OriginalDynamicMesh;
	EmbedOp->SetResultTransform(WorldTransform);

	return EmbedOp;
}




void UPolygonOnMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	PlaneMechanic->Render(RenderAPI);
	
	if (DrawPolygonMechanic != nullptr)
	{
		DrawPolygonMechanic->Render(RenderAPI);
	}
	else
	{
		FToolDataVisualizer Visualizer;
		Visualizer.BeginFrame(RenderAPI);
		double Scale = BasicProperties->PolygonScale;
		const TArray<FVector2d>& Vertices = ActivePolygon.GetVertices();
		int32 NumVertices = Vertices.Num();
		FVector3d PrevPosition = DrawPlaneWorld.FromPlaneUV(Scale * Vertices[0]);
		for (int32 k = 1; k <= NumVertices; ++k)
		{
			FVector3d NextPosition = DrawPlaneWorld.FromPlaneUV(Scale * Vertices[k%NumVertices]);
			Visualizer.DrawLine(PrevPosition, NextPosition, LinearColors::VideoRed3f(), 3.0f, false);
			PrevPosition = NextPosition;
		}
	}
}

void UPolygonOnMeshTool::OnTick(float DeltaTime)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(this->CameraState);

	PlaneMechanic->Tick(DeltaTime);
	Preview->Tick(DeltaTime);

	if (PendingAction != EPolygonOnMeshToolActions::NoAction)
	{
		if (PendingAction == EPolygonOnMeshToolActions::DrawPolygon)
		{
			BeginDrawPolygon();
		}
		PendingAction = EPolygonOnMeshToolActions::NoAction;
	}
}



void UPolygonOnMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	UpdatePolygonType();
	Preview->InvalidateResult();
}

void UPolygonOnMeshTool::RequestAction(EPolygonOnMeshToolActions ActionType)
{
	if (PendingAction != EPolygonOnMeshToolActions::NoAction || DrawPolygonMechanic != nullptr)
	{
		return;
	}
	PendingAction = ActionType;
}



void UPolygonOnMeshTool::BeginDrawPolygon()
{
	check(DrawPolygonMechanic == nullptr);

	GetToolManager()->DisplayMessage(LOCTEXT("PolygonOnMeshBeginDrawMessage", "Click repeatedly on the plane to draw a polygon, and on start point to finish."), EToolMessageLevel::UserWarning);

	DrawPolygonMechanic = NewObject<UCollectSurfacePathMechanic>(this);
	DrawPolygonMechanic->Setup(this);
	double SnapTol = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD();
	DrawPolygonMechanic->SpatialSnapPointsFunc = [this, SnapTol](FVector3d Position1, FVector3d Position2)
	{
		return true && ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, Position1, Position2, SnapTol);
	};
	DrawPolygonMechanic->SetDrawClosedLoopMode();

	DrawPolygonMechanic->InitializePlaneSurface(DrawPlaneWorld);
}


void UPolygonOnMeshTool::CompleteDrawPolygon()
{
	check(DrawPolygonMechanic != nullptr);

	GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);

	FFrame3d DrawFrame = DrawPlaneWorld;
	FPolygon2d TmpPolygon;
	for (const FFrame3d& Point : DrawPolygonMechanic->HitPath)
	{
		TmpPolygon.AppendVertex(DrawFrame.ToPlaneUV(Point.Origin));
	}
	if (TmpPolygon.IsClockwise() == true)
	{
		TmpPolygon.Reverse();
	}

	// check for self-intersections and other invalids

	LastDrawnPolygon = TmpPolygon;
	BasicProperties->Shape = EPolygonType::Custom;
	BasicProperties->PolygonScale = 1.0;
	UpdatePolygonType();
	Preview->InvalidateResult();

	DrawPolygonMechanic->Shutdown();
	DrawPolygonMechanic = nullptr;
}



bool UPolygonOnMeshTool::CanAccept() const
{
	return Super::CanAccept() && Preview != nullptr && Preview->HaveValidResult();
}


void UPolygonOnMeshTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PolygonOnMeshToolTransactionName", "Cut Hole"));
	
	check(Results.Num() > 0);
	check(Results[0].Mesh.Get() != nullptr);
	ComponentTarget->CommitMesh([&Results](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
	{
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(Results[0].Mesh.Get(), *CommitParams.MeshDescription);
	});

	GetToolManager()->EndUndoTransaction();
}





bool UPolygonOnMeshTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	if (DrawPolygonMechanic != nullptr)
	{
		FFrame3d HitPoint;
		if (DrawPolygonMechanic->IsHitByRay(FRay3d(Ray), HitPoint))
		{
			OutHit.Distance = FRay3d(Ray).Project(HitPoint.Origin);
			OutHit.ImpactPoint = (FVector)HitPoint.Origin;
			OutHit.ImpactNormal = (FVector)HitPoint.Z();
			return true;
		}
		return false;
	}

	return false;
}


FInputRayHit UPolygonOnMeshTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FHitResult OutHit;
	if (HitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	return (DrawPolygonMechanic != nullptr) ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
}

void UPolygonOnMeshTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (DrawPolygonMechanic != nullptr)
	{
		if (DrawPolygonMechanic->TryAddPointFromRay(ClickPos.WorldRay))
		{
			if (DrawPolygonMechanic->IsDone())
			{
				CompleteDrawPolygon();
				//GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginOffset", "Set Offset"));
				//OnCompleteSurfacePath();
			}
			else
			{
				//GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginPath", "Begin Path"));
			}
		}
	}

}



FInputRayHit UPolygonOnMeshTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	return (DrawPolygonMechanic != nullptr) ? FInputRayHit(TNumericLimits<float>::Max()) : FInputRayHit();
}


bool UPolygonOnMeshTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (DrawPolygonMechanic != nullptr)
	{
		DrawPolygonMechanic->UpdatePreviewPoint(DevicePos.WorldRay);
	}
	return true;
}





#undef LOCTEXT_NAMESPACE
