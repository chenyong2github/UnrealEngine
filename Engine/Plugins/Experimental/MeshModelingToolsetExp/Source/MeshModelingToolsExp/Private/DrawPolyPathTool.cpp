// Copyright Epic Games, Inc. All Rights Reserved.

#include "DrawPolyPathTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"

#include "ToolSceneQueriesUtil.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh/MeshIndexUtil.h"
#include "Generators/RectangleMeshGenerator.h"
#include "Generators/PolygonEdgeMeshGenerator.h"
#include "Distance/DistLine3Line3.h"
#include "ModelingObjectsCreationAPI.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Selection/ToolSelectionUtil.h"
#include "Operations/ExtrudeMesh.h"
#include "DynamicMesh/MeshNormals.h"
#include "MeshBoundaryLoops.h"
#include "ToolDataVisualizer.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UDrawPolyPathTool"


namespace DrawPolyPathToolLocals
{
	void ComputeArcLengths(const TArray<FFrame3d>& PathPoints, TArray<double>& ArcLengths)
	{
		double CurPathLength = 0;
		ArcLengths.SetNum(PathPoints.Num());
		ArcLengths[0] = 0.0f;
		for (int32 k = 1; k < PathPoints.Num(); ++k)
		{
			CurPathLength += Distance(PathPoints[k].Origin, PathPoints[k - 1].Origin);
			ArcLengths[k] = CurPathLength;
		}
	}


	void GeneratePathMesh(FDynamicMesh3& Mesh, const TArray<FFrame3d>& InPathPoints, const TArray<double>& InOffsetScaleFactors, double OffsetDistance, bool bPathIsClosed, bool bRampMode, bool bSinglePolyGroup)
	{
		Mesh.Clear();

		TArray<FFrame3d> UsePathPoints = InPathPoints;
		TArray<double> UseOffsetScaleFactors = InOffsetScaleFactors;

		if (bPathIsClosed && bRampMode)
		{
			// Duplicate vertices at the beginning/end of the path when generating a ramp
			const FFrame3d FirstPoint = InPathPoints[0];
			UsePathPoints.Add(FirstPoint);
			const double FirstScaleFactor = InOffsetScaleFactors[0];
			UseOffsetScaleFactors.Add(FirstScaleFactor);
		}

		const int NumPoints = UsePathPoints.Num();

		TArray<double> ArcLengths;
		ComputeArcLengths(UsePathPoints, ArcLengths);
		const double PathLength = ArcLengths.Last();

		if (bPathIsClosed)
		{
			FPolygonEdgeMeshGenerator MeshGen(UsePathPoints, UseOffsetScaleFactors, OffsetDistance, FVector3d::UnitZ());
			MeshGen.bSinglePolyGroup = bSinglePolyGroup;
			MeshGen.UVWidth = PathLength;
			MeshGen.UVHeight = 2 * OffsetDistance;
			MeshGen.Generate();
			Mesh.Copy(&MeshGen);

			Mesh.EnableVertexUVs(FVector2f::Zero());
			for (int k = 0; k < NumPoints; ++k)
			{
				// Temporarily set vertex UVs to arclengths, for use in interpolating height in ramp mode
				const float Alpha = (float)ArcLengths[k] / PathLength;
				Mesh.SetVertexUV(2 * k, FVector2f(Alpha, (float)k));
				Mesh.SetVertexUV(2 * k + 1, FVector2f(Alpha, (float)k));
			}

			if (bRampMode)
			{
				int NumMeshVertices = 2 * NumPoints;
				ensure(NumMeshVertices == Mesh.VertexCount());
				ensure(NumMeshVertices == Mesh.MaxVertexID());
				Mesh.SetVertex(NumMeshVertices - 2, Mesh.GetVertex(0));
				Mesh.SetVertex(NumMeshVertices - 1, Mesh.GetVertex(1));
			}
		}
		else
		{
			FRectangleMeshGenerator MeshGen;
			MeshGen.bSinglePolyGroup = bSinglePolyGroup;
			MeshGen.Width = PathLength;
			MeshGen.Height = 2 * OffsetDistance;
			MeshGen.Normal = FVector3f::UnitZ();
			MeshGen.Origin = FVector3d(PathLength / 2, 0, 0);
			MeshGen.HeightVertexCount = 2;
			MeshGen.WidthVertexCount = NumPoints;
			MeshGen.Generate();
			Mesh.Copy(&MeshGen);
			Mesh.EnableVertexUVs(FVector2f::Zero());		// we will store arc length for each vtx in VertexUV

			double ShiftX = 0;
			double DeltaX = PathLength / (double)(NumPoints - 1);
			for (int32 k = 0; k < NumPoints; ++k)
			{
				FFrame3d PathFrame = UsePathPoints[k];
				FVector3d V0 = Mesh.GetVertex(k);
				V0.X -= ShiftX;
				V0.Y *= UseOffsetScaleFactors[k];
				V0 = PathFrame.FromFramePoint(V0);
				Mesh.SetVertex(k, V0);
				const float Alpha = (float)ArcLengths[k] / PathLength;
				Mesh.SetVertexUV(k, FVector2f(Alpha, (float)k));
				FVector3d V1 = Mesh.GetVertex(NumPoints + k);
				V1.X -= ShiftX;
				V1.Y *= UseOffsetScaleFactors[k];
				V1 = PathFrame.FromFramePoint(V1);
				Mesh.SetVertex(NumPoints + k, V1);
				Mesh.SetVertexUV(NumPoints + k, FVector2f(Alpha, (float)k));
				ShiftX += DeltaX;
			}
		}
	}

}	// namespace DrawPolyPathToolLocals


/*
 * ToolBuilder
 */
bool UDrawPolyPathToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UDrawPolyPathToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UDrawPolyPathTool* NewTool = NewObject<UDrawPolyPathTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	return NewTool;
}

/*
* Tool methods
*/
void UDrawPolyPathTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UDrawPolyPathTool::Setup()
{
	UInteractiveTool::Setup();

	// register click behavior
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>(this);
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	DrawPlaneWorld = FFrame3d();

	PlaneMechanic = NewObject<UConstructionPlaneMechanic>(this);
	PlaneMechanic->Setup(this);
	PlaneMechanic->CanUpdatePlaneFunc = [this]() { return CanUpdateDrawPlane(); };
	PlaneMechanic->Initialize(TargetWorld, DrawPlaneWorld);
	PlaneMechanic->UpdateClickPriority(ClickBehavior->GetPriority().MakeHigher());
	PlaneMechanic->OnPlaneChanged.AddLambda([this]() {
		DrawPlaneWorld = PlaneMechanic->Plane;
		UpdateSurfacePathPlane();
	});

	OutputTypeProperties = NewObject<UCreateMeshObjectTypeProperties>(this);
	OutputTypeProperties->RestoreProperties(this);
	OutputTypeProperties->InitializeDefault();
	OutputTypeProperties->WatchProperty(OutputTypeProperties->OutputType, [this](FString) { OutputTypeProperties->UpdatePropertyVisibility(); });
	AddToolPropertySource(OutputTypeProperties);

	// add properties
	TransformProps = NewObject<UDrawPolyPathProperties>(this);
	TransformProps->RestoreProperties(this);
	AddToolPropertySource(TransformProps);
	TransformProps->WatchProperty(TransformProps->bSnapToWorldGrid, [this](bool) 
	{
		if (SurfacePathMechanic != nullptr)
		{
			SurfacePathMechanic->bSnapToWorldGrid = TransformProps->bSnapToWorldGrid;
		}
	});

	TransformProps->WatchProperty(TransformProps->ExtrudeMode, [this](EDrawPolyPathExtrudeMode)
	{
		if(ExtrudeHeightMechanic != nullptr)
		{
			// regenerate the base path mesh
			BeginInteractiveExtrudeHeight();
		}
	});


	ExtrudeProperties = NewObject<UDrawPolyPathExtrudeProperties>();
	ExtrudeProperties->RestoreProperties(this);
	AddToolPropertySource(ExtrudeProperties);
	SetToolPropertySourceEnabled(ExtrudeProperties, false);

	// initialize material properties for new objects
	MaterialProperties = NewObject<UNewMeshMaterialProperties>(this);
	MaterialProperties->RestoreProperties(this);
	MaterialProperties->bShowExtendedOptions = false;
	AddToolPropertySource(MaterialProperties);

	// begin path draw
	InitializeNewSurfacePath();

	SetToolDisplayName(LOCTEXT("ToolName", "Path Extrude"));
}


void UDrawPolyPathTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (bHasSavedWidth)
	{
		TransformProps->Width = SavedWidth;
		bHasSavedWidth = false;
	}

	if (bHasSavedExtrudeHeight)
	{
		TransformProps->ExtrudeHeight = SavedExtrudeHeight;
		SavedExtrudeHeight = false;
	}

	PlaneMechanic->Shutdown();
	PlaneMechanic = nullptr;

	OutputTypeProperties->SaveProperties(this);
	TransformProps->SaveProperties(this);
	ExtrudeProperties->SaveProperties(this);
	MaterialProperties->SaveProperties(this);

	ClearPreview();
}


bool UDrawPolyPathTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	if (SurfacePathMechanic != nullptr)
	{
		FFrame3d HitPoint;
		if (SurfacePathMechanic->IsHitByRay(FRay3d(Ray), HitPoint))
		{
			OutHit.Distance = FRay3d(Ray).GetParameter(HitPoint.Origin);
			OutHit.ImpactPoint = (FVector)HitPoint.Origin;
			OutHit.ImpactNormal = (FVector)HitPoint.Z();
			return true;
		}
		return false;
	}
	else if (CurveDistMechanic != nullptr)
	{
		OutHit.ImpactPoint = Ray.PointAt(100);
		OutHit.Distance = 100;
		return true;
	}
	else if (ExtrudeHeightMechanic != nullptr)
	{
		OutHit.ImpactPoint = Ray.PointAt(100);
		OutHit.Distance = 100;
		return true;
	}

	return false;
}



FInputRayHit UDrawPolyPathTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FHitResult OutHit;
	if (HitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	// background capture, if nothing else is hit
	return FInputRayHit(TNumericLimits<float>::Max());
}


void UDrawPolyPathTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (SurfacePathMechanic != nullptr)
	{
		if (SurfacePathMechanic->TryAddPointFromRay((FRay3d)ClickPos.WorldRay))
		{
			if (SurfacePathMechanic->IsDone())
			{
				bPathIsClosed = SurfacePathMechanic->LoopWasClosed();
				GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginOffset", "Set path width"));
				OnCompleteSurfacePath();
			}
			else
			{
				GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginPath", "Begin path"));
			}
		}
	}
	else if (CurveDistMechanic != nullptr)
	{
		GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginHeight", "Set extrude height"));
		OnCompleteOffsetDistance();
	}
	else if (ExtrudeHeightMechanic != nullptr)
	{
		CurHeight = TransformProps->ExtrudeHeight;
		OnCompleteExtrudeHeight();
	}
}




FInputRayHit UDrawPolyPathTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	// background capture, if nothing else is hit
	return FInputRayHit(TNumericLimits<float>::Max());
}


bool UDrawPolyPathTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (SurfacePathMechanic != nullptr)
	{
		SurfacePathMechanic->UpdatePreviewPoint((FRay3d)DevicePos.WorldRay);
	} 
	else if (CurveDistMechanic != nullptr)
	{
		CurveDistMechanic->UpdateCurrentDistance(DevicePos.WorldRay);

		if (TransformProps->bSnapToWorldGrid)
		{
			double QuantizedDistance = ToolSceneQueriesUtil::SnapDistanceToWorldGridSize(this, CurveDistMechanic->CurrentDistance);
			TransformProps->Width = QuantizedDistance * 2.0;
			CurOffsetDistance = QuantizedDistance;
		}
		else
		{
			TransformProps->Width = CurveDistMechanic->CurrentDistance * 2.0;
			CurOffsetDistance = CurveDistMechanic->CurrentDistance;
		}
		UpdatePathPreview();
	}
	else if (ExtrudeHeightMechanic != nullptr)
	{
		ExtrudeHeightMechanic->UpdateCurrentDistance(DevicePos.WorldRay);
		CurHeight = ExtrudeHeightMechanic->CurrentHeight;
		TransformProps->ExtrudeHeight = ExtrudeHeightMechanic->CurrentHeight;
		UpdateExtrudePreview();
	}
	return true;
}






void UDrawPolyPathTool::OnTick(float DeltaTime)
{
	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->Tick(DeltaTime);
	}
}


void UDrawPolyPathTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->Render(RenderAPI);
	}

	if (ExtrudeHeightMechanic != nullptr)
	{
		ExtrudeHeightMechanic->Render(RenderAPI);
	}
	if (CurveDistMechanic != nullptr)
	{
		CurveDistMechanic->Render(RenderAPI);
	}
	if (SurfacePathMechanic != nullptr)
	{
		SurfacePathMechanic->Render(RenderAPI);
	}

	if (CurPolyLoop.Num() > 0)
	{
		FToolDataVisualizer LineRenderer;
		LineRenderer.LineColor = LinearColors::DarkOrange3f();
		LineRenderer.LineThickness = 4.0f;
		LineRenderer.bDepthTested = false;

		LineRenderer.BeginFrame(RenderAPI);

		int32 NumPoints = CurPolyLoop.Num();
		for (int32 k = 0; k < NumPoints; ++k)
		{
			LineRenderer.DrawLine( CurPolyLoop[k], CurPolyLoop[ (k+1) % NumPoints ] );
		}
		if (SecondPolyLoop.Num() > 0)
		{
			NumPoints = SecondPolyLoop.Num();
			for (int32 k = 0; k < NumPoints; ++k)
			{
				LineRenderer.DrawLine( SecondPolyLoop[k], SecondPolyLoop[ (k+1) % NumPoints ] );
			}
		}

		LineRenderer.EndFrame();
	}

}




void UDrawPolyPathTool::InitializeNewSurfacePath()
{
	SurfacePathMechanic = NewObject<UCollectSurfacePathMechanic>(this);
	SurfacePathMechanic->Setup(this);
	double SnapTol = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD();
	SurfacePathMechanic->SpatialSnapPointsFunc = [this, SnapTol](FVector3d Position1, FVector3d Position2)
	{
		return ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, Position1, Position2, SnapTol);
	};
	SurfacePathMechanic->SetDoubleClickOrCloseLoopMode();

	if (TransformProps)
	{
		SurfacePathMechanic->bSnapToWorldGrid = TransformProps->bSnapToWorldGrid;
	}

	UpdateSurfacePathPlane();

	ShowStartupMessage();
}


bool UDrawPolyPathTool::CanUpdateDrawPlane() const
{
 	return (SurfacePathMechanic != nullptr && SurfacePathMechanic->HitPath.Num() == 0);
}

void UDrawPolyPathTool::UpdateSurfacePathPlane()
{
	if (SurfacePathMechanic != nullptr)
	{
		SurfacePathMechanic->InitializePlaneSurface(DrawPlaneWorld);
	}
}


void UDrawPolyPathTool::OnCompleteSurfacePath()
{
	check(SurfacePathMechanic != nullptr);

	CurPathPoints = SurfacePathMechanic->HitPath;
	int NumPoints = CurPathPoints.Num();
	// align frames
	FVector3d PlaneNormal = DrawPlaneWorld.Z();
	CurPathPoints[0].ConstrainedAlignAxis(0, UE::Geometry::Normalized(CurPathPoints[1].Origin - CurPathPoints[0].Origin), PlaneNormal);
	CurPathPoints[NumPoints-1].ConstrainedAlignAxis(0, UE::Geometry::Normalized(CurPathPoints[NumPoints-1].Origin - CurPathPoints[NumPoints-2].Origin), PlaneNormal);
	double DistOffsetDelta = 0.01;
	OffsetScaleFactors.SetNum(NumPoints);
	OffsetScaleFactors[0] = OffsetScaleFactors[NumPoints-1] = 1.0;

	// Set local frames for path points. If the path is closed, we will adjust the first and last frames for continuity,
	// otherwise we will leave them as set above.
	int LastPointIndex = bPathIsClosed ? NumPoints : NumPoints - 1;
	int FirstPointIndex = bPathIsClosed ? 0 : 1;
	for (int j = FirstPointIndex; j < LastPointIndex; ++j)
	{
		int NextJ = (j + 1) % NumPoints;
		int PrevJ = (j - 1 + NumPoints) % NumPoints;
		FVector3d Prev(CurPathPoints[PrevJ].Origin), Next(CurPathPoints[NextJ].Origin), Cur(CurPathPoints[j].Origin);
		FLine3d Line1(FLine3d::FromPoints(Prev, Cur)), Line2(FLine3d::FromPoints(Cur, Next));
		Line1.Origin += DistOffsetDelta * PlaneNormal.Cross(Line1.Direction);
		Line2.Origin += DistOffsetDelta * PlaneNormal.Cross(Line2.Direction);

		if (Line1.Direction.Dot(Line2.Direction) > 0.999 )
		{
			CurPathPoints[j].ConstrainedAlignAxis(0, UE::Geometry::Normalized(Next-Prev), PlaneNormal);
			OffsetScaleFactors[j] = 1.0;
		}
		else
		{
			FDistLine3Line3d LineDist(Line1, Line2);
			LineDist.GetSquared();
			FVector3d OffsetPoint = 0.5 * (LineDist.Line1ClosestPoint + LineDist.Line2ClosestPoint);
			OffsetScaleFactors[j] = Distance(OffsetPoint, Cur) / DistOffsetDelta;
			FVector3d TangentDir = UE::Geometry::Normalized(OffsetPoint - Cur).Cross(PlaneNormal);
			CurPathPoints[j].ConstrainedAlignAxis(0, TangentDir, PlaneNormal);
		}
	}

	CurPolyLine.Reset();
	for (const FFrame3d& Point : SurfacePathMechanic->HitPath)
	{
		CurPolyLine.Add(Point.Origin);
	}

	SurfacePathMechanic = nullptr;
	if (TransformProps->WidthMode == EDrawPolyPathWidthMode::Fixed)
	{
		BeginConstantOffsetDistance();
	}
	else
	{
		BeginInteractiveOffsetDistance();
	}
}


void UDrawPolyPathTool::BeginInteractiveOffsetDistance()
{
	bHasSavedWidth = true;
	SavedWidth = TransformProps->Width;
	
	// begin setting offset distance
	CurveDistMechanic = NewObject<USpatialCurveDistanceMechanic>(this);
	CurveDistMechanic->Setup(this);
	CurveDistMechanic->InitializePolyCurve(CurPolyLine, UE::Geometry::FTransform3d::Identity());

	InitializePreviewMesh();

	ShowOffsetMessage();
}


void UDrawPolyPathTool::BeginConstantOffsetDistance()
{
	InitializePreviewMesh();
	CurOffsetDistance = TransformProps->Width * 0.5;
	UpdatePathPreview();
	OnCompleteOffsetDistance();
}



void UDrawPolyPathTool::OnCompleteOffsetDistance()
{
	CurveDistMechanic = nullptr;

	if (TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::Flat)
	{
		CurHeight = 0.0;
		OnCompleteExtrudeHeight();
	}
	else if (TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::Fixed || TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampFixed)
	{
		CurHeight = TransformProps->ExtrudeHeight;
		OnCompleteExtrudeHeight();
	}
	else
	{
		BeginInteractiveExtrudeHeight();
	}
}


void UDrawPolyPathTool::OnCompleteExtrudeHeight()
{
	ExtrudeHeightMechanic = nullptr;

	ClearPreview();

	EmitNewObject();

	InitializeNewSurfacePath();
	CurrentCurveTimestamp++;
}



void UDrawPolyPathTool::UpdatePathPreview()
{
	check(EditPreview != nullptr);

	FDynamicMesh3 PathMesh;
	GeneratePathMesh(PathMesh);
	EditPreview->ReplaceMesh( MoveTempIfPossible(PathMesh) );
}



void UDrawPolyPathTool::GeneratePathMesh(FDynamicMesh3& Mesh)
{
	CurPolyLoop.Reset();
	SecondPolyLoop.Reset();

	const bool bRampMode = (TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampFixed) || (TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampInteractive);
	DrawPolyPathToolLocals::GeneratePathMesh(Mesh, CurPathPoints, OffsetScaleFactors, CurOffsetDistance, bPathIsClosed, bRampMode, TransformProps->bSinglePolyGroup);

	FMeshNormals::QuickRecomputeOverlayNormals(Mesh);

	FMeshBoundaryLoops Loops(&Mesh, true);
	if (Loops.Loops.Num() > 0)
	{
		Loops.Loops[0].GetVertices<FVector3d>(CurPolyLoop);
		if (Loops.Loops.Num() > 1)
		{
			Loops.Loops[1].GetVertices<FVector3d>(SecondPolyLoop);
		}
	}
}



void UDrawPolyPathTool::BeginInteractiveExtrudeHeight()
{
	bHasSavedExtrudeHeight = true;
	SavedExtrudeHeight = TransformProps->ExtrudeHeight;

	// begin extrude
	ExtrudeHeightMechanic = NewObject<UPlaneDistanceFromHitMechanic>(this);
	ExtrudeHeightMechanic->Setup(this);

	ExtrudeHeightMechanic->WorldHitQueryFunc = [this](const FRay& WorldRay, FHitResult& HitResult)
	{
		return ToolSceneQueriesUtil::FindNearestVisibleObjectHit(TargetWorld, HitResult, WorldRay);
	};
	ExtrudeHeightMechanic->WorldPointSnapFunc = [this](const FVector3d& WorldPos, FVector3d& SnapPos)
	{
		return TransformProps->bSnapToWorldGrid && ToolSceneQueriesUtil::FindWorldGridSnapPoint(this, WorldPos, SnapPos);
	};
	ExtrudeHeightMechanic->CurrentHeight = 1.0f;  // initialize to something non-zero...prob should be based on polygon bounds maybe?

	InitializePreviewMesh();

	FDynamicMesh3 PathMesh;
	GeneratePathMesh(PathMesh);
	EditPreview->InitializeExtrudeType(MoveTemp(PathMesh), DrawPlaneWorld.Z(), nullptr, false);

	FDynamicMesh3 TmpMesh;
	EditPreview->MakeExtrudeTypeHitTargetMesh(TmpMesh, false);
	FFrame3d UseFrame = DrawPlaneWorld; 
	UseFrame.Origin = CurPathPoints.Last().Origin;
	ExtrudeHeightMechanic->Initialize(MoveTemp(TmpMesh), UseFrame, true);

	ShowExtrudeMessage();
}


void UDrawPolyPathTool::UpdateExtrudePreview()
{
	EditPreview->UpdateExtrudeType([&](FDynamicMesh3& Mesh) { GenerateExtrudeMesh(Mesh); }, true);
}


void UDrawPolyPathTool::InitializePreviewMesh()
{
	if (EditPreview == nullptr)
	{
		EditPreview = NewObject<UPolyEditPreviewMesh>(this);
		EditPreview->CreateInWorld(TargetWorld, FTransform::Identity);
		ToolSetupUtil::ApplyRenderingConfigurationToPreview(EditPreview, nullptr);
		if ( MaterialProperties->Material == nullptr )
		{
			EditPreview->SetMaterial(
				ToolSetupUtil::GetSelectionMaterial(FLinearColor(0.8f, 0.75f, 0.0f), GetToolManager()));
		}
		else
		{
			EditPreview->SetMaterial(MaterialProperties->Material.Get());
		}
	}
}

void UDrawPolyPathTool::ClearPreview()
{
	if (EditPreview != nullptr)
	{
		EditPreview->Disconnect();
		EditPreview = nullptr;
	}

	CurPolyLoop.Reset();
	SecondPolyLoop.Reset();
}



void UDrawPolyPathTool::GenerateExtrudeMesh(FDynamicMesh3& PathMesh)
{
	FExtrudeMesh Extruder(&PathMesh);
	const FVector3d ExtrudeDir = DrawPlaneWorld.Z();

	const bool bRampMode = (TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampFixed) || (TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampInteractive);

	if (bRampMode)
	{
		const double RampStartRatio = TransformProps->RampStartRatio;
		const double StartHeight = FMathd::Max(0.1, RampStartRatio * FMathd::Abs(CurHeight)) * FMathd::Sign(CurHeight);
		const double EndHeight = CurHeight;
		Extruder.ExtrudedPositionFunc = [&PathMesh, StartHeight, EndHeight, &ExtrudeDir](const FVector3d& P, const FVector3f& N, int32 VID) {
			FVector2f UV = PathMesh.GetVertexUV(VID);
			double UseHeight = FMathd::Lerp(StartHeight, EndHeight, UV.X);
			return P + UseHeight * ExtrudeDir;
		};
	}
	else
	{
		Extruder.ExtrudedPositionFunc = [this, &ExtrudeDir](const FVector3d& P, const FVector3f& N, int32 VID) {
			return P + CurHeight * ExtrudeDir;
		};
	}

	const FAxisAlignedBox3d Bounds = PathMesh.GetBounds();
	Extruder.UVScaleFactor = 1.0 / Bounds.MaxDim();
	Extruder.IsPositiveOffset = (CurHeight >= 0);
	Extruder.Apply();

	FMeshNormals::QuickRecomputeOverlayNormals(PathMesh);
}


void UDrawPolyPathTool::EmitNewObject()
{
	FDynamicMesh3 PathMesh;
	GeneratePathMesh(PathMesh);
	GenerateExtrudeMesh(PathMesh);
	PathMesh.DiscardVertexUVs();  // throw away arc lengths

	FFrame3d MeshTransform = DrawPlaneWorld;
	FVector3d Center = PathMesh.GetBounds().Center();
	MeshTransform.Origin = MeshTransform.ToPlane(Center, 2);
	MeshTransforms::WorldToFrameCoords(PathMesh, MeshTransform);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("CreatePolyPath", "Create PolyPath"));

	FCreateMeshObjectParams NewMeshObjectParams;
	NewMeshObjectParams.TargetWorld = TargetWorld;
	NewMeshObjectParams.Transform = MeshTransform.ToFTransform();
	NewMeshObjectParams.BaseName = TEXT("Path");
	NewMeshObjectParams.Materials.Add(MaterialProperties->Material.Get());
	NewMeshObjectParams.SetMesh(&PathMesh);
	OutputTypeProperties->ConfigureCreateMeshObjectParams(NewMeshObjectParams);
	FCreateMeshObjectResult Result = UE::Modeling::CreateMeshObject(GetToolManager(), MoveTemp(NewMeshObjectParams));
	if (Result.IsOK() && Result.NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), Result.NewActor);
	}

	GetToolManager()->EndUndoTransaction();

	if (bHasSavedWidth)
	{		
		TransformProps->Width = SavedWidth;
		bHasSavedWidth = false;
	}

	if (bHasSavedExtrudeHeight)
	{
		TransformProps->ExtrudeHeight = SavedExtrudeHeight;
		bHasSavedExtrudeHeight = false;
	}

	CurPolyLoop.Reset();
	SecondPolyLoop.Reset();
}



void UDrawPolyPathTool::ShowStartupMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartDraw", "Draw a path on the drawing plane, set its width, and extrude it. Left-click to place path vertices, and click on the last or first vertex to complete the path. Hold Shift to ignore snapping."),
		EToolMessageLevel::UserNotification);
}

void UDrawPolyPathTool::ShowOffsetMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartOffset", "Set the width of the path by clicking on the drawing plane."),
		EToolMessageLevel::UserNotification);
}

void UDrawPolyPathTool::ShowExtrudeMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartExtrude", "Set the height of the extrusion by positioning the mouse over the extrusion volume, or over objects to snap to their heights. Hold Shift to ignore snapping."),
		EToolMessageLevel::UserNotification);
}



void UDrawPolyPathTool::UndoCurrentOperation()
{
	if (SurfacePathMechanic != nullptr)
	{
		SurfacePathMechanic->PopLastPoint();
		if (SurfacePathMechanic->HitPath.Num() == 0)
		{
			CurrentCurveTimestamp++;
		}
	}
	else if (CurveDistMechanic != nullptr)
	{
		CurveDistMechanic = nullptr;
		ClearPreview();
		InitializeNewSurfacePath();
		SurfacePathMechanic->HitPath = CurPathPoints;
	}
	else if (ExtrudeHeightMechanic != nullptr)
	{
		ExtrudeHeightMechanic = nullptr;
		BeginInteractiveOffsetDistance();
	}
}


void FDrawPolyPathStateChange::Revert(UObject* Object)
{
	Cast<UDrawPolyPathTool>(Object)->UndoCurrentOperation();
	bHaveDoneUndo = true;
}
bool FDrawPolyPathStateChange::HasExpired(UObject* Object) const
{
	return bHaveDoneUndo || (Cast<UDrawPolyPathTool>(Object)->CheckInCurve(CurveTimestamp) == false);
}
FString FDrawPolyPathStateChange::ToString() const
{
	return TEXT("FDrawPolyPathStateChange");
}




#undef LOCTEXT_NAMESPACE
