// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DrawPolygonTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "BaseBehaviors/KeyAsModifierInputBehavior.h"

#include "Polygon2.h"
#include "Curve/GeneralPolygon2.h"
#include "FrameTypes.h"
#include "MatrixTypes.h"
#include "DynamicMeshAttributeSet.h"

#include "MeshDescriptionBuilder.h"
#include "Generators/FlatTriangulationMeshGenerator.h"
#include "Generators/DiscMeshGenerator.h"
#include "Generators/RectangleMeshGenerator.h"
#include "Operations/ExtrudeMesh.h"
#include "Distance/DistLine3Ray3.h"
#include "Intersection/IntrSegment2Segment2.h"
#include "MeshQueries.h"
#include "ToolSceneQueriesUtil.h"
#include "ConstrainedDelaunay2.h"
#include "Arrangement2d.h"

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
constexpr int CurrentGridSnapID = FPointPlanarSnapSolver::BaseExternalPointID + 3;

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
	PropertyCache->bSnapToWorldGrid = this->bSnapToWorldGrid;
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
	this->bSnapToWorldGrid = PropertyCache->bSnapToWorldGrid;
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




void UDrawPolygonTool::Tick(float DeltaTime)
{
	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->bSnapToWorldGrid =
			SnapProperties->bEnableSnapping && SnapProperties->bSnapToWorldGrid && bIgnoreSnappingToggle == false;
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
	FVector3f A = Center - Height * 0.25f*X - Height * Y;
	FVector3f B = Center + Height * 0.25f*X + Height * Y;
	PDI->DrawLine((FVector)A, (FVector)B, Color, DepthPriorityGroup, LineThickness, 0.0f, bIsScreenSpace);
	A += Height * 0.5f*X;
	B += Height * 0.5f*X;
	PDI->DrawLine((FVector)A, (FVector)B, Color, DepthPriorityGroup, LineThickness, 0.0f, bIsScreenSpace);
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
		if (FixedPolygonClickPoints.Num() > 0 && bInInteractiveExtrude == false)		// once we are in extrude, polygon is done
		{
			FixedPolygonClickPoints.Add(PreviewVertex);
			GenerateFixedPolygon(FixedPolygonClickPoints, PolygonVertices, PolygonHolesVertices);
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
		else if (SnapEngine.GetActiveSnapTargetID() == CurrentGridSnapID)
		{
			DrawCircle(PDI, (FVector)LastGridSnapPoint, CameraState.Right(), CameraState.Up(),
				SnapHighlightColor, ElementSize, 4, SDPG_Foreground, 1.0f, 0.0f, true);
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

		auto DrawVertices = [&PDI, &UseColor](const TArray<FVector>& Vertices, ESceneDepthPriorityGroup Group, float Thickness)
		{
			for (int lasti = Vertices.Num() - 1, i = 0, NumVertices = Vertices.Num(); i < NumVertices; lasti = i++)
			{
				PDI->DrawLine(Vertices[lasti], Vertices[i], UseColor, Group, Thickness, 0.0f, true);
			}
		};

		// draw thin no-depth
		//DrawVertices(PolygonVertices, SDPG_Foreground, HiddenLineThickness);
		for (int i = 0; i < NumVerts - 1; ++i)
		{
			PDI->DrawLine(PolygonVertices[i], PolygonVertices[i + 1],
				UseColor, SDPG_Foreground, HiddenLineThickness, 0.0f, true);
		}
		PDI->DrawLine(PolygonVertices[NumVerts - 1], UseLastVertex,
			UseColor, SDPG_Foreground, HiddenLineThickness, 0.0f, true);
		for (int HoleIdx = 0; HoleIdx < PolygonHolesVertices.Num(); HoleIdx++)
		{
			DrawVertices(PolygonHolesVertices[HoleIdx], SDPG_Foreground, HiddenLineThickness);
		}

		// draw thick depth-tested
		//DrawVertices(PolygonVertices, SDPG_World, LineThickness);
		for (int i = 0; i < NumVerts - 1; ++i)
		{
			PDI->DrawLine(PolygonVertices[i], PolygonVertices[i + 1],
				UseColor, SDPG_World, LineThickness, 0.0f, true);
		}
		PDI->DrawLine(PolygonVertices[NumVerts - 1], UseLastVertex,
			UseColor, SDPG_World, LineThickness, 0.0f, true);
		for (int HoleIdx = 0; HoleIdx < PolygonHolesVertices.Num(); HoleIdx++)
		{
			DrawVertices(PolygonHolesVertices[HoleIdx], SDPG_World, LineThickness);
		}

		if (bHaveSelfIntersection)
		{
			PDI->DrawPoint((FVector)SelfIntersectionPoint, ErrorColor, 10, SDPG_Foreground);
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
			(FVector)HitPosFrameWorld.PointAt(-Length, -Length, 0), (FVector)HitPosFrameWorld.PointAt(Length, Length, 0),
			HitFrameColor, 1, Thickness, 0.0f, true);
		PDI->DrawLine(
			(FVector)HitPosFrameWorld.PointAt(-Length, Length, 0), (FVector)HitPosFrameWorld.PointAt(Length, -Length, 0),
			HitFrameColor, 1, Thickness, 0.0f, true);

		FVector PreviewOrigin = (FVector)PreviewHeightFrame.Origin;

		FVector DrawPlaneNormal = DrawPlaneOrientation.GetAxisZ();

		FColor AxisColor(128, 128, 0);
		PDI->DrawLine(
			PreviewOrigin -1000*DrawPlaneNormal, PreviewOrigin +1000*DrawPlaneNormal,
			AxisColor, 1, 1.0f, 0.0f, true);

		FColor HeightPosColor(128, 0, 128);
		PDI->DrawLine(
			PreviewOrigin + PolygonProperties->ExtrudeHeight*DrawPlaneNormal, (FVector)HitPosFrameWorld.Origin,
			HeightPosColor, 1, 1.0f, 0.0f, true);
	}


	ShowGizmoWatcher.CheckAndUpdate();
}


void UDrawPolygonTool::ResetPolygon()
{
	PolygonVertices.Reset();
	PolygonHolesVertices.Reset();
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
	if (bIgnoreSnappingToggle || SnapProperties->bEnableSnapping == false)
	{
		SnapEngine.ResetActiveSnap();
		SnapEngine.UpdatePointHistory(TArray<FVector>());
	}
	else 
	{
		if (SnapProperties->bSnapToWorldGrid)
		{
			FVector3d WorldGridSnapPos;
			if (ToolSceneQueriesUtil::FindWorldGridSnapPoint(this, HitPos, WorldGridSnapPos))
			{
				WorldGridSnapPos = Frame.ToPlane(WorldGridSnapPos, 2);
				SnapEngine.AddPointTarget(WorldGridSnapPos, CurrentGridSnapID, 
					FBasePositionSnapSolver3::FCustomMetric::Replace(999), SnapEngine.MinInternalPriority() - 5);
				LastGridSnapPoint = WorldGridSnapPos;
			}
		}

		if (SnapProperties->bSnapToVertices || SnapProperties->bSnapToEdges)
		{
			FVector3d SceneSnapPos;
			if (ToolSceneQueriesUtil::FindSceneSnapPoint(this, HitPos, SceneSnapPos, SnapProperties->bSnapToVertices, SnapProperties->bSnapToEdges, 0, &LastSnapGeometry))
			{
				SnapEngine.AddPointTarget(SceneSnapPos, CurrentSceneSnapID, SnapEngine.MinInternalPriority() - 10);
			}
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
	SnapEngine.RemovePointTargetsByID(CurrentGridSnapID);

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
		int NumTargetPoints = (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Rectangle || PolygonProperties->PolygonType == EDrawPolygonDrawMode::RoundedRectangle) ? 3 : 2;
		bDonePolygon = (FixedPolygonClickPoints.Num() == NumTargetPoints);
		if (bDonePolygon)
		{
			GenerateFixedPolygon(FixedPolygonClickPoints, PolygonVertices, PolygonHolesVertices);
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
			PolygonVertices[0] = PreviewVertex = (FVector)SelfIntersectionPoint;
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

	float BestIntersectionParameter = FMathf::MaxReal;
	for (int k = 0; k < NumVertices - 2; ++k) 
	{
		FSegment2f Segment(DrawFrame.ToPlaneUV(PolygonVertices[k],2), DrawFrame.ToPlaneUV(PolygonVertices[k + 1],2));
		FIntrSegment2Segment2f Intersection(PreviewSegment, Segment);
		if (Intersection.Find()) 
		{
			bHaveSelfIntersection = true;
			if (Intersection.Parameter0 < BestIntersectionParameter)
			{
				BestIntersectionParameter = Intersection.Parameter0;
				SelfIntersectSegmentIdx = k;
				SelfIntersectionPoint = DrawFrame.FromPlaneUV(Intersection.Point0, 2);
			}
		}
	}
	return bHaveSelfIntersection;
}

void UDrawPolygonTool::GetPolygonParametersFromFixedPoints(const TArray<FVector>& FixedPoints, FVector2f& FirstReferencePt, FVector2f& BoxSize, float& YSign, float& AngleRad)
{
	if (FixedPoints.Num() < 2)
	{
		return;
	}

	FFrame3f DrawFrame(DrawPlaneOrigin, DrawPlaneOrientation);
	FirstReferencePt = DrawFrame.ToPlaneUV(FixedPoints[0], 2);

	FVector2f EdgePt = DrawFrame.ToPlaneUV(FixedPoints[1], 2);
	FVector2f Delta = EdgePt - FirstReferencePt;
	AngleRad = FMath::Atan2(Delta.Y, Delta.X);

	float Radius = Delta.Length();
	FVector2f AxisX = Delta / Radius;
	FVector2f AxisY = -AxisX.Perp();
	FVector2f HeightPt = DrawFrame.ToPlaneUV((FixedPoints.Num() == 3) ? FixedPoints[2] : FixedPoints[1], 2);
	FVector2f HeightDelta = HeightPt - FirstReferencePt;
	YSign = FMath::Sign(HeightDelta.Dot(AxisY));
	BoxSize.X = Radius;
	BoxSize.Y = FMath::Abs(HeightDelta.Dot(AxisY));
}

void UDrawPolygonTool::GenerateFixedPolygon(const TArray<FVector>& FixedPoints, TArray<FVector>& VerticesOut, TArray<TArray<FVector>>& HolesVerticesOut)
{
	FVector2f FirstReferencePt, BoxSize;
	float YSign, AngleRad;
	GetPolygonParametersFromFixedPoints(FixedPoints, FirstReferencePt, BoxSize, YSign, AngleRad);
	float Width = BoxSize.X, Height = BoxSize.Y;
	FMatrix2f RotationMat = FMatrix2f::RotationRad(AngleRad);

	FPolygon2f Polygon;
	TArray<FPolygon2f> PolygonHoles;
	if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Square)
	{
		Polygon = FPolygon2f::MakeRectangle(FVector2f::Zero(), 2*Width, 2*Width);
	}
	else if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Rectangle || PolygonProperties->PolygonType == EDrawPolygonDrawMode::RoundedRectangle)
	{
		if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Rectangle)
		{
			Polygon = FPolygon2f::MakeRectangle(FVector2f(Width / 2, YSign*Height / 2), Width, Height);
		}
		else // PolygonProperties->PolygonType == EDrawPolygonDrawMode::RoundedRectangle
		{
			Polygon = FPolygon2f::MakeRoundedRectangle(FVector2f(Width / 2, YSign*Height / 2), Width, Height, FMath::Min(Width,Height) * FMathf::Clamp(PolygonProperties->FeatureSizeRatio, .01, .99) * .5, PolygonProperties->Steps);
		}
	}
	else // Circle or HoleyCircle
	{
		Polygon = FPolygon2f::MakeCircle(Width, PolygonProperties->Steps, 0);
		if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::HoleyCircle)
		{
			PolygonHoles.Add(FPolygon2f::MakeCircle(Width * FMathd::Clamp(PolygonProperties->FeatureSizeRatio, .01, .99), PolygonProperties->Steps, 0));
		}
	}
	Polygon.Transform([RotationMat](const FVector2f& Pt) { return RotationMat * Pt; });
	for (FPolygon2f& Hole : PolygonHoles)
	{
		Hole.Transform([RotationMat](const FVector2f& Pt) { return RotationMat * Pt; });
	}

	FFrame3f DrawFrame(DrawPlaneOrigin, DrawPlaneOrientation);
	VerticesOut.SetNum(Polygon.VertexCount());
	for (int k = 0; k < Polygon.VertexCount(); ++k)
	{
		FVector2f NewPt = FirstReferencePt + Polygon[k];
		VerticesOut[k] = (FVector)DrawFrame.FromPlaneUV(NewPt, 2);
	}

	HolesVerticesOut.SetNum(PolygonHoles.Num());
	for (int HoleIdx = 0; HoleIdx < PolygonHoles.Num(); HoleIdx++)
	{
		int NumHoleVerts = PolygonHoles[HoleIdx].VertexCount();
		HolesVerticesOut[HoleIdx].SetNum(NumHoleVerts);
		for (int k = 0; k < NumHoleVerts; ++k)
		{
			FVector2f NewPt = FirstReferencePt + PolygonHoles[HoleIdx][k];
			HolesVerticesOut[HoleIdx][k] = (FVector)DrawFrame.FromPlaneUV(NewPt, 2);
		}
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
		NearestHitDist = ClickPos.WorldRay.GetParameter((FVector)NearestHitFrameWorld.Origin);
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
		if ( bIgnoreSnappingToggle == false && SnapProperties->bEnableSnapping && SnapProperties->bSnapToWorldGrid )
		{
			FVector3d GridPosWorld;
			if (ToolSceneQueriesUtil::FindWorldGridSnapPoint(this, (FVector3d)NearestHitFrameWorld.Origin, GridPosWorld))
			{
				NearestHitFrameWorld.Origin = (FVector3f)GridPosWorld;
				FVector3d LocalPos = PreviewHeightFrame.ToFramePoint((FVector3d)NearestHitFrameWorld.Origin);
				NearestHitHeight = (float)LocalPos.Z;
			}
		}

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
		DrawPlaneOrientation = (FQuat)DrawPlane.Rotation;
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
	bool bSucceeded = GeneratePolygonMesh(PolygonVertices, PolygonHolesVertices, &Mesh, PlaneFrameOut, false, ExtrudeDist, false);
	if (!bSucceeded) // somehow made a polygon with no valid triangulation; just throw it away ...
	{
		ResetPolygon();
		return;
	}

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
	if (NumVerts < 2 || PreviewMesh == nullptr || PreviewMesh->IsVisible() == false)
	{
		return;
	}

	FFrame3d PlaneFrame;
	FDynamicMesh3 Mesh;
	double ExtrudeDist = (PolygonProperties->OutputMode == EDrawPolygonOutputMode::MeshedPolygon) ?
		0 : PolygonProperties->ExtrudeHeight;
	if (GeneratePolygonMesh(PolygonVertices, PolygonHolesVertices, &Mesh, PlaneFrame, false, ExtrudeDist, false))
	{
		PreviewMesh->SetTransform(PlaneFrame.ToFTransform());
		PreviewMesh->SetMaterial(MaterialProperties->Material);
		PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
		PreviewMesh->UpdatePreview(&Mesh);
	}
}

bool UDrawPolygonTool::GeneratePolygonMesh(const TArray<FVector>& Polygon, const TArray<TArray<FVector>>& PolygonHoles, FDynamicMesh3* ResultMeshOut, FFrame3d& WorldFrameOut, bool bIncludePreviewVtx, double ExtrudeDistance, bool bExtrudeSymmetric)
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

	// Compute outer polygon & bounds
	auto VertexArrayToPolygon = [&WorldFrameOut](const TArray<FVector>& Vertices)
	{
		FPolygon2d OutPolygon;
		for (int k = 0, N = Vertices.Num(); k < N; ++k)
		{
			OutPolygon.AppendVertex(WorldFrameOut.ToPlaneUV(Vertices[k], 2));
		}
		return OutPolygon;
	};
	FPolygon2d OuterPolygon = VertexArrayToPolygon(Polygon);
	// add preview vertex
	if (bIncludePreviewVtx)
	{
		if (FVector::Dist(PreviewVertex, Polygon[NumVerts - 1]) > 0.1)
		{
			OuterPolygon.AppendVertex(WorldFrameOut.ToPlaneUV(PreviewVertex, 2));
		}
	}
	FAxisAlignedBox2d Bounds(OuterPolygon.Bounds());

	// special case paths
	if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::HoleyCircle || PolygonProperties->PolygonType == EDrawPolygonDrawMode::Circle || PolygonProperties->PolygonType == EDrawPolygonDrawMode::RoundedRectangle)
	{
		// get polygon parameters
		FVector2f FirstReferencePt, BoxSize;
		float YSign, AngleRad;
		GetPolygonParametersFromFixedPoints(FixedPolygonClickPoints, FirstReferencePt, BoxSize, YSign, AngleRad);
		FirstReferencePt -= FVector2f(Centroid.X, Centroid.Y);
		FMatrix2f RotationMat = FMatrix2f::RotationRad(AngleRad);

		// translate general polygon parameters to specific mesh generator parameters, and generate mesh
		if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::HoleyCircle)
		{
			FPuncturedDiscMeshGenerator HCGen;
			HCGen.AngleSamples = PolygonProperties->Steps;
			HCGen.RadialSamples = 1;
			HCGen.Radius = BoxSize.X;
			HCGen.HoleRadius = BoxSize.X * FMath::Clamp(PolygonProperties->FeatureSizeRatio, .01f, .99f);
			ResultMeshOut->Copy(&HCGen.Generate());
		}
		else if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::Circle)
		{
			FDiscMeshGenerator CGen;
			CGen.AngleSamples = PolygonProperties->Steps;
			CGen.RadialSamples = 1;
			CGen.Radius = BoxSize.X;
			ResultMeshOut->Copy(&CGen.Generate());
		}
		else if (PolygonProperties->PolygonType == EDrawPolygonDrawMode::RoundedRectangle)
		{
			FRoundedRectangleMeshGenerator RRGen;
			FirstReferencePt += RotationMat * (FVector2f(BoxSize.X, BoxSize.Y * YSign)*.5f);
			RRGen.AngleSamples = PolygonProperties->Steps;
			RRGen.Radius = .5 * FMath::Min(BoxSize.X, BoxSize.Y) * FMath::Clamp(PolygonProperties->FeatureSizeRatio, .01f, .99f);
			RRGen.Height = BoxSize.Y - RRGen.Radius * 2.;
			RRGen.Width = BoxSize.X - RRGen.Radius * 2.;
			RRGen.WidthVertexCount = 1;
			RRGen.HeightVertexCount = 1;
			ResultMeshOut->Copy(&RRGen.Generate());
		}

		// transform generated mesh
		for (int VertIdx : ResultMeshOut->VertexIndicesItr())
		{
			FVector3d V = ResultMeshOut->GetVertex(VertIdx);
			FVector2f VTransformed = RotationMat * FVector2f(V.X, V.Y) + FirstReferencePt;
			ResultMeshOut->SetVertex(VertIdx, FVector3d(VTransformed.X, VTransformed.Y, 0));
		}
	}
	else // generic path: triangulate using polygon vertices
	{
		// triangulate polygon into the MeshDescription
		FGeneralPolygon2d GeneralPolygon;
		FFlatTriangulationMeshGenerator TriangulationMeshGen;

		if (OuterPolygon.IsClockwise() == false)
		{
			OuterPolygon.Reverse();
		}

		GeneralPolygon.SetOuter(OuterPolygon);

		for (int HoleIdx = 0; HoleIdx < PolygonHoles.Num(); HoleIdx++)
		{
			// attempt to add holes (skipping if safety checks fail)
			GeneralPolygon.AddHole(VertexArrayToPolygon(PolygonHoles[HoleIdx]), true, false /*currently don't care about hole orientation; we'll just set the triangulation algo not to care*/);
		}

		FConstrainedDelaunay2d Triangulator;
		if (PolygonProperties->bAllowSelfIntersections)
		{
			FArrangement2d Arrangement(OuterPolygon.Bounds());
			// arrangement2d builds a general 2d graph that discards orientation info ...
			Triangulator.FillRule = FConstrainedDelaunay2d::EFillRule::Odd;
			Triangulator.bOrientedEdges = false;
			Triangulator.bSplitBowties = true;
			for (FSegment2d Seg : GeneralPolygon.GetOuter().Segments())
			{
				Arrangement.Insert(Seg);
			}
			Triangulator.Add(Arrangement.Graph);
			for (const FPolygon2d& Hole : GeneralPolygon.GetHoles())
			{
				Triangulator.Add(Hole, true);
			}
		}
		else
		{
			Triangulator.Add(GeneralPolygon);
		}


		bool bTriangulationSuccess = Triangulator.Triangulate();
		// only truly fail if we got zero triangles back from the triangulator; if it just returned false it may still have managed to partially generate something
		if (Triangulator.Triangles.Num() == 0)
		{
			return false;
		}

		TriangulationMeshGen.Vertices2D = Triangulator.Vertices;
		TriangulationMeshGen.Triangles2D = Triangulator.Triangles;

		ResultMeshOut->Copy(&TriangulationMeshGen.Generate());
	}

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
		
		Extruder.UVScaleFactor = 1.0 / Bounds.MaxDim();
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
	float InitialUVScale = 1.0 / Bounds.MaxDim(); // this is the UV scale used by both the polymeshgen and the extruder above
	// default global rescale -- initial scale doesn't factor in extrude distance; rescale so UVScale of 1.0 fits in the unit square texture
	float GlobalUVRescale = MaterialProperties->UVScale / FMathf::Max(1.0f, ExtrudeDistance * InitialUVScale);
	if (MaterialProperties->bWorldSpaceUVScale)
	{
		// since we know the initial uv scale, directly compute the global scale (relative to 1 meter as a standard scale)
		GlobalUVRescale = MaterialProperties->UVScale * .01 / InitialUVScale;
	}
	Editor.RescaleAttributeUVs(GlobalUVRescale, false);

	return true;
}

void UDrawPolygonTool::GeneratePreviewHeightTarget()
{
	if (GeneratePolygonMesh(PolygonVertices, PolygonHolesVertices, &PreviewHeightTarget, PreviewHeightFrame, false, 99999, true))
	{
		PreviewHeightTargetAABB.SetMesh(&PreviewHeightTarget);
	}
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
