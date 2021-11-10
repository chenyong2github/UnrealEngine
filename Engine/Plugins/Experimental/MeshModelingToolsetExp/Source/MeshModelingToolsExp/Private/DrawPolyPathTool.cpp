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
#include "Distance/DistLine3Line3.h"
#include "ModelingObjectsCreationAPI.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Selection/ToolSelectionUtil.h"
#include "Operations/ExtrudeMesh.h"
#include "DynamicMesh/MeshNormals.h"
#include "MeshBoundaryLoops.h"
#include "ToolDataVisualizer.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UDrawPolyPathTool"

namespace
{
	// Simple mesh generator that generates a quad for each edge of an input polygon
	class FPolygonEdgeMeshGenerator : public FMeshShapeGenerator
	{

	private:

		// Polygon to triangulate. Assumed to be closed (i.e. last edge is (LastVertex, FirstVertex)). If Polygon has 
		// self-intersections or degenerate edges, result is undefined.
		TArray<FFrame3d> Polygon;

		// For each polygon vertex, a scale factor for the patch width at that vertex. Helps keep the width constant
		// going around acute corners. 
		TArray<double> OffsetScaleFactors;

		// Width of quads to generate.
		double Width = 1.0;

		// Normal vector of all vertices will be set to this value. Default is +Z axis.
		FVector3d Normal = FVector3d::UnitZ();

	public:

		FPolygonEdgeMeshGenerator(const TArray<FFrame3d>& InPolygon, 
								  const TArray<double>& InOffsetScaleFactors,
								  double InWidth = 1.0,
								  FVector3d InNormal = FVector3d::UnitZ()) :
			Polygon(InPolygon),
			OffsetScaleFactors(InOffsetScaleFactors),
			Width(InWidth),
			Normal(InNormal)
		{
			check(Polygon.Num() == OffsetScaleFactors.Num());
		}


		// Generate the triangulation
		virtual FMeshShapeGenerator& Generate() final
		{
			const int NumInputVertices = Polygon.Num();
			check(NumInputVertices >= 3);
			if (NumInputVertices < 3)
			{
				return *this;
			}
			const int NumVertices = 2 * NumInputVertices;
			const int NumTriangles = 2 * NumInputVertices;
			SetBufferSizes(NumVertices, NumTriangles, NumVertices, NumVertices);

			// Trace the input path, placing vertices on either side of each input vertex 

			const FVector3d LeftVertex{ 0, -Width, 0 };
			const FVector3d RightVertex{ 0, Width, 0 };
			for (int CurrentInputVertex = 0; CurrentInputVertex < NumInputVertices; ++CurrentInputVertex)
			{
				const FFrame3d& CurrentFrame = Polygon[CurrentInputVertex];
				const int NewVertexAIndex = 2 * CurrentInputVertex;
				const int NewVertexBIndex = NewVertexAIndex + 1;
				Vertices[NewVertexAIndex] = CurrentFrame.FromFramePoint(OffsetScaleFactors[CurrentInputVertex] * LeftVertex);
				Vertices[NewVertexBIndex] = CurrentFrame.FromFramePoint(OffsetScaleFactors[CurrentInputVertex] * RightVertex);
			}

			// Triangulate the vertices we just placed

			for (int CurrentVertex = 0; CurrentVertex < NumInputVertices; ++CurrentVertex)
			{
				const int NewVertexA = 2 * CurrentVertex;
				const int NewVertexB = NewVertexA + 1;
				const int NewVertexC = (NewVertexA + 2) % NumVertices;
				const int NewVertexD = (NewVertexA + 3) % NumVertices;

				FIndex3i NewTriA{ NewVertexA, NewVertexB, NewVertexC };
				const int NewTriAIndex = 2 * CurrentVertex;

				SetTriangle(NewTriAIndex, NewTriA);
				SetTriangleUVs(NewTriAIndex, NewTriA);
				SetTriangleNormals(NewTriAIndex, NewTriA);
				SetTrianglePolygon(NewTriAIndex, 0);

				const int NewTriBIndex = NewTriAIndex + 1;
				FIndex3i NewTriB{ NewVertexC, NewVertexB, NewVertexD };

				SetTriangle(NewTriBIndex, NewTriB);
				SetTriangleUVs(NewTriBIndex, NewTriB);
				SetTriangleNormals(NewTriBIndex, NewTriB);
				SetTrianglePolygon(NewTriBIndex, 0);
			}

			// Set UVs, etc. for the vertices we put down previously

			FAxisAlignedBox2d BoundingBox;
			for (auto& CurrentFrame : Polygon)
			{
				BoundingBox.Contain(FVector2d{ CurrentFrame.Origin.X, CurrentFrame.Origin.Y });
			}
			BoundingBox.Max += {Width, Width};
			BoundingBox.Min -= {Width, Width};

			double BoxWidth = BoundingBox.Width(), BoxHeight = BoundingBox.Height();
			double UVScale = FMath::Max(BoxWidth, BoxHeight);

			for (int NewVertexIndex = 0; NewVertexIndex < NumVertices; ++NewVertexIndex)
			{
				FVector3d Pos = Vertices[NewVertexIndex];

				UVs[NewVertexIndex] = FVector2f(
					(float)((Pos.X - BoundingBox.Min.X) / UVScale),
					(float)((Pos.Y - BoundingBox.Min.Y) / UVScale));
				UVParentVertex[NewVertexIndex] = NewVertexIndex;
				Normals[NewVertexIndex] = FVector3f(Normal);
				NormalParentVertex[NewVertexIndex] = NewVertexIndex;
			}

			return *this;
		}
	};		// class FPolygonEdgeMeshGenerator

}	// unnamed namespace

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
	PlaneMechanic->Shutdown();
	PlaneMechanic = nullptr;

	OutputTypeProperties->SaveProperties(this);
	TransformProps->SaveProperties(this);
	ExtrudeProperties->SaveProperties(this);
	MaterialProperties->SaveProperties(this);

	ClearPreview();
}




void UDrawPolyPathTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
	//	TEXT("PopLastVertex"),
	//	LOCTEXT("PopLastVertex", "Pop Last Vertex"),
	//	LOCTEXT("PopLastVertexTooltip", "Pop last vertex added to polygon"),
	//	EModifierKey::None, EKeys::BackSpace,
	//	[this]() { PopLastVertexAction(); });


	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
	//	TEXT("ToggleGizmo"),
	//	LOCTEXT("ToggleGizmo", "Toggle Gizmo"),
	//	LOCTEXT("ToggleGizmoTooltip", "Toggle visibility of the transformation Gizmo"),
	//	EModifierKey::None, EKeys::A,
	//	[this]() { PolygonProperties->bShowGizmo = !PolygonProperties->bShowGizmo; });
}


bool UDrawPolyPathTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	if (SurfacePathMechanic != nullptr)
	{
		FFrame3d HitPoint;
		if (SurfacePathMechanic->IsHitByRay(UE::Geometry::FRay3d(Ray), HitPoint))
		{
			OutHit.Distance = UE::Geometry::FRay3d(Ray).Project(HitPoint.Origin);
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
		if (SurfacePathMechanic->TryAddPointFromRay(ClickPos.WorldRay))
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
		SurfacePathMechanic->UpdatePreviewPoint(DevicePos.WorldRay);
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
	ArcLengths.SetNum(NumPoints);
	ArcLengths[0] = 0;

	// Set local frames for path points. If the path is closed, we will adjust the first and last frames for continuity,
	// otherwise we will leave them as set above.
	int LastPointIndex = bPathIsClosed ? NumPoints : NumPoints - 1;
	int FirstPointIndex = bPathIsClosed ? 0 : 1;
	for (int j = FirstPointIndex; j < LastPointIndex; ++j)
	{
		int NextJ = (j + 1) % NumPoints;
		int PrevJ = (j - 1 + NumPoints) % NumPoints;
		FVector3d Prev(CurPathPoints[PrevJ].Origin), Next(CurPathPoints[NextJ].Origin), Cur(CurPathPoints[j].Origin);
		if (j != 0)
		{
			ArcLengths[j] = ArcLengths[PrevJ] + Distance(Cur, Prev);
		}
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
	ArcLengths[NumPoints-1] = ArcLengths[NumPoints-2] + Distance(CurPathPoints[NumPoints-1].Origin, CurPathPoints[NumPoints - 2].Origin);

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
	CurHeight = TransformProps->ExtrudeHeight;
	ExtrudeHeightMechanic = nullptr;

	ClearPreview();

	EmitNewObject(TransformProps->ExtrudeMode);

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

	Mesh.Clear();
	int32 NumPoints = CurPathPoints.Num();
	if (NumPoints > 1)
	{
		CurPathLength = 0;
		for (int32 k = 1; k < NumPoints; ++k)
		{
			CurPathLength += Distance(CurPathPoints[k].Origin, CurPathPoints[k - 1].Origin);
		}

		if (bPathIsClosed)
		{
			TArray<FFrame3d> UsePathPoints = CurPathPoints;
			TArray<double> UseOffsetScaleFactors = OffsetScaleFactors;
			TArray<double> UseArcLengths = ArcLengths;

			const bool bRampMode = TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampFixed || TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampInteractive;

			if (bRampMode)
			{
				// Duplicate vertices at the beginning/end of the path when generating a ramp
				// TODO: This creates sliver quads along the bottom path and along the extrusion sides. Ideally we should do something smarter.
				FFrame3d FirstPoint = UsePathPoints[0];
				UsePathPoints.Add(FirstPoint);
				
				const double FirstScaleFactor = UseOffsetScaleFactors[0];
				UseOffsetScaleFactors.Add(FirstScaleFactor);				

				const double ArcLen = FVector3d::Distance(CurPathPoints.Last().Origin, FirstPoint.Origin);
				UseArcLengths.Add(ArcLengths.Last() + ArcLen);
				CurPathLength += ArcLen;

				++NumPoints;
			}

			FPolygonEdgeMeshGenerator MeshGen(UsePathPoints, UseOffsetScaleFactors, CurOffsetDistance, FVector3d::UnitZ());
			MeshGen.Generate();
			Mesh.Copy(&MeshGen);

			Mesh.EnableVertexUVs(FVector2f::Zero());
			for (int k = 0; k < NumPoints; ++k)
			{
				// Temporarily set vertex UVs to arclengths, for use in interpolating height in ramp mode
				Mesh.SetVertexUV(2 * k, FVector2f((float)UseArcLengths[k], (float)k));
				Mesh.SetVertexUV(2 * k + 1, FVector2f((float)UseArcLengths[k], (float)k));
			}
		}
		else
		{
			FRectangleMeshGenerator MeshGen;
			MeshGen.Width = CurPathLength;
			MeshGen.Height = 2 * CurOffsetDistance;
			MeshGen.Normal = FVector3f::UnitZ();
			MeshGen.Origin = FVector3d(CurPathLength / 2, 0, 0);
			MeshGen.HeightVertexCount = 2;
			MeshGen.WidthVertexCount = NumPoints;
			MeshGen.Generate();
			Mesh.Copy(&MeshGen);
			Mesh.EnableVertexUVs(FVector2f::Zero());		// we will store arc length for each vtx in VertexUV

			double ShiftX = 0;
			double DeltaX = CurPathLength / (double)(NumPoints - 1);
			for (int32 k = 0; k < NumPoints; ++k)
			{
				FFrame3d PathFrame = CurPathPoints[k];
				FVector3d V0 = Mesh.GetVertex(k);
				V0.X -= ShiftX;
				V0.Y *= OffsetScaleFactors[k];
				V0 = PathFrame.FromFramePoint(V0);
				Mesh.SetVertex(k, V0);
				Mesh.SetVertexUV(k, FVector2f((float)ArcLengths[k], (float)k));
				FVector3d V1 = Mesh.GetVertex(NumPoints + k);
				V1.X -= ShiftX;
				V1.Y *= OffsetScaleFactors[k];
				V1 = PathFrame.FromFramePoint(V1);
				Mesh.SetVertex(NumPoints + k, V1);
				Mesh.SetVertexUV(NumPoints + k, FVector2f((float)ArcLengths[k], (float)k));
				ShiftX += DeltaX;
			}
		}

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
}



void UDrawPolyPathTool::BeginInteractiveExtrudeHeight()
{
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
	if (TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampFixed || TransformProps->ExtrudeMode == EDrawPolyPathExtrudeMode::RampInteractive)
	{
		EditPreview->UpdateExtrudeType( [&](FDynamicMesh3& Mesh) { GenerateRampMesh(Mesh); }, true);
	}
	else
	{
		EditPreview->UpdateExtrudeType([&](FDynamicMesh3& Mesh) { GenerateExtrudeMesh(Mesh); }, true);
	}
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
	FAxisAlignedBox3d Bounds = PathMesh.GetBounds();

	FExtrudeMesh Extruder(&PathMesh);
	FVector3d ExtrudeDir = DrawPlaneWorld.Z();
	Extruder.ExtrudedPositionFunc = [&](const FVector3d& P, const FVector3f& N, int32 VID) {
		return P + CurHeight * ExtrudeDir;
	};
	Extruder.UVScaleFactor = 1.0 / Bounds.MaxDim();
	Extruder.IsPositiveOffset = (CurHeight >= 0);
	Extruder.Apply();

	FMeshNormals::QuickRecomputeOverlayNormals(PathMesh);
}

void UDrawPolyPathTool::GenerateRampMesh(FDynamicMesh3& PathMesh)
{
	FAxisAlignedBox3d Bounds = PathMesh.GetBounds();

	// two tris forming last quad on the path mesh
	int LastQuad[2] = { FDynamicMesh3::InvalidID, FDynamicMesh3::InvalidID };
	if (bPathIsClosed)
	{
		int NT = PathMesh.MaxTriangleID();
		LastQuad[0] = NT - 2;
		LastQuad[1] = NT - 1;
	}

	FExtrudeMesh Extruder(&PathMesh);
	FVector3d ExtrudeDir = DrawPlaneWorld.Z();

	double RampStartRatio = TransformProps->RampStartRatio;
	double StartHeight = FMathd::Max(0.1, RampStartRatio * FMathd::Abs(CurHeight)) * FMathd::Sign(CurHeight);
	double EndHeight = CurHeight;
	Extruder.ExtrudedPositionFunc = [&](const FVector3d& P, const FVector3f& N, int32 VID) {
		FVector2f UV = Extruder.Mesh->GetVertexUV(VID);
		double UseHeight = FMathd::Lerp(StartHeight, EndHeight, (UV.X / this->CurPathLength));
		return P + UseHeight * ExtrudeDir;
	};

	Extruder.UVScaleFactor = 1.0 / Bounds.MaxDim();
	Extruder.IsPositiveOffset = (CurHeight >= 0);
	Extruder.Apply();

	FMeshNormals::QuickRecomputeOverlayNormals(PathMesh);

	if (bPathIsClosed)
	{
		// Last quad on a closed loop ramp will be vertical -- compute some UVs for the quad
		const FExtrudeMesh::FExtrusionInfo& Info = Extruder.Extrusions[0];

		if (!ensure(PathMesh.IsTriangle(LastQuad[0])))
		{
			return;
		}
		if (!ensure(PathMesh.IsTriangle(LastQuad[1])))
		{
			return;
		}

		int Index0 = Info.InitialTriangles.Find(LastQuad[0]);
		if (!ensure(Index0 != INDEX_NONE))
		{
			return;
		}
		int NewTri0 = Info.OffsetTriangles[Index0];

		int Index1 = Info.InitialTriangles.Find(LastQuad[1]);
		if (!ensure(Index1 != INDEX_NONE))
		{
			return;
		}
		int NewTri1 = Info.OffsetTriangles[Index1];

		FVector3d Normal = PathMesh.GetTriNormal(NewTri0);
		FVector3d Up = ExtrudeDir;
		ensure(FMath::Abs(Normal.Dot(Up)) < KINDA_SMALL_NUMBER);
		FVector3d Right = -Normal.Cross(Up);
		FFrame3d ProjectFrame(FVector3d::Zero(), Right, Up, Normal);

		FDynamicMeshEditor Editor(&PathMesh);
		Editor.SetQuadUVsFromProjection({ NewTri0, NewTri1 }, ProjectFrame, Extruder.UVScaleFactor);
	}

}



void UDrawPolyPathTool::EmitNewObject(EDrawPolyPathExtrudeMode ExtrudeMode)
{
	FDynamicMesh3 PathMesh;
	GeneratePathMesh(PathMesh);

	if (ExtrudeMode == EDrawPolyPathExtrudeMode::Fixed || ExtrudeMode == EDrawPolyPathExtrudeMode::Interactive)
	{
		GenerateExtrudeMesh(PathMesh);
	}
	else if (ExtrudeMode == EDrawPolyPathExtrudeMode::RampFixed || ExtrudeMode == EDrawPolyPathExtrudeMode::RampInteractive)
	{
		GenerateRampMesh(PathMesh);
	}
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
