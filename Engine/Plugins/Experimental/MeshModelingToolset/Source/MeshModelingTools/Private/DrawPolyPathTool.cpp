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

#define LOCTEXT_NAMESPACE "UDrawPolyPathTool"



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


void UDrawPolyPathProperties::SaveRestoreProperties(UInteractiveTool* RestoreToTool, bool bSaving)
{
	UDrawPolyPathProperties* PropertyCache = GetPropertyCache<UDrawPolyPathProperties>();
	SaveRestoreProperty(PropertyCache->OutputType, this->OutputType, bSaving);
	SaveRestoreProperty(PropertyCache->WidthMode, this->WidthMode, bSaving);
	SaveRestoreProperty(PropertyCache->Width, this->Width, bSaving);
	SaveRestoreProperty(PropertyCache->HeightMode, this->HeightMode, bSaving);
	SaveRestoreProperty(PropertyCache->Height, this->Height, bSaving);
	SaveRestoreProperty(PropertyCache->RampStartRatio, this->RampStartRatio, bSaving);
	SaveRestoreProperty(PropertyCache->bSnapToWorldGrid, this->bSnapToWorldGrid, bSaving);
}





/*
* Tool methods
*/

UDrawPolyPathTool::UDrawPolyPathTool()
{
}

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

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartDrawPolyPathTool", "Click to begin drawing path. Doubleclick to finish path."),
		EToolMessageLevel::UserNotification);
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






void UDrawPolyPathTool::Tick(float DeltaTime)
{
	UInteractiveTool::Tick(DeltaTime);

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
		return true && ToolSceneQueriesUtil::CalculateViewVisualAngleD(this->CameraState, Position1, Position2) < SnapTol;
	};
	UpdateSurfacePathPlane();
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
	for (int j = 1; j < NumPoints - 1; ++j)
	{
		FVector3d Prev(CurPathPoints[j - 1].Origin), Next(CurPathPoints[j + 1].Origin), Cur(CurPathPoints[j].Origin);
		ArcLengths[j] = ArcLengths[j-1] + Cur.Distance(Prev);
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
		CurPathLength = 0;
		for (int32 k = 1; k < NumPoints; ++k)
		{
			CurPathLength += CurPathPoints[k].Origin.Distance(CurPathPoints[k-1].Origin);
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
			Mesh.SetVertexUV(k, FVector2f((float)ArcLengths[k],(float)k));
			FVector3d V1 = Mesh.GetVertex(NumPoints + k);
			V1.X -= ShiftX;
			V1.Y *= OffsetScaleFactors[k];
			V1 = PathFrame.FromFramePoint(V1);
			Mesh.SetVertex(NumPoints + k, V1);
			Mesh.SetVertexUV(NumPoints+k, FVector2f((float)ArcLengths[k], (float)k));
			ShiftX += DeltaX;
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
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		return TargetWorld->LineTraceSingleByObjectType(HitResult, WorldRay.Origin, WorldRay.PointAt(999999), QueryParams);
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
			EditPreview->SetMaterial(MaterialProperties->Material);
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
		&PathMesh, MeshTransform.ToTransform(), TEXT("Path"), MaterialProperties->Material);
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}

	GetToolManager()->EndUndoTransaction();
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
