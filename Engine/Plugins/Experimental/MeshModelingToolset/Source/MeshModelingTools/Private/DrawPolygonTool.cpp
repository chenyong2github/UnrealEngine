// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DrawPolygonTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "BaseBehaviors/KeyAsModifierInputBehavior.h"

#include "Polygon2.h"
#include "FrameTypes.h"
#include "MatrixTypes.h"
#include "DynamicMeshAttributeSet.h"

#include "MeshDescriptionBuilder.h"
#include "Generators/PlanarPolygonMeshGenerator.h"
#include "Operations/ExtrudeMesh.h"
#include "Distance/DistLine3Ray3.h"
#include "Intersection/IntrSegment2Segment2.h"
#include "MeshQueries.h"
#include "ToolSceneQueriesUtil.h"

#include "DynamicMeshEditor.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"
#include "Drawing/MeshDebugDrawing.h"

#include "Selection/SelectClickedAction.h"
#include "Selection/ToolSelectionUtil.h"
#include "AssetGenerationUtil.h"

#include "StaticMeshComponentBuilder.h"

// [RMS] need these for now
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"


#define LOCTEXT_NAMESPACE "UDrawPolygonTool"


/*
 * ToolBuilder
 */
constexpr int StartPointSnapID = FPointPlanarSnapSolver::BaseExternalPointID + 1;
constexpr int CurrentSceneSnapID = FPointPlanarSnapSolver::BaseExternalPointID + 2;

bool UDrawPolygonToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (this->AssetAPI != nullptr);
}

UInteractiveTool* UDrawPolygonToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UDrawPolygonTool* NewTool = NewObject<UDrawPolygonTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}

/*
 * Properties
 */
UDrawPolygonToolStandardProperties::UDrawPolygonToolStandardProperties()
{
}


void UDrawPolygonToolStandardProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UDrawPolygonToolStandardProperties* PropertyCache = GetPropertyCache<UDrawPolygonToolStandardProperties>();
	PropertyCache->PolygonType = this->PolygonType;
	PropertyCache->OutputMode = this->OutputMode;
	PropertyCache->ExtrudeHeight = this->ExtrudeHeight;
	PropertyCache->Steps = this->Steps;
	PropertyCache->bAllowSelfIntersections = this->bAllowSelfIntersections;
	PropertyCache->bShowGizmo = this->bShowGizmo;
}

void UDrawPolygonToolStandardProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UDrawPolygonToolStandardProperties* PropertyCache = GetPropertyCache<UDrawPolygonToolStandardProperties>();
	this->PolygonType = PropertyCache->PolygonType;
	this->OutputMode = PropertyCache->OutputMode;
	this->ExtrudeHeight = PropertyCache->ExtrudeHeight;
	this->Steps = PropertyCache->Steps;
	this->bAllowSelfIntersections = PropertyCache->bAllowSelfIntersections;
	this->bShowGizmo = PropertyCache->bShowGizmo;
}



void UDrawPolygonToolSnapProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UDrawPolygonToolSnapProperties* PropertyCache = GetPropertyCache<UDrawPolygonToolSnapProperties>();
	PropertyCache->bEnableSnapping = this->bEnableSnapping;
	PropertyCache->bSnapToVertices = this->bSnapToVertices;
	PropertyCache->bSnapToEdges = this->bSnapToEdges;
	PropertyCache->bSnapToAngles = this->bSnapToAngles;
	PropertyCache->bSnapToLengths = this->bSnapToLengths;
	PropertyCache->bHitSceneObjects = this->bHitSceneObjects;
	//PropertyCache->SegmentLength = this->Length;		// this is purely a feedback property
	PropertyCache->HitNormalOffset = this->HitNormalOffset;
}

void UDrawPolygonToolSnapProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UDrawPolygonToolSnapProperties* PropertyCache = GetPropertyCache<UDrawPolygonToolSnapProperties>();
	this->bEnableSnapping = PropertyCache->bEnableSnapping;
	this->bSnapToVertices = PropertyCache->bSnapToVertices;
	this->bSnapToEdges = PropertyCache->bSnapToEdges;
	this->bSnapToAngles = PropertyCache->bSnapToAngles;
	this->bSnapToLengths = PropertyCache->bSnapToLengths;
	this->bHitSceneObjects = PropertyCache->bHitSceneObjects;
	//this->SegmentLength = PropertyCache->Length;
	this->HitNormalOffset = PropertyCache->HitNormalOffset;
}




/*
 * Tool
 */
UDrawPolygonTool::UDrawPolygonTool()
{
	DrawPlaneOrigin = FVector::ZeroVector;
	DrawPlaneOrientation = FQuat::Identity;
	bInInteractiveExtrude = false;
}

void UDrawPolygonTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UDrawPolygonTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

void UDrawPolygonTool::Setup()
{
	UInteractiveTool::Setup();

	// add default button input behaviors for devices
	UMultiClickSequenceInputBehavior* MouseBehavior = NewObject<UMultiClickSequenceInputBehavior>();
	MouseBehavior->Initialize(this);
	MouseBehavior->Modifiers.RegisterModifier(IgnoreSnappingModifier, FInputDeviceState::IsShiftKeyDown);
	AddInputBehavior(MouseBehavior);

	// Register a click behavior/action pair, that sets the draw plane to the clicked world position
	FSelectClickedAction* SetPlaneAction = new FSelectClickedAction();
	SetPlaneAction->World = this->TargetWorld;
	SetPlaneAction->OnClickedPositionFunc = [this](const FHitResult& Hit) {
		SetDrawPlaneFromWorldPos(Hit.ImpactPoint, Hit.ImpactNormal);
	};
	SetPointInWorldConnector = SetPlaneAction;

	USingleClickInputBehavior* ClickToSetPlaneBehavior = NewObject<USingleClickInputBehavior>();
	ClickToSetPlaneBehavior->ModifierCheckFunc = FInputDeviceState::IsCtrlKeyDown;
	ClickToSetPlaneBehavior->Initialize(SetPointInWorldConnector);
	ClickToSetPlaneBehavior->SetDefaultPriority(MouseBehavior->GetPriority().MakeHigher());
	AddInputBehavior(ClickToSetPlaneBehavior);

	// register modifier key behaviors   (disabled because it is not implemented yet)
	//UKeyAsModifierInputBehavior* AKeyBehavior = NewObject<UKeyAsModifierInputBehavior>();
	//AKeyBehavior->Initialize(this, AngleSnapModifier, EKeys::A);
	//AddInputBehavior(AKeyBehavior);


	PolygonProperties = NewObject<UDrawPolygonToolStandardProperties>(this, TEXT("Polygon Settings"));
	PolygonProperties->RestoreProperties(this);
	ShowGizmoWatcher.Initialize(
		[this]() { return this->PolygonProperties->bShowGizmo; }, 
		[this](bool bNewValue) { this->UpdateShowGizmoState(bNewValue); },
		true);

	// Create a new TransformGizmo and associated TransformProxy. The TransformProxy will not be the
	// parent of any Components in this case, we just use it's transform and change delegate.
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformProxy->SetTransform(FTransform(DrawPlaneOrientation, DrawPlaneOrigin));
	PlaneTransformGizmo = GetToolManager()->GetPairedGizmoManager()->Create3AxisTransformGizmo(this);
	PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy, GetToolManager());
	// listen for changes to the proxy and update the plane when that happens
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UDrawPolygonTool::PlaneTransformChanged);

	// initialize material properties for new objects
	MaterialProperties = NewObject<UNewMeshMaterialProperties>(this);
	MaterialProperties->RestoreProperties(this);

	// create preview mesh object
	PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("DrawPolygonPreviewMesh"));
	PreviewMesh->CreateInWorld(this->TargetWorld, FTransform::Identity);
	PreviewMesh->SetVisible(false);
	PreviewMesh->SetMaterial(MaterialProperties->Material);
	bPreviewUpdatePending = false;

	// initialize snapping engine and properties
	SnapEngine.SnapMetricTolerance = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD();
	SnapEngine.SnapMetricFunc = [this](const FVector3d& Position1, const FVector3d& Position2) {
		return ToolSceneQueriesUtil::CalculateViewVisualAngleD(this->CameraState, Position1, Position2);
	};
	SnapEngine.Plane = FFrame3d((FVector3d)DrawPlaneOrigin, (FQuaterniond)DrawPlaneOrientation);

	SnapProperties = NewObject<UDrawPolygonToolSnapProperties>(this, TEXT("Snapping"));
	SnapProperties->RestoreProperties(this);

	// register tool properties
	AddToolPropertySource(PolygonProperties);
	AddToolPropertySource(SnapProperties);
	AddToolPropertySource(MaterialProperties);

	ShowStartupMessage();
}


void UDrawPolygonTool::Shutdown(EToolShutdownType ShutdownType)
{
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	if (SetPointInWorldConnector != nullptr)
	{
		delete SetPointInWorldConnector;
	}

	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);

	PolygonProperties->SaveProperties(this);
	SnapProperties->SaveProperties(this);
	MaterialProperties->SaveProperties(this);
}

void UDrawPolygonTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
		TEXT("PopLastVertex"), 
		LOCTEXT("PopLastVertex", "Pop Last Vertex"),
		LOCTEXT("PopLastVertexTooltip", "Pop last vertex added to polygon"),
		EModifierKey::None, EKeys::BackSpace,
		[this]() { PopLastVertexAction(); });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
		TEXT("ToggleGizmo"),
		LOCTEXT("ToggleGizmo", "Toggle Gizmo"),
		LOCTEXT("ToggleGizmoTooltip", "Toggle visibility of the transformation Gizmo"),
		EModifierKey::None, EKeys::A,
		[this]() { PolygonProperties->bShowGizmo = !PolygonProperties->bShowGizmo; });

}



void UDrawPolygonTool::PopLastVertexAction()
{
	if (bInInteractiveExtrude || PolygonVertices.Num() == 0)
	{
		return;
	}

	if (bInFixedPolygonMode == false)
	{
		int NumVertices = PolygonVertices.Num();
		if (NumVertices > 1)
		{
			PolygonVertices.RemoveAt(NumVertices-1);
		}
		else
		{
			PolygonVertices.RemoveAt(0);
			bAbortActivePolygonDraw = true;
		}
	}
	else
	{
		int NumVertices = FixedPolygonClickPoints.Num();
		if (NumVertices > 1)
		{
			FixedPolygonClickPoints.RemoveAt(NumVertices - 1);
		}
		else
		{
			FixedPolygonClickPoints.RemoveAt(0);
			bAbortActivePolygonDraw = true;
		}
	}
}

void DrawEdgeTicks(FPrimitiveDrawInterface* PDI, 
	const FSegment3f& Segment, float Height,
	const FVector3f& PlaneNormal, 
	const FLinearColor& Color, uint8 DepthPriorityGroup, float LineThickness, bool bIsScreenSpace)
{
	FVector3f Center = Segment.Center;
	FVector3f X = Segment.Direction;
	FVector3f Y = X.Cross(PlaneNormal);
	Y.Normalize();
	FVector A = Center - Height * 0.25f*X - Height * Y;
	FVector B = Center + Height * 0.25f*X + Height * Y;
	PDI->DrawLine(A, B, Color, DepthPriorityGroup, LineThickness, 0.0f, bIsScreenSpace);
	A += Height * 0.5f*X;
	B += Height * 0.5f*X;
	PDI->DrawLine(A, B, Color, DepthPriorityGroup, LineThickness, 0.0f, bIsScreenSpace);
}

void UDrawPolygonTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (bPreviewUpdatePending)
	{
		UpdateLivePreview();
		bPreviewUpdatePending = false;
	}

	double CurViewSizeFactor = ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(CameraState, PreviewVertex, 1.0);

	FColor OpenPolygonColor(240, 16, 240);
	FColor ClosedPolygonColor(16, 240, 16);
	FColor ErrorColor(240, 16, 16);
	float HiddenLineThickness = 1.0f;
	float LineThickness = 4.0f;
	float SelfIntersectThickness = 8.0f;
	FColor GridColor(128, 128, 128, 32);
	float GridThickness = 0.5f;
	float GridLineSpacing = 25.0f;   // @todo should be relative to view
	int NumGridLines = 21;
	FColor SnapHighlightColor(240, 200, 16);
	float ElementSize = CurViewSizeFactor;

	bool bIsClosed = SnapEngine.HaveActiveSnap() && SnapEngine.GetActiveSnapTargetID() == StartPointSnapID;

	if (bInInteractiveExtrude == false)
	{
		FFrame3f DrawFrame(DrawPlaneOrigin, DrawPlaneOrientation);
		MeshDebugDraw::DrawSimpleGrid(DrawFrame, NumGridLines, GridLineSpacing, GridThickness, GridColor, false, PDI, FTransform::Identity);
	}

	if (bInFixedPolygonMode)
	{
		if (bInInteractiveExtrude == false)		// once we are in extrude, polygon is done
		{
			FixedPolygonClickPoints.Add(PreviewVertex);
			GenerateFixedPolygon(FixedPolygonClickPoints, PolygonVertices);
			FixedPolygonClickPoints.Pop(false);
		}
		bIsClosed = true;
	}

	int NumVerts = PolygonVertices.Num();

	if (SnapEngine.HaveActiveSnap())
	{
		PDI->DrawPoint((FVector)SnapEngine.GetActiveSnapToPoint(), ClosedPolygonColor, 10, SDPG_Foreground);
		
		PDI->DrawPoint((FVector)SnapEngine.GetActiveSnapFromPoint(), OpenPolygonColor, 15, SDPG_Foreground);
		PDI->DrawLine((FVector)SnapEngine.GetActiveSnapToPoint(), (FVector)SnapEngine.GetActiveSnapFromPoint(),
			ClosedPolygonColor, SDPG_Foreground, 0.5f, 0.0f, true);
		if (SnapEngine.GetActiveSnapTargetID() == CurrentSceneSnapID)
		{
			if (LastSnapGeometry.PointCount == 1) {
				DrawCircle(PDI, (FVector)LastSnapGeometry.Points[0], CameraState.Right(), CameraState.Up(),
					SnapHighlightColor, ElementSize, 32, SDPG_Foreground, 1.0f, 0.0f, true);
			} 
			else
			{
				PDI->DrawLine((FVector)LastSnapGeometry.Points[0], (FVector)LastSnapGeometry.Points[1],
					SnapHighlightColor, SDPG_Foreground, 1.0f, 0.0f, true);
			}
		}

		if (SnapEngine.HaveActiveSnapLine())
		{
			FLine3d SnapLine = SnapEngine.GetActiveSnapLine();
			PDI->DrawLine((FVector)SnapLine.PointAt(-9999), (FVector)SnapLine.PointAt(9999),
				ClosedPolygonColor, SDPG_Foreground, 0.5, 0.0f, true);

			if (SnapEngine.HaveActiveSnapDistance())
			{
				int iSegment = SnapEngine.GetActiveSnapDistanceID();
				TArray<FVector>& HistoryPoints = (bInFixedPolygonMode) ? FixedPolygonClickPoints : PolygonVertices;
				FVector UseNormal = DrawPlaneOrientation.GetAxisZ();
				DrawEdgeTicks(PDI, FSegment3f(HistoryPoints[iSegment], HistoryPoints[iSegment+1]),
					0.75f*ElementSize, UseNormal, SnapHighlightColor, SDPG_Foreground, 1.0f, true);
				DrawEdgeTicks(PDI, FSegment3f(HistoryPoints[HistoryPoints.Num()-1], PreviewVertex),
					0.75f*ElementSize, UseNormal, SnapHighlightColor, SDPG_Foreground, 1.0f, true);
				PDI->DrawLine(HistoryPoints[iSegment], HistoryPoints[iSegment + 1],
					SnapHighlightColor, SDPG_Foreground, 2.0f, 0.0f, true);
			}
		}
	}


	if (bHaveSurfaceHit)
	{
		PDI->DrawPoint((FVector)SurfaceHitPoint, ClosedPolygonColor, 10, SDPG_Foreground);
		if (SnapProperties->HitNormalOffset != 0)
		{
			PDI->DrawPoint((FVector)SurfaceOffsetPoint, OpenPolygonColor, 15, SDPG_Foreground);
			PDI->DrawLine((FVector)SurfaceOffsetPoint, (FVector)SurfaceHitPoint,
				ClosedPolygonColor, SDPG_Foreground, 0.5f, 0.0f, true);
		}
		PDI->DrawLine((FVector)SurfaceOffsetPoint, (FVector)PreviewVertex,
			ClosedPolygonColor, SDPG_Foreground, 0.5f, 0.0f, true);
	}


	if (PolygonVertices.Num() > 0)
	{
		FColor UseColor = (bIsClosed) ? ClosedPolygonColor : OpenPolygonColor;
		FVector UseLastVertex = (bIsClosed) ? PolygonVertices[0] : PreviewVertex;
		float UseThickness = LineThickness;
		if (bHaveSelfIntersection)
		{
			UseColor = ErrorColor;
			UseThickness = SelfIntersectThickness;
		}

		// draw thin no-depth
		for (int i = 0; i < NumVerts - 1; ++i)
		{
			PDI->DrawLine(PolygonVertices[i], PolygonVertices[i + 1],
				UseColor, SDPG_Foreground, HiddenLineThickness, 0.0f, true);
		}
		PDI->DrawLine(PolygonVertices[NumVerts - 1], UseLastVertex,
			UseColor, SDPG_Foreground, HiddenLineThickness, 0.0f, true);


		// draw thick depth-tested
		for (int i = 0; i < NumVerts-1; ++i)
		{
			PDI->DrawLine(PolygonVertices[i], PolygonVertices[i+1],
				UseColor, SDPG_World, LineThickness, 0.0f, true);
		}
		PDI->DrawLine(PolygonVertices[NumVerts-1], UseLastVertex,
			UseColor, SDPG_World, LineThickness, 0.0f, true);

		if (bHaveSelfIntersection)
		{
			PDI->DrawPoint(SelfIntersectionPoint, ErrorColor, 10, SDPG_Foreground);
		}
	}

	// draw preview vertex
	PDI->DrawPoint(PreviewVertex, ClosedPolygonColor, 10, SDPG_Foreground);


	// todo should be an indicator
	if (bInInteractiveExtrude)
	{
		float Length = 10; float Thickness = 2;
		FColor HitFrameColor(0, 128, 128);
		PDI->DrawLine(
			HitPosFrameWorld.PointAt(-Length, -Length, 0), HitPosFrameWorld.PointAt(Length, Length, 0),
			HitFrameColor, 1, Thickness, 0.0f, true);
		PDI->DrawLine(
			HitPosFrameWorld.PointAt(-Length, Length, 0), HitPosFrameWorld.PointAt(Length, -Length, 0),
			HitFrameColor, 1, Thickness, 0.0f, true);

		FVector PreviewOrigin = (FVector)PreviewHeightFrame.Origin;

		FVector DrawPlaneNormal = DrawPlaneOrientation.GetAxisZ();

		FColor AxisColor(128, 128, 0);
		PDI->DrawLine(
			PreviewOrigin -1000*DrawPlaneNormal, PreviewOrigin +1000*DrawPlaneNormal,
			AxisColor, 1, 1.0f, 0.0f, true);

		FColor HeightPosColor(128, 0, 128);
		PDI->DrawLine(
			PreviewOrigin + PolygonProperties->ExtrudeHeight*DrawPlaneNormal, HitPosFrameWorld.Origin,
			HeightPosColor, 1, 1.0f, 0.0f, true);
	}


	ShowGizmoWatcher.CheckAndUpdate();
}


void UDrawPolygonTool::ResetPolygon()
{
	PolygonVertices.Reset();
	SnapEngine.Reset();
	bHaveSurfaceHit = false;
	bInFixedPolygonMode = false;
}

void UDrawPolygonTool::UpdatePreviewVertex(const FVector& PreviewVertexIn)
{
	PreviewVertex = PreviewVertexIn;

	// update length and angle
	if (PolygonVertices.Num() > 0)
	{
		FVector LastVertex = PolygonVertices[PolygonVertices.Num() - 1];
		SnapProperties->SegmentLength = FVector::Distance(LastVertex, PreviewVertex);
	}
}

void UDrawPolygonTool::AppendVertex(const FVector& Vertex)
{
	PolygonVertices.Add(Vertex);
}

bool UDrawPolygonTool::FindDrawPlaneHitPoint(const FInputDeviceRay& ClickPos, FVector& HitPosOut)
{
	bHaveSurfaceHit = false;

	FFrame3d Frame(DrawPlaneOrigin, DrawPlaneOrientation);
	FVector3d HitPos;
	bool bHit = Frame.RayPlaneIntersection(ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction, 2, HitPos);
	if (bHit == false)
	{
		return false;
	}

	// if we found a scene snap point, add to snap set
	FVector3d SnapPos;
	if (bIgnoreSnappingToggle || SnapProperties->bEnableSnapping == false)
	{
		SnapEngine.ResetActiveSnap();
		SnapEngine.UpdatePointHistory(TArray<FVector>());
	}
	else 
	{
		if (ToolSceneQueriesUtil::FindSceneSnapPoint(this, HitPos, SnapPos, SnapProperties->bSnapToVertices, SnapProperties->bSnapToEdges, 0, &LastSnapGeometry))
		{
			SnapEngine.AddPointTarget(SnapPos, CurrentSceneSnapID, SnapEngine.MinInternalPriority()-1 );
		}

		TArray<FVector>& HistoryPoints = (bInFixedPolygonMode) ? FixedPolygonClickPoints : PolygonVertices;
		SnapEngine.UpdatePointHistory(HistoryPoints);
		if (SnapProperties->bSnapToAngles)
		{
			SnapEngine.RegenerateTargetLines(true, true);
		}
		SnapEngine.bEnableSnapToKnownLengths = SnapProperties->bSnapToLengths;
	}

	SnapEngine.UpdateSnappedPoint(HitPos);

	// remove scene snap point
	SnapEngine.RemovePointTargetsByID(CurrentSceneSnapID);

	if (SnapEngine.HaveActiveSnap())
	{
		HitPosOut = (FVector)SnapEngine.GetActiveSnapToPoint();
		return true;
	}


	// if not snap and we want to hit objects, do that
	if (SnapProperties->bHitSceneObjects)
	{
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		FHitResult Result;
		bool bWorldHit = TargetWorld->LineTraceSingleByObjectType(Result, ClickPos.WorldRay.Origin, ClickPos.WorldRay.PointAt(9999), QueryParams);
		if (bWorldHit)
		{
			bHaveSurfaceHit = true;
			SurfaceHitPoint = Result.ImpactPoint;
			FVector UseHitPos = Result.ImpactPoint + SnapProperties->HitNormalOffset*Result.Normal;
			HitPos = Frame.ToPlane((FVector3d)UseHitPos, 2);
			SurfaceOffsetPoint = UseHitPos;
		}
	}

	HitPosOut = (FVector)HitPos;
	return true;
}

void UDrawPolygonTool::OnBeginSequencePreview(const FInputDeviceRay& DevicePos)
{
	// just update snapped point preview
	FVector HitPos;
	if (FindDrawPlaneHitPoint(DevicePos, HitPos))
	{
		PreviewVertex = HitPos;
	}
	
}

bool UDrawPolygonTool::CanBeginClickSequence(const FInputDeviceRay& ClickPos)
{
	return true;
}

void UDrawPolygonTool::OnBeginClickSequence(const FInputDeviceRay& ClickPos)
{
	ResetPolygon();
	
	FVector HitPos;
	bool bHit = FindDrawPlaneHitPoint(ClickPos, HitPos);
	if (bHit == false)
	{
		bAbortActivePolygonDraw = true;
		return;
	}
	if (ToolSceneQueriesUtil::IsPointVisible(CameraState, HitPos) == false)
	{
		bAbortActivePolygonDraw = true;
		return;		// cannot start a poly an a point that is not visible, this is almost certainly an error due to draw plane
	}

	AppendVertex(HitPos);
	UpdatePreviewVertex(HitPos);

	bInFixedPolygonMode = (PolygonProperties->PolygonType != EDrawPolygonDrawMode::Freehand);
	FixedPolygonClickPoints.Reset();
	FixedPolygonClickPoints.Add(HitPos);

	// if we are starting a freehand poly, add start point as snap target, but then ignore it until we get 3 verts
	if (bInFixedPolygonMode == false)
	{
		SnapEngine.AddPointTarget(PolygonVertices[0], StartPointSnapID, 1);
		SnapEngine.AddIgnoreTarget(StartPointSnapID);
	}
}

void UDrawPolygonTool::OnNextSequencePreview(const FInputDeviceRay& ClickPos)
{
	if (bInInteractiveExtrude)
	{
		PolygonProperties->ExtrudeHeight = FindInteractiveHeightDistance(ClickPos);
		bPreviewUpdatePending = true;
		return;
	}

	FVector HitPos;
	bool bHit = FindDrawPlaneHitPoint(ClickPos, HitPos);
	if (bHit == false)
	{
		return;
	}

	if (bInFixedPolygonMode)
	{
		UpdatePreviewVertex(HitPos);
		bPreviewUpdatePending = true;
		return;
	}

	if (PolygonVertices.Num() > 2)
	{
		bPreviewUpdatePending = true;
	}

	UpdatePreviewVertex(HitPos);
	UpdateSelfIntersection();
}

bool UDrawPolygonTool::OnNextSequenceClick(const FInputDeviceRay& ClickPos)
{
	if (bInInteractiveExtrude)
	{
		EndInteractiveExtrude();
		return false;
	}

	FVector HitPos;
	bool bHit = FindDrawPlaneHitPoint(ClickPos, HitPos);
	if (bHit == false)
	{
		return true;  // ignore click but continue accepting clicks
	}

	bool bDonePolygon = false;
	if (bInFixedPolygonMode)
	{
		// ignore very close click points
		if ( ToolSceneQueriesUtil::PointSnapQuery(this, FixedPolygonClickPoints[FixedPolygonClickPoints.Num()-1], HitPos) )
		{
			return true;
		}

		FixedPolygonClickPoints.Add(HitPos);
		int NumTargetPoints = (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Rectangle) ? 3 : 2;
		bDonePolygon = (FixedPolygonClickPoints.Num() == NumTargetPoints);
		if (bDonePolygon)
		{
			GenerateFixedPolygon(FixedPolygonClickPoints, PolygonVertices);
		}
	} 
	else
	{
		// ignore very close click points
		if (ToolSceneQueriesUtil::PointSnapQuery(this, PolygonVertices[PolygonVertices.Num()-1], HitPos))
		{
			return true;
		}

		// close polygon if we clicked on start point
		bDonePolygon = SnapEngine.HaveActiveSnap() && SnapEngine.GetActiveSnapTargetID() == StartPointSnapID;

		if (bHaveSelfIntersection)
		{
			// discard vertex in segments before intersection (this is redundant if idx is 0)
			for (int j = SelfIntersectSegmentIdx; j < PolygonVertices.Num(); ++j)
			{
				PolygonVertices[j-SelfIntersectSegmentIdx] = PolygonVertices[j];
			}
			PolygonVertices.SetNum(PolygonVertices.Num() - SelfIntersectSegmentIdx);
			PolygonVertices[0] = PreviewVertex = SelfIntersectionPoint;
			bDonePolygon = true;
		}
	}

	
	if (bDonePolygon)
	{
		SnapEngine.Reset();
		bHaveSurfaceHit = false;
		if (PolygonProperties->OutputMode == EDrawPolygonOutputMode::ExtrudedInteractive)
		{
			BeginInteractiveExtrude();

			PreviewMesh->ClearPreview();
			PreviewMesh->SetVisible(true);

			return true;
		}
		else 
		{
			EmitCurrentPolygon();

			PreviewMesh->ClearPreview();
			PreviewMesh->SetVisible(false);

			return false;
		}
	}

	AppendVertex(HitPos);
	if (PolygonVertices.Num() > 2)
	{
		SnapEngine.RemoveIgnoreTarget(StartPointSnapID);
	}

	UpdatePreviewVertex(HitPos);
	return true;
}

void UDrawPolygonTool::OnTerminateClickSequence()
{
	ResetPolygon();
}

bool UDrawPolygonTool::RequestAbortClickSequence()
{
	if (bAbortActivePolygonDraw)
	{
		bAbortActivePolygonDraw = false;
		return true;
	}
	return false;
}

void UDrawPolygonTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == IgnoreSnappingModifier)
	{
		bIgnoreSnappingToggle = bIsOn;
	}
	else if (ModifierID == AngleSnapModifier)
	{

	}
}

bool UDrawPolygonTool::UpdateSelfIntersection()
{
	bHaveSelfIntersection = false;
	if (bInFixedPolygonMode || PolygonProperties->bAllowSelfIntersections == true)
	{
		return false;
	}

	int NumVertices = PolygonVertices.Num();

	FFrame3f DrawFrame(DrawPlaneOrigin, DrawPlaneOrientation);
	FSegment2f PreviewSegment(DrawFrame.ToPlaneUV(PolygonVertices[NumVertices - 1],2), DrawFrame.ToPlaneUV(PreviewVertex,2));

	for (int k = 0; k < NumVertices - 2; ++k) 
	{
		FSegment2f Segment(DrawFrame.ToPlaneUV(PolygonVertices[k],2), DrawFrame.ToPlaneUV(PolygonVertices[k + 1],2));
		FIntrSegment2Segment2f Intersection(PreviewSegment, Segment);
		if (Intersection.Find()) 
		{
			bHaveSelfIntersection = true;
			SelfIntersectSegmentIdx = k;
			SelfIntersectionPoint = DrawFrame.FromPlaneUV(Intersection.Point0,2);
			return true;
		}
	}
	return false;
}

void UDrawPolygonTool::GenerateFixedPolygon(TArray<FVector>& FixedPoints, TArray<FVector>& VerticesOut)
{
	FFrame3f DrawFrame(DrawPlaneOrigin, DrawPlaneOrientation);
	FVector2f CenterPt = DrawFrame.ToPlaneUV(FixedPoints[0], 2 );
	FVector2f EdgePt = DrawFrame.ToPlaneUV(FixedPoints[1], 2);
	FVector2f Delta = EdgePt - CenterPt;
	float AngleRad = FMath::Atan2(Delta.Y, Delta.X);
	FMatrix2f RotationMat = FMatrix2f::RotationRad(AngleRad);
	FVector2f RotAxisX = RotationMat * FVector2f::UnitX();
	float Dist = Delta.Length();
	float Width = FMath::Abs(Delta.Dot(RotAxisX));

	FPolygon2f Polygon;
	if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Square)
	{
		Polygon = FPolygon2f::MakeRectangle(FVector2f::Zero(), 2*Width, 2*Width);
	}
	else if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Rectangle)
	{
		FVector2f HeightPt = DrawFrame.ToPlaneUV((FixedPoints.Num() == 3) ? FixedPoints[2] : FixedPoints[1], 2);
		FVector2f HeightDelta = HeightPt - CenterPt;
		FVector2f RotAxisY = RotationMat * FVector2f::UnitY();
		float YSign = FMath::Sign(HeightDelta.Dot(RotAxisY));
		float Height = FMath::Abs(HeightDelta.Dot(RotAxisY));
		Polygon = FPolygon2f::MakeRectangle(FVector2f(Width/2, YSign*Height/2), Width, Height);
	}
	else
	{
		Polygon = FPolygon2f::MakeCircle(Dist, PolygonProperties->Steps, 0);
	}
	Polygon.Transform([RotationMat](const FVector2f& Pt) { return RotationMat * Pt; });

	VerticesOut.Reset();
	for (int k = 0; k < Polygon.VertexCount(); ++k)
	{
		FVector2f NewPt = CenterPt + Polygon[k];
		VerticesOut.Add(DrawFrame.FromPlaneUV(NewPt, 2));
	}
}

void UDrawPolygonTool::BeginInteractiveExtrude()
{
	bInInteractiveExtrude = true;

	GeneratePreviewHeightTarget();

	ShowExtrudeMessage();
}

void UDrawPolygonTool::EndInteractiveExtrude()
{
	EmitCurrentPolygon();

	PreviewMesh->ClearPreview();
	PreviewMesh->SetVisible(false);

	bInInteractiveExtrude = false;

	ShowStartupMessage();
}

float UDrawPolygonTool::FindInteractiveHeightDistance(const FInputDeviceRay& ClickPos)
{
	float NearestHitDist = TNumericLimits<float>::Max();
	float NearestHitHeight = 1.0f;
	FFrame3f NearestHitFrameWorld;

	// cast ray at target object
	FRay3d LocalRay = PreviewHeightFrame.ToFrame((FRay3d)ClickPos.WorldRay);
	int HitTID = PreviewHeightTargetAABB.FindNearestHitTriangle(LocalRay);
	if (HitTID >= 0)
	{
		FIntrRay3Triangle3d IntrQuery =
			TMeshQueries<FDynamicMesh3>::TriangleIntersection(PreviewHeightTarget, HitTID, LocalRay);
		FVector3d HitPosLocal = LocalRay.PointAt(IntrQuery.RayParameter);
		FVector3d HitNormalLocal = PreviewHeightTarget.GetTriNormal(HitTID);

		NearestHitFrameWorld = FFrame3f(
			(FVector3f)PreviewHeightFrame.FromFramePoint(HitPosLocal),
			(FVector3f)PreviewHeightFrame.FromFrameVector(HitNormalLocal));
		NearestHitHeight = HitPosLocal.Z;
		NearestHitDist = ClickPos.WorldRay.GetParameter(NearestHitFrameWorld.Origin);
	}
	

	// cast ray into scene
	FVector RayStart = ClickPos.WorldRay.Origin;
	FVector RayEnd = ClickPos.WorldRay.PointAt(999999);
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(Result, RayStart, RayEnd, QueryParams);
	if (bHitWorld)
	{
		float WorldHitDist = ClickPos.WorldRay.GetParameter(Result.ImpactPoint);
		if (WorldHitDist < NearestHitDist)
		{
			NearestHitFrameWorld = FFrame3f(Result.ImpactPoint, Result.ImpactNormal);
			FVector3d HitPosWorld = Result.ImpactPoint;
			FVector3d HitPosLocal = PreviewHeightFrame.ToFramePoint(HitPosWorld);
			NearestHitHeight = HitPosLocal.Z;
			NearestHitDist = WorldHitDist;
		}
	}

	if (NearestHitDist < TNumericLimits<float>::Max())
	{
		this->HitPosFrameWorld = NearestHitFrameWorld;
		return NearestHitHeight;
	}
	else
	{
		return PolygonProperties->ExtrudeHeight;
	}
	
}

void UDrawPolygonTool::SetDrawPlaneFromWorldPos(const FVector& Position, const FVector& Normal)
{
	DrawPlaneOrigin = Position;

	FFrame3f DrawPlane(Position, DrawPlaneOrientation);
	if (bIgnoreSnappingToggle == false)
	{
		DrawPlane.AlignAxis(2, Normal);
		DrawPlane.ConstrainedAlignPerpAxes();
		DrawPlaneOrientation = DrawPlane.Rotation;
	}

	SnapEngine.Plane = FFrame3d((FVector3d)DrawPlane.Origin, (FQuaterniond)DrawPlane.Rotation);

	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(DrawPlaneOrientation, DrawPlaneOrigin));
	}
}


void UDrawPolygonTool::PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	DrawPlaneOrientation = Transform.GetRotation();
	DrawPlaneOrigin = Transform.GetLocation();
	SnapEngine.Plane = FFrame3d((FVector3d)DrawPlaneOrigin, (FQuaterniond)DrawPlaneOrientation);
}

void UDrawPolygonTool::UpdateShowGizmoState(bool bNewVisibility)
{
	if (bNewVisibility == false)
	{
		GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);
		PlaneTransformGizmo = nullptr;
	}
	else
	{
		PlaneTransformGizmo = GetToolManager()->GetPairedGizmoManager()->Create3AxisTransformGizmo(this);
		PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy, GetToolManager());
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(DrawPlaneOrientation, DrawPlaneOrigin));
	}
}


void UDrawPolygonTool::EmitCurrentPolygon()
{
	FString BaseName = (PolygonProperties->OutputMode == EDrawPolygonOutputMode::MeshedPolygon) ?
		TEXT("Polygon") : TEXT("Extrude");

#if WITH_EDITOR
	// generate new mesh
	FFrame3d PlaneFrameOut;
	FDynamicMesh3 Mesh;
	double ExtrudeDist = (PolygonProperties->OutputMode == EDrawPolygonOutputMode::MeshedPolygon) ?
		0 : PolygonProperties->ExtrudeHeight;
	GeneratePolygonMesh(PolygonVertices, &Mesh, PlaneFrameOut, false, ExtrudeDist, false);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("CreatePolygon", "Create Polygon"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		&Mesh, PlaneFrameOut.ToTransform(), BaseName,
		AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath(),
		MaterialProperties->Material);

	// select newly-created object
	ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);

	GetToolManager()->EndUndoTransaction();
#else
	checkNoEntry();
#endif
	ResetPolygon();
}

void UDrawPolygonTool::UpdateLivePreview()
{
	int NumVerts = PolygonVertices.Num();
	if (NumVerts < 2 || PreviewMesh == nullptr || PreviewMesh->IsVisible() == false )
	{
		return;
	}

	FFrame3d PlaneFrame;
	FDynamicMesh3 Mesh;
	double ExtrudeDist = (PolygonProperties->OutputMode == EDrawPolygonOutputMode::MeshedPolygon) ?
		0 : PolygonProperties->ExtrudeHeight;
	GeneratePolygonMesh(PolygonVertices, &Mesh, PlaneFrame, false, ExtrudeDist, false);

	PreviewMesh->SetTransform(PlaneFrame.ToFTransform());
	PreviewMesh->SetMaterial(MaterialProperties->Material);
	PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
	PreviewMesh->UpdatePreview(&Mesh);
}

void UDrawPolygonTool::GeneratePolygonMesh(const TArray<FVector>& Polygon, FDynamicMesh3* ResultMeshOut, FFrame3d& WorldFrameOut, bool bIncludePreviewVtx, double ExtrudeDistance, bool bExtrudeSymmetric)
{
	// construct centered frame for polygon
	WorldFrameOut = FFrame3d(DrawPlaneOrigin, DrawPlaneOrientation);

	int NumVerts = Polygon.Num();
	FVector3d Centroid(0, 0, 0);
	for (int k = 0; k < NumVerts; ++k)
	{
		Centroid += Polygon[k];
	}
	Centroid /= (double)NumVerts;
	WorldFrameOut.Origin = Centroid;


	// triangulate polygon into the MeshDescription
	FPlanarPolygonMeshGenerator PolyMeshGen;
	for (int k = 0; k < NumVerts; ++k)
	{
		PolyMeshGen.Polygon.AppendVertex(WorldFrameOut.ToPlaneUV(Polygon[k], 2));
	}

	// add preview vertex
	if (bIncludePreviewVtx)
	{
		if (FVector::Dist(PreviewVertex, Polygon[NumVerts - 1]) > 0.1)
		{
			PolyMeshGen.Polygon.AppendVertex(WorldFrameOut.ToPlaneUV(PreviewVertex, 2));
		}
	}

	if (PolyMeshGen.Polygon.IsClockwise() == false)
	{
		PolyMeshGen.Polygon.Reverse();
	}

	ResultMeshOut->Copy(&PolyMeshGen.Generate());

	// for symmetric extrude we translate the first poly by -dist along axis
	if (bExtrudeSymmetric)
	{
		FVector3d ShiftNormal = FVector3d::UnitZ();
		for (int vid : ResultMeshOut->VertexIndicesItr())
		{
			FVector3d Pos = ResultMeshOut->GetVertex(vid);
			ResultMeshOut->SetVertex(vid, Pos - ExtrudeDistance * ShiftNormal);
		}
		// double extrude dist
		ExtrudeDistance *= 2.0;
	}

	if (ExtrudeDistance != 0)
	{
		FExtrudeMesh Extruder(ResultMeshOut);
		Extruder.DefaultExtrudeDistance = ExtrudeDistance;
		FAxisAlignedBox2d bounds = PolyMeshGen.Polygon.Bounds();
		Extruder.UVScaleFactor = 1.0 / bounds.MaxDim();
		if (ExtrudeDistance < 0)
		{
			Extruder.IsPositiveOffset = false;
		}

		FVector3d ExtrudeNormal = FVector3d::UnitZ();
		Extruder.ExtrudedPositionFunc = [&ExtrudeDistance, &ExtrudeNormal](const FVector3d& Position, const FVector3f& Normal, int VertexID)
		{
			return Position + ExtrudeDistance * ExtrudeNormal;
		};

		Extruder.Apply();
	}

	FDynamicMeshEditor Editor(ResultMeshOut);
	float InitialUVScale = 1.0 / PolyMeshGen.Polygon.Bounds().MaxDim(); // this is the UV scale used by both the polymeshgen and the extruder above
	// default global rescale -- initial scale doesn't factor in extrude distance; rescale so UVScale of 1.0 fits in the unit square texture
	float GlobalUVRescale = MaterialProperties->UVScale / FMathf::Max(1.0f, ExtrudeDistance * InitialUVScale);
	if (MaterialProperties->bWorldSpaceUVScale)
	{
		// since we know the initial uv scale, directly compute the global scale (relative to 1 meter as a standard scale)
		GlobalUVRescale = MaterialProperties->UVScale * .01 / InitialUVScale;
	}
	Editor.RescaleAttributeUVs(GlobalUVRescale, false);
}

void UDrawPolygonTool::GeneratePreviewHeightTarget()
{
	GeneratePolygonMesh(PolygonVertices, &PreviewHeightTarget, PreviewHeightFrame, false, 99999, true);
	PreviewHeightTargetAABB.SetMesh(&PreviewHeightTarget);
}




void UDrawPolygonTool::ShowStartupMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartDraw", "Left-click to place points on the Drawing Plane. Hold Shift to ignore Snapping. Ctrl-click on the scene to reposition the Plane (Shift+Ctrl-click to only Translate). Backspace to discard last vertex. A key toggles Gizmo."),
		EToolMessageLevel::UserNotification);
}

void UDrawPolygonTool::ShowExtrudeMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartExtrude", "Set the height of the Extrusion by positioning the mouse over the extrusion volume, or over the scene to snap to relative heights."),
		EToolMessageLevel::UserNotification);
}


#undef LOCTEXT_NAMESPACE
