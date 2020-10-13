// Copyright Epic Games, Inc. All Rights Reserved.

#include "DrawPolyPathTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"

#include "ToolSceneQueriesUtil.h"
#include "Intersection/IntersectionUtil.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "MeshIndexUtil.h"
#include "Generators/RectangleMeshGenerator.h"
#include "Distance/DistLine3Line3.h"
#include "AssetGenerationUtil.h"
#include "MeshTransforms.h"
#include "Selection/ToolSelectionUtil.h"
#include "Operations/ExtrudeMesh.h"
#include "MeshNormals.h"

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
	return (this->AssetAPI != nullptr);
}

UInteractiveTool* UDrawPolyPathToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UDrawPolyPathTool* NewTool = NewObject<UDrawPolyPathTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}

/*
* Tool methods
*/
void UDrawPolyPathTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UDrawPolyPathTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
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

	// add properties
	TransformProps = NewObject<UDrawPolyPathProperties>(this);
	TransformProps->RestoreProperties(this);
	AddToolPropertySource(TransformProps);

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

	ShowStartupMessage();
}


void UDrawPolyPathTool::Shutdown(EToolShutdownType ShutdownType)
{
	PlaneMechanic->Shutdown();
	PlaneMechanic = nullptr;

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
		if (SurfacePathMechanic->IsHitByRay(FRay3d(Ray), HitPoint))
		{
			OutHit.Distance = FRay3d(Ray).Project(HitPoint.Origin);
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
		bool bIsFirstPoint = SurfacePathMechanic->HitPath.Num() == 0;
		if (SurfacePathMechanic->TryAddPointFromRay(ClickPos.WorldRay))
		{
			if (SurfacePathMechanic->IsDone())
			{
				bPathIsClosed = SurfacePathMechanic->LoopWasClosed();
				GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginOffset", "Set Offset"));
				OnCompleteSurfacePath();
			}
			else
			{
				GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginPath", "Begin Path"));
			}
		}
	}
	else if (CurveDistMechanic != nullptr)
	{
		GetToolManager()->EmitObjectChange(this, MakeUnique<FDrawPolyPathStateChange>(CurrentCurveTimestamp), LOCTEXT("DrawPolyPathBeginHeight", "Set Height"));
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
		TransformProps->Width = CurveDistMechanic->CurrentDistance;
		CurOffsetDistance = CurveDistMechanic->CurrentDistance;
		UpdatePathPreview();
	}
	else if (ExtrudeHeightMechanic != nullptr)
	{
		ExtrudeHeightMechanic->UpdateCurrentDistance(DevicePos.WorldRay);
		CurHeight = ExtrudeHeightMechanic->CurrentHeight;
		TransformProps->Height = ExtrudeHeightMechanic->CurrentHeight;
		UpdateExtrudePreview();
	}
	return true;
}






void UDrawPolyPathTool::OnTick(float DeltaTime)
{
	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->SetEnableGridSnaping(TransformProps->bSnapToWorldGrid);
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
}




void UDrawPolyPathTool::InitializeNewSurfacePath()
{
	SurfacePathMechanic = NewObject<UCollectSurfacePathMechanic>(this);
	SurfacePathMechanic->Setup(this);
	double SnapTol = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD();
	SurfacePathMechanic->SpatialSnapPointsFunc = [this, SnapTol](FVector3d Position1, FVector3d Position2)
	{
		return true && ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, Position1, Position2, SnapTol);
	};
	SurfacePathMechanic->SetDoubleClickOrCloseLoopMode();
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
	CurPathPoints[0].ConstrainedAlignAxis(0, (CurPathPoints[1].Origin - CurPathPoints[0].Origin).Normalized(), PlaneNormal);
	CurPathPoints[NumPoints-1].ConstrainedAlignAxis(0, (CurPathPoints[NumPoints-1].Origin - CurPathPoints[NumPoints-2].Origin).Normalized(), PlaneNormal);
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
		ArcLengths[j] = ArcLengths[PrevJ] + Cur.Distance(Prev);
		FLine3d Line1(FLine3d::FromPoints(Prev, Cur)), Line2(FLine3d::FromPoints(Cur, Next));
		Line1.Origin += DistOffsetDelta * PlaneNormal.Cross(Line1.Direction);
		Line2.Origin += DistOffsetDelta * PlaneNormal.Cross(Line2.Direction);

		if (Line1.Direction.Dot(Line2.Direction) > 0.999 )
		{
			CurPathPoints[j].ConstrainedAlignAxis(0, (Next-Prev).Normalized(), PlaneNormal);
			OffsetScaleFactors[j] = 1.0;
		}
		else
		{
			FDistLine3Line3d Distance(Line1, Line2);
			Distance.GetSquared();
			FVector3d OffsetPoint = 0.5 * (Distance.Line1ClosestPoint + Distance.Line2ClosestPoint);
			OffsetScaleFactors[j] = OffsetPoint.Distance(Cur) / DistOffsetDelta;
			FVector3d TangentDir = (OffsetPoint - Cur).Normalized().Cross(PlaneNormal);
			CurPathPoints[j].ConstrainedAlignAxis(0, TangentDir, PlaneNormal);
		}
	}
	ArcLengths[NumPoints-1] = ArcLengths[NumPoints-2] + CurPathPoints[NumPoints-1].Origin.Distance(CurPathPoints[NumPoints - 2].Origin);

	CurPolyLine.Reset();
	for (const FFrame3d& Point : SurfacePathMechanic->HitPath)
	{
		CurPolyLine.Add(Point.Origin);
	}

	SurfacePathMechanic = nullptr;
	if (TransformProps->WidthMode == EDrawPolyPathWidthMode::Constant)
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
	CurveDistMechanic->InitializePolyCurve(CurPolyLine, FTransform3d::Identity());

	InitializePreviewMesh();

	ShowOffsetMessage();
}


void UDrawPolyPathTool::BeginConstantOffsetDistance()
{
	InitializePreviewMesh();
	CurOffsetDistance = TransformProps->Width;
	UpdatePathPreview();
	OnCompleteOffsetDistance();
}



void UDrawPolyPathTool::OnCompleteOffsetDistance()
{
	CurveDistMechanic = nullptr;

	if (TransformProps->OutputType == EDrawPolyPathOutputMode::Ribbon)
	{
		OnCompleteExtrudeHeight();
	}
	else if (TransformProps->HeightMode == EDrawPolyPathHeightMode::Constant)
	{
		CurHeight = TransformProps->Height;
		OnCompleteExtrudeHeight();
	}
	else
	{
		BeginInteractiveExtrudeHeight();
	}
}


void UDrawPolyPathTool::OnCompleteExtrudeHeight()
{
	CurHeight = TransformProps->Height;
	ExtrudeHeightMechanic = nullptr;

	ClearPreview();

	EmitNewObject(TransformProps->OutputType);

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
	Mesh.Clear();
	int32 NumPoints = CurPathPoints.Num();
	if (NumPoints > 1)
	{
		if (bPathIsClosed)
		{
			FPolygonEdgeMeshGenerator MeshGen(CurPathPoints, OffsetScaleFactors, CurOffsetDistance, FVector3d::UnitZ());
			MeshGen.Generate();
			Mesh.Copy(&MeshGen);
		}
		else
		{
			CurPathLength = 0;
			for (int32 k = 1; k < NumPoints; ++k)
			{
				CurPathLength += CurPathPoints[k].Origin.Distance(CurPathPoints[k - 1].Origin);
			}

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

			FAxisAlignedBox3d PathBounds = Mesh.GetBounds();

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
	if (TransformProps->OutputType == EDrawPolyPathOutputMode::Ramp)
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

	FExtrudeMesh Extruder(&PathMesh);
	FVector3d ExtrudeDir = DrawPlaneWorld.Z();

	int NumPoints = CurPathPoints.Num();
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
}



void UDrawPolyPathTool::EmitNewObject(EDrawPolyPathOutputMode OutputMode)
{
	FDynamicMesh3 PathMesh;
	GeneratePathMesh(PathMesh);

	if (OutputMode == EDrawPolyPathOutputMode::Extrusion)
	{
		GenerateExtrudeMesh(PathMesh);
	}
	else if (OutputMode == EDrawPolyPathOutputMode::Ramp)
	{
		GenerateRampMesh(PathMesh);
	}
	PathMesh.DiscardVertexUVs();  // throw away arc lengths

	FFrame3d MeshTransform = DrawPlaneWorld;
	FVector3d Center = PathMesh.GetBounds().Center();
	MeshTransform.Origin = MeshTransform.ToPlane(Center, 2);
	MeshTransforms::WorldToFrameCoords(PathMesh, MeshTransform);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("CreatePolyPath", "Create PolyPath"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		&PathMesh, MeshTransform.ToTransform(), TEXT("Path"), MaterialProperties->Material.Get());
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}

	GetToolManager()->EndUndoTransaction();
}



void UDrawPolyPathTool::ShowStartupMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartDraw", "Use this Tool to draw a path on the Drawing Plane, and then Extrude. Left-click to place points, Double-click (or close) to complete path. Ctrl-click on the scene to reposition the Plane (Shift+Ctrl-click to ignore Normal). [A] toggles Gizmo. Hold Shift to ignore Snapping."),
		EToolMessageLevel::UserNotification);
}

void UDrawPolyPathTool::ShowOffsetMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartOffset", "Set the Width of the Extrusion by clicking on the Drawing Plane."),
		EToolMessageLevel::UserNotification);
}

void UDrawPolyPathTool::ShowExtrudeMessage()
{
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartExtrude", "Set the Height of the Extrusion by positioning the mouse over the extrusion volume, or over the scene to snap to relative heights."),
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
