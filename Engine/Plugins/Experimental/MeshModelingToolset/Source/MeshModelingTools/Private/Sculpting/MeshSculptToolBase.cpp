// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Sculpting/MeshSculptToolBase.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "ToolSetupUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "PreviewMesh.h"
#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"
#include "Drawing/MeshDebugDrawing.h"

#include "Sculpting/StampFalloffs.h"

#include "Generators/SphereGenerator.h"



#define LOCTEXT_NAMESPACE "UMeshSculptToolBase"



namespace
{
	const FString VertexSculptIndicatorGizmoType = TEXT("VertexSculptIndicatorGizmoType");
}


void UMeshSculptToolBase::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UMeshSculptToolBase::Setup()
{
	UMeshSurfacePointTool::Setup();

	BrushProperties = NewObject<USculptBrushProperties>(this);
	BrushProperties->RestoreProperties(this);
	BrushProperties->bShowStrength = false;
	// Note that brush properties includes BrushRadius, which, when not used as a constant,
	// serves as an output property based on target size and brush size, and so it would need
	// updating after the RestoreProperties() call. But deriving classes will call 
	// InitializeBrushSizeRange after this Setup() call to finish the brush setup, which will
	// update the output property if necessary.

	// work plane
	GizmoProperties = NewObject<UWorkPlaneProperties>();
	GizmoProperties->RestoreProperties(this);

	// create proxy for plane gizmo, but not gizmo itself, as it only appears in FixedPlane brush mode
	// listen for changes to the proxy and update the plane when that happens
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UMeshSculptToolBase::PlaneTransformChanged);

	//GizmoProperties->WatchProperty(GizmoProperties->Position,
	//	[this](FVector NewPosition) { UpdateGizmoFromProperties(); });
	//GizmoProperties->WatchProperty(GizmoProperties->Rotation,
	//	[this](FQuat NewRotation) { UpdateGizmoFromProperties(); });
	GizmoPositionWatcher.Initialize(
		[this]() { return GizmoProperties->Position; },
		[this](FVector NewPosition) { UpdateGizmoFromProperties(); }, GizmoProperties->Position);
	GizmoRotationWatcher.Initialize(
		[this]() { return GizmoProperties->Rotation; },
		[this](FQuat NewRotation) { UpdateGizmoFromProperties(); }, GizmoProperties->Rotation);



	// display
	ViewProperties = NewObject<UMeshEditingViewProperties>();
	ViewProperties->RestoreProperties(this);

	ViewProperties->WatchProperty(ViewProperties->bShowWireframe,
		[this](bool bNewValue) { UpdateWireframeVisibility(bNewValue); });
	ViewProperties->WatchProperty(ViewProperties->MaterialMode,
		[this](EMeshEditingMaterialModes NewMode) { UpdateMaterialMode(NewMode); });
	ViewProperties->WatchProperty(ViewProperties->bFlatShading,
		[this](bool bNewValue) { UpdateFlatShadingSetting(bNewValue); });
	ViewProperties->WatchProperty(ViewProperties->Color,
		[this](FLinearColor NewColor) { UpdateColorSetting(NewColor); });
	ViewProperties->WatchProperty(ViewProperties->Image,
		[this](UTexture2D* NewImage) { UpdateImageSetting(NewImage); });
}


void UMeshSculptToolBase::OnCompleteSetup()
{
	RestoreAllBrushTypeProperties(this);

	for (auto Pair : BrushOpPropSets)
	{
		SetToolPropertySourceEnabled(Pair.Value, false);
	}
}


void UMeshSculptToolBase::Shutdown(EToolShutdownType ShutdownType)
{
	if (ShutdownType == EToolShutdownType::Accept && AreAllTargetsValid() == false)
	{
		UE_LOG(LogTemp, Error, TEXT("Tool Target has become Invalid (possibly it has been Force Deleted). Aborting Tool."));
		ShutdownType = EToolShutdownType::Cancel;
	}

	UMeshSurfacePointTool::Shutdown(ShutdownType);

	BrushIndicatorMesh->Disconnect();
	BrushIndicatorMesh = nullptr;

	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);
	BrushIndicator = nullptr;
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(VertexSculptIndicatorGizmoType);

	BrushProperties->SaveProperties(this);
	if (GizmoProperties)
	{
		GizmoProperties->SaveProperties(this);
	}

	ViewProperties->SaveProperties(this);

	SaveAllBrushTypeProperties(this);


	// bake result
	UBaseDynamicMeshComponent* DynamicMeshComponent = GetSculptMeshComponent();
	if (DynamicMeshComponent != nullptr)
	{
		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// safe to do this here because we are about to destroy componeont
			DynamicMeshComponent->ApplyTransform(InitialTargetTransform, true);

			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("SculptMeshToolTransactionName", "Sculpt Mesh"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FConversionToMeshDescriptionOptions ConversionOptions;
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, false, ConversionOptions);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}

}



void UMeshSculptToolBase::OnTick(float DeltaTime)
{
	GizmoPositionWatcher.CheckAndUpdate();
	GizmoRotationWatcher.CheckAndUpdate();

	ActivePressure = GetCurrentDevicePressure();

	if (InStroke() == false)
	{
		SaveActiveStrokeModifiers();
	}

	// update cached falloff
	CurrentBrushFalloff = 0.5;
	if (GetActiveBrushOp()->PropertySet.IsValid())
	{
		CurrentBrushFalloff = FMathd::Clamp(GetActiveBrushOp()->PropertySet->GetFalloff(), 0.0, 1.0);
	}

	UpdateHoverStamp(GetBrushFrameWorld());

	UpdateWorkPlane();
}


void UMeshSculptToolBase::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);
	// Cache here for usage during interaction, should probably happen in ::Tick() or elsewhere
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	FViewCameraState RenderCameraState = RenderAPI->GetCameraState();

	BrushIndicator->Update((float)GetCurrentBrushRadius(),
		(FVector)HoverStamp.WorldFrame.Origin, (FVector)HoverStamp.WorldFrame.Z(),
		1.0f - (float)GetCurrentBrushFalloff());
	if (BrushIndicatorMaterial)
	{
		double FixedDimScale = ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(RenderCameraState, HoverStamp.WorldFrame.Origin, 1.5f);
		BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffWidth"), FixedDimScale);
	}

	if (ShowWorkPlane())
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		FColor GridColor(128, 128, 128, 32);
		float GridThickness = 0.5f*RenderCameraState.GetPDIScalingFactor();
		int NumGridLines = 10;
		FFrame3f DrawFrame(GizmoProperties->Position, GizmoProperties->Rotation);
		MeshDebugDraw::DrawSimpleFixedScreenAreaGrid(RenderCameraState, DrawFrame, NumGridLines, 45.0, GridThickness, GridColor, false, PDI, FTransform::Identity);
	}
}





void UMeshSculptToolBase::InitializeSculptMeshComponent(UBaseDynamicMeshComponent* Component)
{
	Component->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	Component->RegisterComponent();

	// initialize from LOD-0 MeshDescription
	Component->InitializeMesh(ComponentTarget->GetMesh());
	double MaxDimension = Component->GetMesh()->GetCachedBounds().MaxDim();

	// bake rotation and scaling into mesh because handling these inside sculpting is a mess
	// Note: this transform does not include translation ( so only the 3x3 transform)
	InitialTargetTransform = FTransform3d(ComponentTarget->GetWorldTransform());
	// clamp scaling because if we allow zero-scale we cannot invert this transform on Accept
	InitialTargetTransform.ClampMinimumScale(0.01);
	FVector3d Translation = InitialTargetTransform.GetTranslation();
	InitialTargetTransform.SetTranslation(FVector3d::Zero());
	Component->ApplyTransform(InitialTargetTransform, false);
	CurTargetTransform = FTransform3d(Translation);
	Component->SetWorldTransform((FTransform)CurTargetTransform);

	// hide input Component
	ComponentTarget->SetOwnerVisibility(false);
}






void UMeshSculptToolBase::RegisterBrushType(int32 Identifier, TUniquePtr<FMeshSculptBrushOpFactory> Factory, UMeshSculptBrushOpProps* PropSet)
{
	check(BrushOpPropSets.Contains(Identifier) == false && BrushOpFactories.Contains(Identifier) == false);
	BrushOpPropSets.Add(Identifier, PropSet);
	BrushOpFactories.Add(Identifier, MoveTemp(Factory));

	AddToolPropertySource(PropSet);
	SetToolPropertySourceEnabled(PropSet, false);
}

void UMeshSculptToolBase::RegisterSecondaryBrushType(int32 Identifier, TUniquePtr<FMeshSculptBrushOpFactory> Factory, UMeshSculptBrushOpProps* PropSet)
{
	check(SecondaryBrushOpPropSets.Contains(Identifier) == false && SecondaryBrushOpFactories.Contains(Identifier) == false);
	SecondaryBrushOpPropSets.Add(Identifier, PropSet);
	SecondaryBrushOpFactories.Add(Identifier, MoveTemp(Factory));

	AddToolPropertySource(PropSet);
	SetToolPropertySourceEnabled(PropSet, false);
}


void UMeshSculptToolBase::SaveAllBrushTypeProperties(UInteractiveTool* SaveFromTool)
{
	for (auto Pair : BrushOpPropSets)
	{
		Pair.Value->SaveProperties(SaveFromTool);
	}
	for (auto Pair : SecondaryBrushOpPropSets)
	{
		Pair.Value->SaveProperties(SaveFromTool);
	}
}
void UMeshSculptToolBase::RestoreAllBrushTypeProperties(UInteractiveTool* RestoreToTool)
{
	for (auto Pair : BrushOpPropSets)
	{
		Pair.Value->RestoreProperties(RestoreToTool);
	}
	for (auto Pair : SecondaryBrushOpPropSets)
	{
		Pair.Value->RestoreProperties(RestoreToTool);
	}
}


void UMeshSculptToolBase::SetActivePrimaryBrushType(int32 Identifier)
{
	TUniquePtr<FMeshSculptBrushOpFactory>* Factory = BrushOpFactories.Find(Identifier);
	if (Factory == nullptr)
	{
		check(false);
		return;
	}

	if (PrimaryVisiblePropSet != nullptr)
	{
		SetToolPropertySourceEnabled(PrimaryVisiblePropSet, false);
		PrimaryVisiblePropSet = nullptr;
	}

	PrimaryBrushOp = (*Factory)->Build();
	PrimaryBrushOp->Falloff = PrimaryFalloff;

	UMeshSculptBrushOpProps** FoundProps = BrushOpPropSets.Find(Identifier);
	if (FoundProps != nullptr)
	{
		SetToolPropertySourceEnabled(*FoundProps, true);
		PrimaryVisiblePropSet = *FoundProps;

		PrimaryBrushOp->PropertySet = PrimaryVisiblePropSet;
	}
}



void UMeshSculptToolBase::SetActiveSecondaryBrushType(int32 Identifier)
{
	TUniquePtr<FMeshSculptBrushOpFactory>* Factory = SecondaryBrushOpFactories.Find(Identifier);
	if (Factory == nullptr)
	{
		check(false);
		return;
	}

	if (SecondaryVisiblePropSet != nullptr)
	{
		SetToolPropertySourceEnabled(SecondaryVisiblePropSet, false);
		SecondaryVisiblePropSet = nullptr;
	}

	SecondaryBrushOp = (*Factory)->Build();
	TSharedPtr<FMeshSculptFallofFunc> SecondaryFalloff = MakeShared<FMeshSculptFallofFunc>();;
	SecondaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeStandardSmoothFalloff();
	SecondaryBrushOp->Falloff = SecondaryFalloff;

	UMeshSculptBrushOpProps** FoundProps = SecondaryBrushOpPropSets.Find(Identifier);
	if (FoundProps != nullptr)
	{
		SetToolPropertySourceEnabled(*FoundProps, true);
		SecondaryVisiblePropSet = *FoundProps;

		SecondaryBrushOp->PropertySet = SecondaryVisiblePropSet;
	}
}



TUniquePtr<FMeshSculptBrushOp>& UMeshSculptToolBase::GetActiveBrushOp()
{
	if (GetInSmoothingStroke())
	{
		return SecondaryBrushOp;
	}
	else
	{
		return PrimaryBrushOp;
	}
}




void UMeshSculptToolBase::SetPrimaryFalloffType(EMeshSculptFalloffType FalloffType)
{
	PrimaryFalloff = MakeShared<FMeshSculptFallofFunc>();
	switch (FalloffType)
	{
	default:
	case EMeshSculptFalloffType::Smooth:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeStandardSmoothFalloff();
		break;
	case EMeshSculptFalloffType::Linear:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeLinearFalloff();
		break;
	case EMeshSculptFalloffType::Inverse:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeInverseFalloff();
		break;
	case EMeshSculptFalloffType::Round:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeRoundFalloff();
		break;
	case EMeshSculptFalloffType::BoxSmooth:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeSmoothBoxFalloff();
		break;
	case EMeshSculptFalloffType::BoxLinear:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeLinearBoxFalloff();
		break;
	case EMeshSculptFalloffType::BoxInverse:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeInverseBoxFalloff();
		break;
	case EMeshSculptFalloffType::BoxRound:
		PrimaryFalloff->FalloffFunc = UE::SculptFalloffs::MakeRoundBoxFalloff();
		break;
	}
}




bool UMeshSculptToolBase::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	FRay3d LocalRay = GetLocalRay(Ray);

	int HitTID = FindHitSculptMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		FTriangle3d Triangle;
		FDynamicMesh3* Mesh = GetSculptMesh();
		Mesh->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		OutHit.FaceIndex = HitTID;
		OutHit.Distance = Query.RayParameter;
		OutHit.Normal = (FVector)CurTargetTransform.TransformNormal(Mesh->GetTriNormal(HitTID));
		OutHit.ImpactPoint = (FVector)CurTargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		return true;
	}
	return false;
}


void UMeshSculptToolBase::OnBeginDrag(const FRay& WorldRay)
{
	SaveActiveStrokeModifiers();

	FHitResult OutHit;
	if (HitTest(WorldRay, OutHit))
	{
		bInStroke = true;

		UpdateBrushTargetPlaneFromHit(WorldRay, OutHit);

		// initialize first stamp
		PendingStampRay = WorldRay;
		bIsStampPending = true;

		// set falloff
		PrimaryBrushOp->Falloff = PrimaryFalloff;

		OnBeginStroke(WorldRay);
	}
}

void UMeshSculptToolBase::OnUpdateDrag(const FRay& WorldRay)
{
	if (InStroke())
	{
		PendingStampRay = WorldRay;
		bIsStampPending = true;
	}
}

void UMeshSculptToolBase::OnEndDrag(const FRay& Ray)
{
	bInStroke = false;

	// cancel these! otherwise change record could become invalid
	bIsStampPending = false;

	OnEndStroke();

}



FRay3d UMeshSculptToolBase::GetLocalRay(const FRay& WorldRay) const
{
	FRay3d LocalRay(CurTargetTransform.InverseTransformPosition(WorldRay.Origin),
		CurTargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();
	return LocalRay;
}



void UMeshSculptToolBase::UpdateBrushFrameWorld(const FVector3d& NewPosition, const FVector3d& NewNormal)
{
	FFrame3d NewFrame = LastBrushFrameWorld;
	NewFrame.Origin = NewPosition;
	NewFrame.AlignAxis(2, NewNormal);
	NewFrame.ConstrainedAlignPerpAxes();

	if (InStroke() && BrushProperties->Lazyness > 0)
	{
		double t = FMathd::Lerp(1.0, 0.1, BrushProperties->Lazyness);
		LastBrushFrameWorld.Origin = FVector3d::Lerp(LastBrushFrameWorld.Origin, NewFrame.Origin, t);
		LastBrushFrameWorld.Rotation = FQuaterniond(LastBrushFrameWorld.Rotation, NewFrame.Rotation, t);
	}
	else
	{
		LastBrushFrameWorld = NewFrame;

	}

	LastBrushFrameLocal = LastBrushFrameWorld;
	LastBrushFrameLocal.Transform(CurTargetTransform.Inverse());
}

void UMeshSculptToolBase::AlignBrushToView()
{
	UpdateBrushFrameWorld(GetBrushFrameWorld().Origin, -CameraState.Forward());
}



void UMeshSculptToolBase::UpdateBrushTargetPlaneFromHit(const FRay& WorldRay, const FHitResult& Hit)
{
	FVector3d WorldPosWithBrushDepth = WorldRay.PointAt(Hit.Distance) + GetCurrentBrushDepth() * GetCurrentBrushRadius() * WorldRay.Direction;
	ActiveBrushTargetPlaneWorld = FFrame3d(WorldPosWithBrushDepth, -WorldRay.Direction);
}

bool UMeshSculptToolBase::UpdateBrushPositionOnActivePlane(const FRay& WorldRay)
{
	FVector3d NewHitPosWorld;
	ActiveBrushTargetPlaneWorld.RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, 2, NewHitPosWorld);
	UpdateBrushFrameWorld(NewHitPosWorld, ActiveBrushTargetPlaneWorld.Z());
	return true;
}

bool UMeshSculptToolBase::UpdateBrushPositionOnTargetMesh(const FRay& WorldRay, bool bFallbackToViewPlane)
{
	FRay3d LocalRay = GetLocalRay(WorldRay);
	int32 HitTID = FindHitTargetMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		const FDynamicMesh3* BaseMesh = GetBaseMesh();
		FIntrRay3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleIntersection(*BaseMesh, HitTID, LocalRay);
		FVector3d WorldNormal = CurTargetTransform.TransformNormal(BaseMesh->GetTriNormal(HitTID));
		FVector3d WorldPos = CurTargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		UpdateBrushFrameWorld(WorldPos, WorldNormal);
		return true;
	}

	if (bFallbackToViewPlane)
	{
		FFrame3d BrushPlane(GetBrushFrameWorld().Origin, CameraState.Forward());
		FVector3d NewHitPosWorld;
		BrushPlane.RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, 2, NewHitPosWorld);
		UpdateBrushFrameWorld(NewHitPosWorld, ActiveBrushTargetPlaneWorld.Z());
		return true;
	}

	return false;
}

bool UMeshSculptToolBase::UpdateBrushPositionOnSculptMesh(const FRay& WorldRay, bool bFallbackToViewPlane)
{
	FRay3d LocalRay = GetLocalRay(WorldRay);
	int32 HitTID = FindHitSculptMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		const FDynamicMesh3* SculptMesh = GetSculptMesh();
		FIntrRay3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleIntersection(*SculptMesh, HitTID, LocalRay);
		FVector3d WorldNormal = CurTargetTransform.TransformNormal(SculptMesh->GetTriNormal(HitTID));
		FVector3d WorldPos = CurTargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		UpdateBrushFrameWorld(WorldPos, WorldNormal);
		return true;
	}

	if (bFallbackToViewPlane)
	{
		FFrame3d BrushPlane(GetBrushFrameWorld().Origin, CameraState.Forward());
		FVector3d NewHitPosWorld;
		BrushPlane.RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, 2, NewHitPosWorld);
		UpdateBrushFrameWorld(NewHitPosWorld, ActiveBrushTargetPlaneWorld.Z());
		return true;
	}

	return false;
}





void UMeshSculptToolBase::SaveActiveStrokeModifiers()
{
	bSmoothing = GetShiftToggle();
	bInvert = GetCtrlToggle();
}


void UMeshSculptToolBase::UpdateHoverStamp(const FFrame3d& StampFrame)
{
	HoverStamp.WorldFrame = StampFrame;
}

void UMeshSculptToolBase::ApplyStrokeFlowInTick()
{
	bIsStampPending = InStroke();
}



FFrame3d UMeshSculptToolBase::ComputeStampRegionPlane(const FFrame3d& StampFrame, const TSet<int32>& StampTriangles, bool bIgnoreDepth, bool bViewAligned, bool bInvDistFalloff)
{
	const FDynamicMesh3* Mesh = GetSculptMesh();
	double FalloffRadius = GetCurrentBrushRadius();
	if (bInvDistFalloff)
	{
		FalloffRadius *= 0.5;
	}
	FVector3d StampNormal = StampFrame.Z();

	FVector3d AverageNormal(0, 0, 0);
	FVector3d AveragePos(0, 0, 0);
	double WeightSum = 0;
	for (int TriID : StampTriangles)
	{
		FVector3d Normal, Centroid; double Area;
		Mesh->GetTriInfo(TriID, Normal, Area, Centroid);
		if (Normal.Dot(StampNormal) < -0.2)		// ignore back-facing (heuristic to avoid "other side")
		{
			continue;
		}

		double Distance = StampFrame.Origin.Distance(Centroid);
		double NormalizedDistance = (Distance / FalloffRadius) + 0.0001;

		double Weight = Area;
		if (bInvDistFalloff)
		{
			double RampT = FMathd::Clamp(1.0 - NormalizedDistance, 0.0, 1.0);
			Weight *= FMathd::Clamp(RampT * RampT * RampT, 0.0, 1.0);
		}
		else
		{
			if (NormalizedDistance > 0.5)
			{
				double d = FMathd::Clamp((NormalizedDistance - 0.5) / (1.0 - 0.5), 0.0, 1.0);
				double t = (1.0 - d * d);
				Weight *= (t * t * t);
			}
		}

		AverageNormal += Weight * Mesh->GetTriNormal(TriID);
		AveragePos += Weight * Centroid;
		WeightSum += Weight;
	}
	AverageNormal.Normalize();
	AveragePos /= WeightSum;

	if (bViewAligned)
	{
		AverageNormal = -CameraState.Forward();
	}

	FFrame3d Result = FFrame3d(AveragePos, AverageNormal);
	if (bIgnoreDepth == false)
	{
		Result.Origin -= GetCurrentBrushDepth() * GetCurrentBrushRadius() * Result.Z();
	}

	return Result;
}



void UMeshSculptToolBase::UpdateStrokeReferencePlaneForROI(const FFrame3d& StampFrame, const TSet<int32>& TriangleROI, bool bViewAligned)
{
	StrokePlane = ComputeStampRegionPlane(GetBrushFrameLocal(), TriangleROI, false, bViewAligned);
}

void UMeshSculptToolBase::UpdateStrokeReferencePlaneFromWorkPlane()
{
	StrokePlane = FFrame3d(
		CurTargetTransform.InverseTransformPosition(GizmoProperties->Position),
		CurTargetTransform.GetRotation().Inverse() * (FQuaterniond)GizmoProperties->Rotation);
}



void UMeshSculptToolBase::InitializeBrushSizeRange(const FAxisAlignedBox3d& TargetBounds)
{
	double MaxDimension = TargetBounds.MaxDim();
	BrushRelativeSizeRange = FInterval1d(MaxDimension * 0.01, MaxDimension);
	CalculateBrushRadius();
}


void UMeshSculptToolBase::CalculateBrushRadius()
{
	CurrentBrushRadius = 0.5 * BrushRelativeSizeRange.Interpolate(BrushProperties->BrushSize);
	if (BrushProperties->bSpecifyRadius)
	{
		CurrentBrushRadius = BrushProperties->BrushRadius;
	}
	else
	{
		BrushProperties->BrushRadius = CurrentBrushRadius;
	}
}


double UMeshSculptToolBase::GetCurrentBrushStrength()
{
	TUniquePtr<FMeshSculptBrushOp>& BrushOp = GetActiveBrushOp();
	if (BrushOp->PropertySet.IsValid())
	{
		return FMathd::Clamp(BrushOp->PropertySet->GetStrength(), 0.0, 1.0);
	}
	return 1.0;
}

double UMeshSculptToolBase::GetCurrentBrushDepth()
{
	TUniquePtr<FMeshSculptBrushOp>& BrushOp = GetActiveBrushOp();
	if (BrushOp->PropertySet.IsValid())
	{
		return FMathd::Clamp(BrushOp->PropertySet->GetDepth(), -1.0, 1.0);
	}
	return 0.0;
}



void UMeshSculptToolBase::IncreaseBrushRadiusAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.025f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UMeshSculptToolBase::DecreaseBrushRadiusAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.025f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UMeshSculptToolBase::IncreaseBrushRadiusSmallStepAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.005f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UMeshSculptToolBase::DecreaseBrushRadiusSmallStepAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.005f, 0.0f, 1.0f);
	CalculateBrushRadius();
}







void UMeshSculptToolBase::UpdateWireframeVisibility(bool bNewValue)
{
	GetSculptMeshComponent()->SetEnableWireframeRenderPass(bNewValue);
}

void UMeshSculptToolBase::UpdateFlatShadingSetting(bool bNewValue)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetScalarParameterValue(TEXT("FlatShading"), (bNewValue) ? 1.0f : 0.0f);
	}
}

void UMeshSculptToolBase::UpdateColorSetting(FLinearColor NewColor)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetVectorParameterValue(TEXT("Color"), NewColor);
	}
}

void UMeshSculptToolBase::UpdateImageSetting(UTexture2D* NewImage)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetTextureParameterValue(TEXT("ImageTexture"), NewImage);
	}
}



void UMeshSculptToolBase::UpdateMaterialMode(EMeshEditingMaterialModes MaterialMode)
{
	if (MaterialMode == EMeshEditingMaterialModes::ExistingMaterial)
	{
		GetSculptMeshComponent()->ClearOverrideRenderMaterial();
		GetSculptMeshComponent()->bCastDynamicShadow = ComponentTarget->GetOwnerComponent()->bCastDynamicShadow;
		ActiveOverrideMaterial = nullptr;
	}
	else
	{
		if (MaterialMode == EMeshEditingMaterialModes::Custom)
		{
			ActiveOverrideMaterial = ToolSetupUtil::GetCustomImageBasedSculptMaterial(GetToolManager(), ViewProperties->Image);
			if (ViewProperties->Image != nullptr)
			{
				ActiveOverrideMaterial->SetTextureParameterValue(TEXT("ImageTexture"), ViewProperties->Image);
			}
		}
		else
		{
			UMaterialInterface* SculptMaterial = nullptr;
			switch (MaterialMode)
			{
			case EMeshEditingMaterialModes::Diffuse:
				SculptMaterial = ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager());
				break;
			case EMeshEditingMaterialModes::Grey:
				SculptMaterial = ToolSetupUtil::GetImageBasedSculptMaterial(GetToolManager(), ToolSetupUtil::ImageMaterialType::DefaultBasic);
				break;
			case EMeshEditingMaterialModes::Soft:
				SculptMaterial = ToolSetupUtil::GetImageBasedSculptMaterial(GetToolManager(), ToolSetupUtil::ImageMaterialType::DefaultSoft);
				break;
			case EMeshEditingMaterialModes::TangentNormal:
				SculptMaterial = ToolSetupUtil::GetImageBasedSculptMaterial(GetToolManager(), ToolSetupUtil::ImageMaterialType::TangentNormalFromView);
				break;
			}
			if (SculptMaterial != nullptr)
			{
				ActiveOverrideMaterial = UMaterialInstanceDynamic::Create(SculptMaterial, this);
			}
		}

		if (ActiveOverrideMaterial != nullptr)
		{
			GetSculptMeshComponent()->SetOverrideRenderMaterial(ActiveOverrideMaterial);
			ActiveOverrideMaterial->SetScalarParameterValue(TEXT("FlatShading"), (ViewProperties->bFlatShading) ? 1.0f : 0.0f);
		}

		GetSculptMeshComponent()->bCastDynamicShadow = false;
	}
}









void UMeshSculptToolBase::InitializeIndicator()
{
	// register and spawn brush indicator gizmo
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(VertexSculptIndicatorGizmoType, NewObject<UBrushStampIndicatorBuilder>());
	BrushIndicator = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UBrushStampIndicator>(VertexSculptIndicatorGizmoType, FString(), this);
	BrushIndicatorMesh = MakeDefaultIndicatorSphereMesh(this, TargetWorld);
	BrushIndicator->AttachedComponent = BrushIndicatorMesh->GetRootComponent();
	BrushIndicator->LineThickness = 1.0;
	BrushIndicator->bDrawIndicatorLines = true;
	BrushIndicator->bDrawRadiusCircle = false;
	BrushIndicator->LineColor = FLinearColor(0.9f, 0.4f, 0.4f);
}


UPreviewMesh* UMeshSculptToolBase::MakeDefaultIndicatorSphereMesh(UObject* Parent, UWorld* World, int Resolution /*= 32*/)
{
	UPreviewMesh* SphereMesh = NewObject<UPreviewMesh>(Parent);
	SphereMesh->CreateInWorld(World, FTransform::Identity);
	FSphereGenerator SphereGen;
	SphereGen.NumPhi = SphereGen.NumTheta = Resolution;
	SphereGen.Generate();
	FDynamicMesh3 Mesh(&SphereGen);
	SphereMesh->UpdatePreview(&Mesh);

	BrushIndicatorMaterial = ToolSetupUtil::GetDefaultBrushVolumeMaterial(GetToolManager());
	if (BrushIndicatorMaterial)
	{
		SphereMesh->SetMaterial(BrushIndicatorMaterial);
	}

	return SphereMesh;
}




void UMeshSculptToolBase::UpdateWorkPlane()
{
	bool bGizmoVisible = ShowWorkPlane() && (GizmoProperties->bShowGizmo);
	UpdateFixedPlaneGizmoVisibility(bGizmoVisible);
	GizmoProperties->bPropertySetEnabled = ShowWorkPlane();

	if (PendingWorkPlaneUpdate != EPendingWorkPlaneUpdate::NoUpdatePending)
	{
		// raycast into scene and current sculpt and place plane at closest hit point
		FRay CursorWorldRay = UMeshSurfacePointTool::LastWorldRay;
		FHitResult Result;
		bool bWorldHit = ToolSceneQueriesUtil::FindNearestVisibleObjectHit(TargetWorld, Result, CursorWorldRay);
		FRay3d LocalRay = GetLocalRay(CursorWorldRay);
		bool bObjectHit = ( FindHitSculptMeshTriangle(LocalRay) != IndexConstants::InvalidID );
		if (bWorldHit && 
			(bObjectHit == false || (CursorWorldRay.GetParameter(Result.ImpactPoint) < CursorWorldRay.GetParameter((FVector)HoverStamp.WorldFrame.Origin))))
		{
			SetFixedSculptPlaneFromWorldPos(Result.ImpactPoint, Result.ImpactNormal, PendingWorkPlaneUpdate);
		}
		else
		{
			SetFixedSculptPlaneFromWorldPos((FVector)HoverStamp.WorldFrame.Origin, (FVector)HoverStamp.WorldFrame.Z(), PendingWorkPlaneUpdate);
		}
		PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::NoUpdatePending;
	}
}


void UMeshSculptToolBase::SetFixedSculptPlaneFromWorldPos(const FVector& Position, const FVector& Normal, EPendingWorkPlaneUpdate UpdateType)
{
	if (UpdateType == EPendingWorkPlaneUpdate::MoveToHitPositionNormal)
	{
		UpdateFixedSculptPlanePosition(Position);
		FFrame3d CurFrame(FVector::ZeroVector, GizmoProperties->Rotation);
		CurFrame.AlignAxis(2, (FVector3d)Normal);
		UpdateFixedSculptPlaneRotation((FQuat)CurFrame.Rotation);
	}
	else if (UpdateType == EPendingWorkPlaneUpdate::MoveToHitPositionViewAligned)
	{
		UpdateFixedSculptPlanePosition(Position);
		FFrame3d CurFrame(FVector::ZeroVector, GizmoProperties->Rotation);
		CurFrame.AlignAxis(2, -(FVector3d)CameraState.Forward());
		UpdateFixedSculptPlaneRotation((FQuat)CurFrame.Rotation);
	}
	else
	{
		UpdateFixedSculptPlanePosition(Position);
	}

	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
	}
}

void UMeshSculptToolBase::PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	UpdateFixedSculptPlaneRotation(Transform.GetRotation());
	UpdateFixedSculptPlanePosition(Transform.GetLocation());
}

void UMeshSculptToolBase::UpdateFixedSculptPlanePosition(const FVector& Position)
{
	GizmoProperties->Position = Position;
	GizmoPositionWatcher.SilentUpdate();
}

void UMeshSculptToolBase::UpdateFixedSculptPlaneRotation(const FQuat& Rotation)
{
	GizmoProperties->Rotation = Rotation;
	GizmoRotationWatcher.SilentUpdate();
}

void UMeshSculptToolBase::UpdateGizmoFromProperties()
{
	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
	}
}

void UMeshSculptToolBase::UpdateFixedPlaneGizmoVisibility(bool bVisible)
{
	if (bVisible == false)
	{
		if (PlaneTransformGizmo != nullptr)
		{
			GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(PlaneTransformGizmo);
			PlaneTransformGizmo = nullptr;
		}
	}
	else
	{
		if (PlaneTransformGizmo == nullptr)
		{
			PlaneTransformGizmo = GetToolManager()->GetPairedGizmoManager()->CreateCustomTransformGizmo(
				ETransformGizmoSubElements::StandardTranslateRotate, this);
			PlaneTransformGizmo->bUseContextCoordinateSystem = false;
			PlaneTransformGizmo->CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;
			PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy, GetToolManager());
			PlaneTransformGizmo->ReinitializeGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
		}

		PlaneTransformGizmo->bSnapToWorldGrid = GizmoProperties->bSnapToGrid;
	}
}







void UMeshSculptToolBase::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::IncreaseBrushSize,
		TEXT("SculptIncreaseRadius"),
		LOCTEXT("SculptIncreaseRadius", "Increase Sculpt Radius"),
		LOCTEXT("SculptIncreaseRadiusTooltip", "Increase radius of sculpting brush"),
		EModifierKey::None, EKeys::RightBracket,
		[this]() { IncreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::DecreaseBrushSize,
		TEXT("SculptDecreaseRadius"),
		LOCTEXT("SculptDecreaseRadius", "Decrease Sculpt Radius"),
		LOCTEXT("SculptDecreaseRadiusTooltip", "Decrease radius of sculpting brush"),
		EModifierKey::None, EKeys::LeftBracket,
		[this]() { DecreaseBrushRadiusAction(); });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
		TEXT("NextBrushMode"),
		LOCTEXT("SculptNextBrushMode", "Next Brush Type"),
		LOCTEXT("SculptNextBrushModeTooltip", "Cycle to next Brush Type"),
		EModifierKey::None, EKeys::A,
		[this]() { NextBrushModeAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
		TEXT("PreviousBrushMode"),
		LOCTEXT("SculptPreviousBrushMode", "Previous Brush Type"),
		LOCTEXT("SculptPreviousBrushModeTooltip", "Cycle to previous Brush Type"),
		EModifierKey::None, EKeys::Q,
		[this]() { PreviousBrushModeAction(); });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 60,
		TEXT("SculptIncreaseSpeed"),
		LOCTEXT("SculptIncreaseSpeed", "Increase Speed"),
		LOCTEXT("SculptIncreaseSpeedTooltip", "Increase Brush Speed"),
		EModifierKey::None, EKeys::E,
		[this]() { IncreaseBrushSpeedAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 61,
		TEXT("SculptDecreaseSpeed"),
		LOCTEXT("SculptDecreaseSpeed", "Decrease Speed"),
		LOCTEXT("SculptDecreaseSpeedTooltip", "Decrease Brush Speed"),
		EModifierKey::None, EKeys::W,
		[this]() { DecreaseBrushSpeedAction(); });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 50,
		TEXT("SculptIncreaseSize"),
		LOCTEXT("SculptIncreaseSize", "Increase Size"),
		LOCTEXT("SculptIncreaseSizeTooltip", "Increase Brush Size"),
		EModifierKey::None, EKeys::D,
		[this]() { IncreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 51,
		TEXT("SculptDecreaseSize"),
		LOCTEXT("SculptDecreaseSize", "Decrease Size"),
		LOCTEXT("SculptDecreaseSizeTooltip", "Decrease Brush Size"),
		EModifierKey::None, EKeys::S,
		[this]() { DecreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 52,
		TEXT("SculptIncreaseSizeSmallStep"),
		LOCTEXT("SculptIncreaseSize", "Increase Size"),
		LOCTEXT("SculptIncreaseSizeTooltip", "Increase Brush Size"),
		EModifierKey::Shift, EKeys::D,
		[this]() { IncreaseBrushRadiusSmallStepAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 53,
		TEXT("SculptDecreaseSizeSmallStemp"),
		LOCTEXT("SculptDecreaseSize", "Decrease Size"),
		LOCTEXT("SculptDecreaseSizeTooltip", "Decrease Brush Size"),
		EModifierKey::Shift, EKeys::S,
		[this]() { DecreaseBrushRadiusSmallStepAction(); });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::ToggleWireframe,
		TEXT("ToggleWireframe"),
		LOCTEXT("ToggleWireframe", "Toggle Wireframe"),
		LOCTEXT("ToggleWireframeTooltip", "Toggle visibility of wireframe overlay"),
		EModifierKey::Alt, EKeys::W,
		[this]() { ViewProperties->bShowWireframe = !ViewProperties->bShowWireframe; });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 100,
		TEXT("SetSculptWorkSurfacePosNormal"),
		LOCTEXT("SetSculptWorkSurfacePosNormal", "Reorient Work Surface"),
		LOCTEXT("SetSculptWorkSurfacePosNormalTooltip", "Move the Sculpting Work Plane/Surface to Position and Normal of World hit point under cursor"),
		EModifierKey::Shift, EKeys::T,
		[this]() { PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::MoveToHitPositionNormal; });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 101,
		TEXT("SetSculptWorkSurfacePos"),
		LOCTEXT("SetSculptWorkSurfacePos", "Reposition Work Surface"),
		LOCTEXT("SetSculptWorkSurfacePosTooltip", "Move the Sculpting Work Plane/Surface to World hit point under cursor (keep current Orientation)"),
		EModifierKey::None, EKeys::T,
		[this]() { PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::MoveToHitPosition; });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 102,
		TEXT("SetSculptWorkSurfaceView"),
		LOCTEXT("SetSculptWorkSurfaceView", "View-Align Work Surface"),
		LOCTEXT("SetSculptWorkSurfaceViewTooltip", "Move the Sculpting Work Plane/Surface to World hit point under cursor and align to View"),
		EModifierKey::Control | EModifierKey::Shift, EKeys::T,
		[this]() { PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::MoveToHitPositionViewAligned; });
}


#undef LOCTEXT_NAMESPACE

