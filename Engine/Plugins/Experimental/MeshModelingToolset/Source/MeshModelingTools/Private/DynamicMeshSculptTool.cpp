// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshSculptTool.h"
#include "Containers/Map.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"

#include "SubRegionRemesher.h"
#include "ProjectionTargets.h"
#include "MeshConstraints.h"
#include "MeshConstraintsUtil.h"
#include "MeshWeights.h"
#include "MeshNormals.h"
#include "MeshIndexUtil.h"
#include "Drawing/MeshDebugDrawing.h"
#include "PreviewMesh.h"
#include "ToolSetupUtil.h"
#include "ToolSceneQueriesUtil.h"

#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "DynamicMeshChangeTracker.h"

#include "ToolDataVisualizer.h"
#include "Components/PrimitiveComponent.h"
#include "Generators/SphereGenerator.h"

#include "Sculpting/KelvinletBrushOp.h"

#include "InteractiveGizmoManager.h"
#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"
#include "UObject/ObjectMacros.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "UDynamicMeshSculptTool"



/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UDynamicMeshSculptToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UDynamicMeshSculptTool* SculptTool = NewObject<UDynamicMeshSculptTool>(SceneState.ToolManager);
	SculptTool->SetEnableRemeshing(this->bEnableRemeshing);
	SculptTool->SetWorld(SceneState.World);
	return SculptTool;
}


/*
 * Tool
 */
UDynamicMeshSculptTool::UDynamicMeshSculptTool()
{
	// initialize parameters
	bEnableRemeshing = true;
}

void UDynamicMeshSculptTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

namespace
{
const FString BrushIndicatorGizmoType = TEXT("BrushIndicatorGizmoType");
}

void UDynamicMeshSculptTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<UOctreeDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMeshSculptToolMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();

	// initialize from LOD-0 MeshDescription
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());

	// transform mesh to world space because handling scaling inside brush is a mess
	// Note: this transform does not include translation ( so only the 3x3 transform)
	InitialTargetTransform = FTransform3d(ComponentTarget->GetWorldTransform());
	// clamp scaling because if we allow zero-scale we cannot invert this transform on Accept
	InitialTargetTransform.ClampMinimumScale(0.01);
	FVector3d Translation = InitialTargetTransform.GetTranslation();
	InitialTargetTransform.SetTranslation(FVector3d::Zero());
	DynamicMeshComponent->ApplyTransform(InitialTargetTransform, false);
	// since we moved to World coords there is not a current transform anymore.
	CurTargetTransform = FTransform3d(Translation);
	DynamicMeshComponent->SetWorldTransform((FTransform)CurTargetTransform);

	// copy material if there is one
	UMaterialInterface* Material = ComponentTarget->GetMaterial(0);
	if (Material != nullptr)
	{
		DynamicMeshComponent->SetMaterial(0, Material);
	}

	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshChanged.Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UDynamicMeshSculptTool::OnDynamicMeshComponentChanged));

	// do we always want to keep vertex normals updated? Perhaps should discard vertex normals before baking?
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FMeshNormals::QuickComputeVertexNormals(*Mesh);

	// switch to vertex normals for testing
	//DynamicMeshComponent->GetMesh()->DiscardAttributes();

	// initialize target mesh
	UpdateTarget();
	bTargetDirty = false;

	// initialize brush radius range interval, brush properties
	double MaxDimension = DynamicMeshComponent->GetMesh()->GetCachedBounds().MaxDim();
	BrushRelativeSizeRange = FInterval1d(MaxDimension*0.01, MaxDimension);
	BrushProperties = NewObject<USculptBrushProperties>(this);
	BrushProperties->bShowStrength = false;
	CalculateBrushRadius();

	// initialize other properties
	SculptProperties = NewObject<UBrushSculptProperties>(this);
	KelvinBrushProperties = NewObject<UKelvinBrushProperties>(this);

	RemeshProperties = NewObject<UBrushRemeshProperties>(this);
	RemeshProperties->RestoreProperties(this);

	InitialEdgeLength = EstimateIntialSafeTargetLength(*Mesh, 5000);

	// hide input Component
	ComponentTarget->SetOwnerVisibility(false);

	// init state flags flags
	bInDrag = false;
	bHaveRemeshed = false;
	bRemeshPending = false;
	bStampPending = false;
	ActiveVertexChange = nullptr;

	// register and spawn brush indicator gizmo
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(BrushIndicatorGizmoType, NewObject<UBrushStampIndicatorBuilder>());
	BrushIndicator = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UBrushStampIndicator>(BrushIndicatorGizmoType, FString(), this);
	BrushIndicatorMesh = MakeDefaultSphereMesh(this, TargetWorld);
	BrushIndicator->AttachedComponent = BrushIndicatorMesh->GetRootComponent();
	BrushIndicator->LineThickness = 1.0;
	BrushIndicator->bDrawIndicatorLines = true;
	BrushIndicator->bDrawRadiusCircle = false;
	BrushIndicator->bDrawFalloffCircle = true;
	BrushIndicator->LineColor = FLinearColor(0.9f, 0.4f, 0.4f);

	// initialize our properties
	AddToolPropertySource(BrushProperties);
	AddToolPropertySource(SculptProperties);

	// add brush-specific properties 
	PlaneBrushProperties = NewObject<UPlaneBrushProperties>(this);
	PlaneBrushProperties->RestoreProperties(this);
	AddToolPropertySource(PlaneBrushProperties);

	SculptMaxBrushProperties = NewObject<USculptMaxBrushProperties>();
	SculptMaxBrushProperties->RestoreProperties(this);
	AddToolPropertySource(SculptMaxBrushProperties);

	AddToolPropertySource(KelvinBrushProperties);
	KelvinBrushProperties->RestoreProperties(this);

	GizmoProperties = NewObject<UFixedPlaneBrushProperties>();
	GizmoProperties->RestoreProperties(this);
	AddToolPropertySource(GizmoProperties);

	if (this->bEnableRemeshing)
	{
		SculptProperties->bIsRemeshingEnabled = true;
		AddToolPropertySource(RemeshProperties);
	}

	BrushProperties->RestoreProperties(this);
	CalculateBrushRadius();
	SculptProperties->RestoreProperties(this);

	// disable tool-specific properties
	SetToolPropertySourceEnabled(PlaneBrushProperties, false);
	SetToolPropertySourceEnabled(GizmoProperties, false);
	SetToolPropertySourceEnabled(SculptMaxBrushProperties, false);
	SetToolPropertySourceEnabled(KelvinBrushProperties, false);

	ViewProperties = NewObject<UMeshEditingViewProperties>();
	ViewProperties->RestoreProperties(this);
	AddToolPropertySource(ViewProperties);

	// register watchers
	ShowWireframeWatcher.Initialize(
		[this]() { return ViewProperties->bShowWireframe; },
		[this](bool bNewValue) { DynamicMeshComponent->bExplicitShowWireframe = bNewValue; }, ViewProperties->bShowWireframe);
	MaterialModeWatcher.Initialize(
		[this]() { return ViewProperties->MaterialMode; },
		[this](EMeshEditingMaterialModes NewMode) { UpdateMaterialMode(NewMode); }, EMeshEditingMaterialModes::ExistingMaterial);
	FlatShadingWatcher.Initialize(
		[this]() { return ViewProperties->bFlatShading; },
		[this](bool bNewValue) { UpdateFlatShadingSetting(bNewValue); }, ViewProperties->bFlatShading);
	ColorWatcher.Initialize(
		[this]() { return ViewProperties->Color; },
		[this](FLinearColor NewColor) { UpdateColorSetting(NewColor); }, ViewProperties->Color);
	ImageWatcher.Initialize(
		[this]() { return ViewProperties->Image; },
		[this](UTexture2D* NewImage) { UpdateImageSetting(NewImage); }, ViewProperties->Image);
	BrushTypeWatcher.Initialize(
		[this]() { return SculptProperties->PrimaryBrushType; },
		[this](EDynamicMeshSculptBrushType NewBrushType) { UpdateBrushType(NewBrushType); }, SculptProperties->PrimaryBrushType);
	GizmoPositionWatcher.Initialize(
		[this]() { return GizmoProperties->Position; },
		[this](FVector NewPosition) { UpdateGizmoFromProperties(); }, GizmoProperties->Position);
	GizmoRotationWatcher.Initialize(
		[this]() { return GizmoProperties->Rotation; },
		[this](FQuat NewRotation) { UpdateGizmoFromProperties(); }, GizmoProperties->Rotation);


	// create proxy for plane gizmo, but not gizmo itself, as it only appears in FixedPlane brush mode
	// listen for changes to the proxy and update the plane when that happens
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UDynamicMeshSculptTool::PlaneTransformChanged);

	if (bEnableRemeshing)
	{
		PrecomputeRemeshInfo();
		if (bHaveUVSeams)
		{
			GetToolManager()->DisplayMessage(
				LOCTEXT("UVSeamWarning", "This mesh has UV seams which may limit remeshing. Consider clearing the UV layers using the Remesh Tool."),
				EToolMessageLevel::UserWarning);
		}
		else if (bHaveNormalSeams)
		{
			GetToolManager()->DisplayMessage(
				LOCTEXT("NormalSeamWarning", "This mesh has Hard Normal seams which may limit remeshing. Consider clearing Hard Normals using the Remesh Tool."),
				EToolMessageLevel::UserWarning);
		}
	}

	UpdateBrushType(SculptProperties->PrimaryBrushType);
}

void UDynamicMeshSculptTool::Shutdown(EToolShutdownType ShutdownType)
{
	BrushIndicatorMesh->Disconnect();
	BrushIndicatorMesh = nullptr;

	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);
	BrushIndicator = nullptr;
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(BrushIndicatorGizmoType);

	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// safe to do this here because we are about to destroy componeont
			DynamicMeshComponent->ApplyTransform(InitialTargetTransform, true);

			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("SculptMeshToolTransactionName", "Sculpt Mesh"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, bHaveRemeshed);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}

	BrushProperties->SaveProperties(this);
	SculptProperties->SaveProperties(this);
	KelvinBrushProperties->SaveProperties(this);
	ViewProperties->SaveProperties(this);
	GizmoProperties->SaveProperties(this);
	PlaneBrushProperties->SaveProperties(this);
	SculptMaxBrushProperties->SaveProperties(this);
	RemeshProperties->SaveProperties(this);
}

void UDynamicMeshSculptTool::OnDynamicMeshComponentChanged()
{
	bNormalUpdatePending = true;
	bTargetDirty = true;
}

void UDynamicMeshSculptTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	CalculateBrushRadius();
}

bool UDynamicMeshSculptTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	FRay3d LocalRay(CurTargetTransform.InverseTransformPosition(Ray.Origin),
		CurTargetTransform.InverseTransformVector(Ray.Direction));
	LocalRay.Direction.Normalize();
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	int HitTID = FindHitSculptMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		FTriangle3d Triangle;
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

void UDynamicMeshSculptTool::OnBeginDrag(const FRay& Ray)
{
	bSmoothing = GetShiftToggle();
	bInvert = GetCtrlToggle();

	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		BrushStartCenterWorld = Ray.PointAt(OutHit.Distance) + BrushProperties->Depth*CurrentBrushRadius*Ray.Direction;

		bInDrag = true;

		ActiveDragPlane = FFrame3d(BrushStartCenterWorld, -Ray.Direction);
		ActiveDragPlane.RayPlaneIntersection(Ray.Origin, Ray.Direction, 2, LastHitPosWorld);

		LastBrushPosWorld = LastHitPosWorld;
		LastBrushPosNormalWorld = ActiveDragPlane.Z();
		LastBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastHitPosWorld);
		LastSmoothBrushPosLocal = LastBrushPosLocal;

		BeginChange(bEnableRemeshing == false);

		UpdateROI(LastBrushPosLocal);

		if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::Plane)
		{
			ActiveFixedBrushPlane = ComputeROIBrushPlane(LastBrushPosLocal, false, false);
		}
		else if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::PlaneViewAligned)
		{
			AlignBrushToView();
			ActiveFixedBrushPlane = ComputeROIBrushPlane(LastBrushPosLocal, false, true);
		}

		// apply initial stamp
		PendingStampRay = Ray;
		bStampPending = true;
	}
}

void UDynamicMeshSculptTool::UpdateROI(const FVector3d& BrushPos)
{
	SCOPE_CYCLE_COUNTER(SculptTool_UpdateROI);

	// TODO: need dynamic vertex hash table!

	float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;

	FAxisAlignedBox3d BrushBox(
		BrushPos - CurrentBrushRadius * FVector3d::One(),
		BrushPos + CurrentBrushRadius * FVector3d::One());

	VertexSetBuffer.Reset();
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();
	Octree->RangeQuery(BrushBox,
		[&](int TriIdx) {
		FIndex3i TriV = Mesh->GetTriangle(TriIdx);
		for (int j = 0; j < 3; ++j)
		{
			FVector3d Position = Mesh->GetVertex(TriV[j]);
			if ((Position - BrushPos).SquaredLength() < RadiusSqr)
			{
				VertexSetBuffer.Add(TriV[j]);
			}
		}

	});

	VertexROI.SetNum(0, false);
	BufferUtil::AppendElements(VertexROI, VertexSetBuffer);

	TriangleROI.Reset();
	MeshIndexUtil::VertexToTriangleOneRing(Mesh, VertexROI, TriangleROI);
}

void UDynamicMeshSculptTool::OnUpdateDrag(const FRay& WorldRay)
{
	if (bInDrag)
	{
		PendingStampRay = WorldRay;
		bStampPending = true;
	}
}

void UDynamicMeshSculptTool::CalculateBrushRadius()
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

void UDynamicMeshSculptTool::ApplyStamp(const FRay& WorldRay)
{
	SCOPE_CYCLE_COUNTER(STAT_SculptToolApplyStamp);

	// update brush type history. apologies for convoluted logic.
	StampTimestamp++;
	if (LastStampType != PendingStampType)
	{
		if (BrushTypeHistoryIndex != BrushTypeHistory.Num() - 1)
		{
			if (LastStampType != EDynamicMeshSculptBrushType::LastValue)
			{
				BrushTypeHistory.Add(LastStampType);
			}
			BrushTypeHistoryIndex = BrushTypeHistory.Num()-1;
		}
		LastStampType = PendingStampType;
		if (BrushTypeHistory.Num() == 0 || BrushTypeHistory[BrushTypeHistory.Num()-1] != PendingStampType)
		{
			BrushTypeHistory.Add(PendingStampType);
			BrushTypeHistoryIndex = BrushTypeHistory.Num() - 1;
		}
	}

	CalculateBrushRadius();

	SaveActiveROI();

	if (bSmoothing)
	{
		ApplySmoothBrush(WorldRay);
		return;
	}

	switch (SculptProperties->PrimaryBrushType)
	{
		case EDynamicMeshSculptBrushType::Offset:
			ApplyOffsetBrush(WorldRay, false);
			break;
		case EDynamicMeshSculptBrushType::SculptView:
			ApplyOffsetBrush(WorldRay, true);
			break;
		case EDynamicMeshSculptBrushType::SculptMax:
			ApplySculptMaxBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Move:
			ApplyMoveBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::PullKelvin:
			ApplyPullKelvinBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::PullSharpKelvin:
			ApplyPullSharpKelvinBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Smooth:
			ApplySmoothBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Pinch:
			ApplyPinchBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::TwistKelvin:
			ApplyTwistKelvinBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Inflate:
			ApplyInflateBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::ScaleKelvin:
			ApplyScaleKelvinBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Flatten:
			ApplyFlattenBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Plane:
			ApplyPlaneBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::PlaneViewAligned:
			ApplyPlaneBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::FixedPlane:
			ApplyFixedPlaneBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Resample:
			ApplyResampleBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::LastValue:
			break;
	}
}

double UDynamicMeshSculptTool::CalculateBrushFalloff(double Distance)
{
	double f = FMathd::Clamp(1.0 - BrushProperties->BrushFalloffAmount, 0.0, 1.0);
	double d = Distance / CurrentBrushRadius;
	double w = 1;
	if (d > f)
	{
		d = FMathd::Clamp((d - f) / (1.0 - f), 0.0, 1.0);
		w = (1.0 - d * d);
		w = w * w * w;
	}
	return w;
}



void UDynamicMeshSculptTool::SyncMeshWithPositionBuffer(FDynamicMesh3* Mesh)
{
	const int NumV = ROIPositionBuffer.Num();
	checkSlow(VertexROI.Num() <= NumV);

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}
}

void UDynamicMeshSculptTool::ApplySmoothBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay, true);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal](int k)
	{
		int VertIdx = VertexROI[k];

		FVector3d OrigPos = Mesh->GetVertex(VertIdx);

		double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));

		FVector3d SmoothedPos = (SculptProperties->bPreserveUVFlow) ?
			FMeshWeights::MeanValueCentroid(*Mesh, VertIdx) : FMeshWeights::UniformCentroid(*Mesh, VertIdx);

		FVector3d NewPos = FVector3d::Lerp(OrigPos, SmoothedPos, Falloff*SculptProperties->SmoothBrushSpeed);

		ROIPositionBuffer[k] = NewPos;
	});

	SyncMeshWithPositionBuffer(Mesh);

	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyMoveBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnActivePlane(WorldRay);

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d MoveVec = NewBrushPosLocal - LastBrushPosLocal;

	if (MoveVec.SquaredLength() <= 0)
	{
		LastBrushPosLocal = NewBrushPosLocal;
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, MoveVec](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);

		double PrevDist = (OrigPos - LastBrushPosLocal).Length();
		double NewDist = (OrigPos - NewBrushPosLocal).Length();
		double UseDist = FMath::Min(PrevDist, NewDist);

		double Falloff = CalculateBrushFalloff(UseDist) * ActivePressure;

		FVector3d NewPos = OrigPos + Falloff * MoveVec;
		ROIPositionBuffer[k] = NewPos;
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);

	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyOffsetBrush(const FRay& WorldRay, bool bUseViewDirection)
{
	UpdateBrushPositionOnTargetMesh(WorldRay, bUseViewDirection);
	if (bUseViewDirection)
	{
		AlignBrushToView();
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d LocalNormal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = 0.5 * Direction * FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->PrimaryBrushSpeed) * ActivePressure;
	double MaxOffset = CurrentBrushRadius;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [&](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);

		FVector3d BasePos, BaseNormal;
		if (GetTargetMeshNearest(OrigPos, (double)(4 * CurrentBrushRadius), BasePos, BaseNormal) == false)
		{
			ROIPositionBuffer[k] = OrigPos;
		}
		else
		{
			FVector3d MoveVec = (bUseViewDirection) ?  (UseSpeed*LocalNormal) : (UseSpeed*BaseNormal);
			double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
			FVector3d NewPos = OrigPos + Falloff * MoveVec;
			ROIPositionBuffer[k] = NewPos;
		}
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);

	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplySculptMaxBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnTargetMesh(WorldRay, true);
	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->PrimaryBrushSpeed) * ActivePressure;

	double MaxOffset = CurrentBrushRadius * SculptMaxBrushProperties->MaxHeight;
	if (SculptMaxBrushProperties->bFreezeCurrentHeight && SculptMaxFixedHeight >= 0)
	{
		MaxOffset = SculptMaxFixedHeight;
	}
	SculptMaxFixedHeight = MaxOffset;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, UseSpeed, MaxOffset](int k)
	{
		int VertIdx = VertexROI[k];

		FVector3d OrigPos = Mesh->GetVertex(VertIdx);

		FVector3d BasePos, BaseNormal;
		if (GetTargetMeshNearest(OrigPos, (double)(2 * CurrentBrushRadius), BasePos, BaseNormal) == false)
		{
			ROIPositionBuffer[k] = OrigPos;
		}
		else
		{
			FVector3d MoveVec = UseSpeed * BaseNormal;
			double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
			FVector3d NewPos = OrigPos + Falloff * MoveVec;

			FVector3d DeltaPos = NewPos - BasePos;
			if (DeltaPos.SquaredLength() > MaxOffset*MaxOffset)
			{
				DeltaPos.Normalize();
				NewPos = BasePos + MaxOffset * DeltaPos;
			}
			ROIPositionBuffer[k] = NewPos;
		}
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);

	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyPinchBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnTargetMesh(WorldRay, true);
	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	FVector3d OffsetBrushPosLocal = NewBrushPosLocal - BrushProperties->Depth * CurrentBrushRadius * BrushNormalLocal;

	// hardcoded lazybrush...
	FVector3d NewSmoothBrushPosLocal = (0.75f)*LastSmoothBrushPosLocal + (0.25f)*NewBrushPosLocal;

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->PrimaryBrushSpeed*0.05) * ActivePressure;

	FVector3d MotionVec = NewSmoothBrushPosLocal - LastSmoothBrushPosLocal;
	bool bHaveMotion = (MotionVec.Length() > FMathf::ZeroTolerance);
	MotionVec.Normalize();
	FLine3d MoveLine(LastSmoothBrushPosLocal, MotionVec);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, OffsetBrushPosLocal, bHaveMotion, MotionVec, UseSpeed](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d Delta = OffsetBrushPosLocal - OrigPos;

		FVector3d MoveVec = UseSpeed * Delta;

		// pinch uses 1/x falloff, shifted so that
		double Distance = OrigPos.Distance(NewBrushPosLocal);
		double NormalizedDistance = Distance / CurrentBrushRadius + FMathf::ZeroTolerance;
		double Falloff = (1.0/NormalizedDistance) - 1.0;
		Falloff = FMathd::Clamp(Falloff, 0.0, 1.0);

		if (bHaveMotion && Falloff < 0.8f)
		{
			double AnglePower = 1.0 - FMathd::Abs(MoveVec.Normalized().Dot(MotionVec));
			Falloff *= AnglePower;
		}

		FVector3d NewPos = OrigPos + Falloff * MoveVec;
		ROIPositionBuffer[k] = NewPos;
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);


	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
	LastSmoothBrushPosLocal = NewSmoothBrushPosLocal;
}

FFrame3d UDynamicMeshSculptTool::ComputeROIBrushPlane(const FVector3d& BrushCenter, bool bIgnoreDepth, bool bViewAligned)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FVector3d AverageNormal(0, 0, 0);
	FVector3d AveragePos(0, 0, 0);
	double WeightSum = 0;
	for (int TriID : TriangleROI)
	{
		FVector3d Centroid = Mesh->GetTriCentroid(TriID);
		double Weight = CalculateBrushFalloff(BrushCenter.Distance(Centroid));

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
		Result.Origin -= BrushProperties->Depth * CurrentBrushRadius * Result.Z();
	}

	return Result;
}

void UDynamicMeshSculptTool::ApplyPlaneBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay, true);
	if (bHit == false)
	{
		return;
	}

	static const double PlaneSigns[3] = { 0, -1, 1 };
	double PlaneSign = PlaneSigns[(int32)PlaneBrushProperties->WhichSide];

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	double UseSpeed = FMathd::Sqrt(CurrentBrushRadius) * FMathd::Sqrt(SculptProperties->PrimaryBrushSpeed) * 0.05 * ActivePressure;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [&](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d PlanePos = ActiveFixedBrushPlane.ToPlane(OrigPos, 2);
		FVector3d Delta = PlanePos - OrigPos;
		double Dot = Delta.Dot(ActiveFixedBrushPlane.Z());
		FVector3d NewPos = OrigPos;
		if (Dot * PlaneSign >= 0)
		{
			FVector3d MoveVec = UseSpeed * Delta;
			double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
			NewPos = OrigPos + Falloff * MoveVec;
		}
		ROIPositionBuffer[k] = NewPos;
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);

	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyFixedPlaneBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay, true);
	if (bHit == false)
	{
		return;
	}

	static const double PlaneSigns[3] = { 0, -1, 1 };
	double PlaneSign = PlaneSigns[(int32)PlaneBrushProperties->WhichSide];

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	double UseSpeed = CurrentBrushRadius * FMathd::Sqrt(SculptProperties->PrimaryBrushSpeed) * 0.1 * ActivePressure;

	FFrame3d FixedPlaneLocal(
		CurTargetTransform.InverseTransformPosition(GizmoProperties->Position),
		CurTargetTransform.GetRotation().Inverse() * (FQuaterniond)GizmoProperties->Rotation);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [&](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d PlanePos = FixedPlaneLocal.ToPlane(OrigPos, 2);
		FVector3d Delta = PlanePos - OrigPos;
		double Dot = Delta.Dot(FixedPlaneLocal.Z());
		FVector3d NewPos = OrigPos;
		if (Dot * PlaneSign >= 0)
		{
			double MaxDist = Delta.Normalize();
			double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
			FVector3d MoveVec = Falloff * UseSpeed * Delta;
			NewPos = (MoveVec.SquaredLength() > MaxDist* MaxDist) ?
				PlanePos : OrigPos + Falloff * MoveVec;
		}
		ROIPositionBuffer[k] = NewPos;
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);

	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyFlattenBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay, true);
	if (bHit == false)
	{
		return;
	}

	static const double PlaneSigns[3] = { 0, -1, 1 };
	double PlaneSign = PlaneSigns[(int32)PlaneBrushProperties->WhichSide];

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);

	double UseSpeed = FMathd::Sqrt(CurrentBrushRadius) * FMathd::Sqrt(SculptProperties->PrimaryBrushSpeed) * 0.05 * ActivePressure;
	FFrame3d StampFlattenPlane = ComputeROIBrushPlane(NewBrushPosLocal, true, false);
	//StampFlattenPlane.Origin -= BrushProperties->Depth * CurrentBrushRadius * StampFlattenPlane.Z();

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [&](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d PlanePos = StampFlattenPlane.ToPlane(OrigPos, 2);
		FVector3d Delta = PlanePos - OrigPos;

		double Dot = Delta.Dot(StampFlattenPlane.Z());
		FVector3d NewPos = OrigPos;
		if (Dot * PlaneSign >= 0)
		{
			double MaxDist = Delta.Normalize();
			double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
			FVector3d MoveVec = Falloff * UseSpeed * Delta;
			NewPos = (MoveVec.SquaredLength() > MaxDist*MaxDist) ?
				PlanePos : OrigPos + Falloff * MoveVec;
		}

		ROIPositionBuffer[k] = NewPos;
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);

	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyInflateBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay, true);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * CurrentBrushRadius * SculptProperties->PrimaryBrushSpeed * 0.05 * ActivePressure;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	// calculate vertex normals
	ParallelFor(VertexROI.Num(), [this, Mesh](int Index) {
		int VertIdx = VertexROI[Index];
		FVector3d Normal = FMeshNormals::ComputeVertexNormal(*Mesh, VertIdx);
		Mesh->SetVertexNormal(VertIdx, (FVector3f)Normal);
	});


	ParallelFor(VertexROI.Num(), [this, Mesh, UseSpeed, NewBrushPosLocal](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d Normal = (FVector3d)Mesh->GetVertexNormal(VertIdx);

		FVector3d MoveVec = UseSpeed * Normal;

		double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));

		FVector3d NewPos = OrigPos + Falloff*MoveVec;
		ROIPositionBuffer[k] = NewPos;
	});

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);
	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}


void UDynamicMeshSculptTool::ApplyResampleBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnTargetMesh(WorldRay, true);
	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d LocalNormal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);

	double UseSpeed = FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->PrimaryBrushSpeed) * ActivePressure;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);
	ParallelFor(NumV, [&](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);

		FVector3d BasePos, BaseNormal;
		if (GetTargetMeshNearest(OrigPos, (double)(4 * CurrentBrushRadius), BasePos, BaseNormal) == false)
		{
			ROIPositionBuffer[k] = OrigPos;
		}
		else
		{
			double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
			FVector3d NewPos = BasePos;// FVector3d::Lerp(OrigPos, BasePos, Falloff);
			ROIPositionBuffer[k] = NewPos;
		}
	});

	SyncMeshWithPositionBuffer(Mesh);
	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}


void UDynamicMeshSculptTool::ApplyPullKelvinBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnActivePlane(WorldRay);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d MoveVec = NewBrushPosLocal - LastBrushPosLocal;

	if (MoveVec.SquaredLength() <= 0)
	{
		LastBrushPosLocal = NewBrushPosLocal;
		return;
	}
	
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FKelvinletBrushOp KelvinBrushOp(*Mesh);

	const EKelvinletBrushMode KelvinMode = EKelvinletBrushMode::PullKelvinlet;

	FKelvinletBrushOp::FKelvinletBrushOpProperties  KelvinletBrushOpProperties(KelvinMode, *KelvinBrushProperties, *BrushProperties);
	KelvinletBrushOpProperties.Direction = FVector(MoveVec.X, MoveVec.Y, MoveVec.Z);  //FVector(BrushNormalLocal.X, BrushNormalLocal.Y, BrushNormalLocal.Z);
	KelvinletBrushOpProperties.Size *= 0.6;
	

	FMatrix ToBrush; ToBrush.SetIdentity();  ToBrush.SetOrigin(-FVector(NewBrushPosLocal.X, NewBrushPosLocal.Y, NewBrushPosLocal.Z)); //  ToBrush.

	KelvinBrushOp.ApplyBrush(KelvinletBrushOpProperties, ToBrush, VertexROI, ROIPositionBuffer);

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);
	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}


void UDynamicMeshSculptTool::ApplyPullSharpKelvinBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnActivePlane(WorldRay);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d MoveVec = NewBrushPosLocal - LastBrushPosLocal;

	if (MoveVec.SquaredLength() <= 0)
	{
		LastBrushPosLocal = NewBrushPosLocal;
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FKelvinletBrushOp KelvinBrushOp(*Mesh);

	const EKelvinletBrushMode KelvinMode = EKelvinletBrushMode::SharpPullKelvinlet;

	FKelvinletBrushOp::FKelvinletBrushOpProperties  KelvinletBrushOpProperties(KelvinMode, *KelvinBrushProperties, *BrushProperties);
	KelvinletBrushOpProperties.Direction = FVector(MoveVec.X, MoveVec.Y, MoveVec.Z);  //FVector(BrushNormalLocal.X, BrushNormalLocal.Y, BrushNormalLocal.Z);
	KelvinletBrushOpProperties.Size *= 0.6;


	FMatrix ToBrush; ToBrush.SetIdentity();  ToBrush.SetOrigin(-FVector(NewBrushPosLocal.X, NewBrushPosLocal.Y, NewBrushPosLocal.Z)); //  ToBrush.

	KelvinBrushOp.ApplyBrush(KelvinletBrushOpProperties, ToBrush, VertexROI, ROIPositionBuffer);

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);
	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyTwistKelvinBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnTargetMesh(WorldRay, true);
	
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	


	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed  = Direction * FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->PrimaryBrushSpeed) * ActivePressure;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FKelvinletBrushOp KelvinBrushOp(*Mesh);


	FKelvinletBrushOp::FKelvinletBrushOpProperties  KelvinletBrushOpProperties(EKelvinletBrushMode::TwistKelvinlet, *KelvinBrushProperties, *BrushProperties);
	KelvinletBrushOpProperties.Direction = UseSpeed * FVector(BrushNormalLocal.X, BrushNormalLocal.Y, BrushNormalLocal.Z); // twist about local normal
	KelvinletBrushOpProperties.Size *= 0.35; // reduce the core size of this brush.
	
	FMatrix ToBrush; ToBrush.SetIdentity();  ToBrush.SetOrigin(-FVector(NewBrushPosLocal.X, NewBrushPosLocal.Y, NewBrushPosLocal.Z)); //  ToBrush.

	KelvinBrushOp.ApplyBrush(KelvinletBrushOpProperties, ToBrush, VertexROI, ROIPositionBuffer);

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);
	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

void UDynamicMeshSculptTool::ApplyScaleKelvinBrush(const FRay& WorldRay)
{
	UpdateBrushPositionOnSculptMesh(WorldRay, true);

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	FVector3d OffsetBrushPosLocal = NewBrushPosLocal - BrushProperties->Depth * CurrentBrushRadius * BrushNormalLocal;
	

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * FMath::Sqrt(CurrentBrushRadius) * SculptProperties->PrimaryBrushSpeed * 0.025 * ActivePressure; ; 

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FKelvinletBrushOp KelvinBrushOp(*Mesh);


	FKelvinletBrushOp::FKelvinletBrushOpProperties  KelvinletBrushOpProperties(EKelvinletBrushMode::ScaleKelvinlet, *KelvinBrushProperties, *BrushProperties);
	KelvinletBrushOpProperties.Direction = FVector(UseSpeed, 0., 0.); // it is a bit iffy, but we only use the first component for the scale
	KelvinletBrushOpProperties.Size *= 0.35;

	FMatrix ToBrush; ToBrush.SetIdentity();  ToBrush.SetOrigin(-FVector(OffsetBrushPosLocal.X, OffsetBrushPosLocal.Y, OffsetBrushPosLocal.Z)); //  ToBrush.

	KelvinBrushOp.ApplyBrush(KelvinletBrushOpProperties, ToBrush, VertexROI, ROIPositionBuffer);

	// Update the mesh positions to match those in the position buffer
	SyncMeshWithPositionBuffer(Mesh);
	ScheduleRemeshPass();
	LastBrushPosLocal = NewBrushPosLocal;
}

int UDynamicMeshSculptTool::FindHitSculptMeshTriangle(const FRay3d& LocalRay)
{
	if (BrushProperties->bHitBackFaces)
	{
		return DynamicMeshComponent->GetOctree()->FindNearestHitObject(LocalRay);
	}
	else
	{
		FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition(StateOut.Position));
		int HitTID = DynamicMeshComponent->GetOctree()->FindNearestHitObject(LocalRay,
			[this, Mesh, &LocalEyePosition](int TriangleID) {
			FVector3d Normal, Centroid;
			double Area;
			Mesh->GetTriInfo(TriangleID, Normal, Area, Centroid);
			return Normal.Dot((Centroid - LocalEyePosition)) < 0;
		});
		return HitTID;
	}
}

int UDynamicMeshSculptTool::FindHitTargetMeshTriangle(const FRay3d& LocalRay)
{
	IMeshSpatial::FQueryOptions RaycastOptions;

	if (BrushProperties->bHitBackFaces == false)
	{
		FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition(StateOut.Position));

		RaycastOptions.TriangleFilterF = [this, Mesh, LocalEyePosition](int TriangleID) {
			FVector3d Normal, Centroid;
			double Area;
			Mesh->GetTriInfo(TriangleID, Normal, Area, Centroid);
			return Normal.Dot((Centroid - LocalEyePosition)) < 0;
		};
	}

	int HitTID =  BrushTargetMeshSpatial.FindNearestHitTriangle(LocalRay, RaycastOptions);

	return HitTID;
}

bool UDynamicMeshSculptTool::UpdateBrushPositionOnActivePlane(const FRay& WorldRay)
{
	FVector3d NewHitPosWorld;
	ActiveDragPlane.RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, 2, NewHitPosWorld);
	LastBrushPosWorld = NewHitPosWorld;
	LastBrushPosNormalWorld = ActiveDragPlane.Z();
	return true;
}

bool UDynamicMeshSculptTool::UpdateBrushPositionOnTargetMesh(const FRay& WorldRay, bool bFallbackToViewPlane)
{
	FRay3d LocalRay(CurTargetTransform.InverseTransformPosition(WorldRay.Origin),
		CurTargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	const FDynamicMesh3* TargetMesh = BrushTargetMeshSpatial.GetMesh();

	int HitTID = FindHitTargetMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		FTriangle3d Triangle;
		TargetMesh->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		LastBrushPosNormalWorld = CurTargetTransform.TransformNormal(TargetMesh->GetTriNormal(HitTID));
		LastBrushPosWorld = CurTargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		return true;
	}

	if (bFallbackToViewPlane)
	{
		FFrame3d BrushPlane(LastBrushPosWorld, CameraState.Forward());
		FVector3d NewHitPosWorld;
		BrushPlane.RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, 2, NewHitPosWorld);
		LastBrushPosWorld = NewHitPosWorld;
		LastBrushPosNormalWorld = ActiveDragPlane.Z();
		return true;
	}

	return false;
}

bool UDynamicMeshSculptTool::UpdateBrushPositionOnSculptMesh(const FRay& WorldRay, bool bFallbackToViewPlane)
{
	FRay3d LocalRay(CurTargetTransform.InverseTransformPosition(WorldRay.Origin),
		CurTargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	int HitTID = FindHitSculptMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		const FDynamicMesh3* SculptMesh = DynamicMeshComponent->GetMesh();

		FTriangle3d Triangle;
		SculptMesh->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		LastBrushPosNormalWorld = CurTargetTransform.TransformNormal(SculptMesh->GetTriNormal(HitTID));
		LastBrushPosWorld = CurTargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		return true;
	}

	if (bFallbackToViewPlane)
	{
		FFrame3d BrushPlane(LastBrushPosWorld, CameraState.Forward());
		FVector3d NewHitPosWorld;
		BrushPlane.RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, 2, NewHitPosWorld);
		LastBrushPosWorld = NewHitPosWorld;
		LastBrushPosNormalWorld = ActiveDragPlane.Z();
		return true;
	}

	return false;
}

void UDynamicMeshSculptTool::AlignBrushToView()
{
	LastBrushPosNormalWorld = -CameraState.Forward();
}


bool UDynamicMeshSculptTool::UpdateBrushPosition(const FRay& WorldRay)
{
	// This is an unfortunate hack necessary because we haven't refactored brushes properly yet

	if (bSmoothing)
	{
		return UpdateBrushPositionOnSculptMesh(WorldRay, false);
	}

	bool bHit = false;
	switch (SculptProperties->PrimaryBrushType)
	{
	case EDynamicMeshSculptBrushType::Offset:
	case EDynamicMeshSculptBrushType::SculptMax:
	case EDynamicMeshSculptBrushType::Pinch:
	case EDynamicMeshSculptBrushType::Resample:
		bHit = UpdateBrushPositionOnTargetMesh(WorldRay, false);
		break;

	case EDynamicMeshSculptBrushType::SculptView:
	case EDynamicMeshSculptBrushType::PlaneViewAligned:
		bHit = UpdateBrushPositionOnTargetMesh(WorldRay, false);
		AlignBrushToView();
		break;

	case EDynamicMeshSculptBrushType::Move:
		//return UpdateBrushPositionOnActivePlane(WorldRay);
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;

	case EDynamicMeshSculptBrushType::Smooth:
	case EDynamicMeshSculptBrushType::Inflate:
	case EDynamicMeshSculptBrushType::Flatten:
	case EDynamicMeshSculptBrushType::Plane:
	case EDynamicMeshSculptBrushType::FixedPlane:
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;

	default:
		UE_LOG(LogTemp, Warning, TEXT("UDynamicMeshSculptTool: unknown brush type in UpdateBrushPosition"));
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	}

	return bHit;
}



void UDynamicMeshSculptTool::OnEndDrag(const FRay& Ray)
{
	bInDrag = false;

	// cancel these! otherwise change record could become invalid
	bStampPending = false;
	bRemeshPending = false;

	// update spatial
	bTargetDirty = true;

	// close change record
	EndChange();
}


FInputRayHit UDynamicMeshSculptTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	return UMeshSurfacePointTool::BeginHoverSequenceHitTest(PressPos);
}

bool UDynamicMeshSculptTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	PendingStampType = SculptProperties->PrimaryBrushType;

	if (bInDrag)
	{
		FVector3d NewHitPosWorld;
		ActiveDragPlane.RayPlaneIntersection(DevicePos.WorldRay.Origin, DevicePos.WorldRay.Direction, 2, NewHitPosWorld);
		LastBrushPosWorld = NewHitPosWorld;
		LastBrushPosNormalWorld = ActiveDragPlane.Z();
	}
	else
	{
		UpdateBrushPosition(DevicePos.WorldRay);

		//FHitResult OutHit;
		//if (HitTest(DevicePos.WorldRay, OutHit))
		//{
		//	LastBrushPosWorld = DevicePos.WorldRay.PointAt(OutHit.Distance + BrushProperties->Depth*CurrentBrushRadius);
		//	LastBrushPosNormalWorld = OutHit.Normal;
		//}
	}
	return true;
}


void UDynamicMeshSculptTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	BrushIndicator->Update( (float)this->CurrentBrushRadius, (FVector)this->LastBrushPosWorld, (FVector)this->LastBrushPosNormalWorld, 1.0f-BrushProperties->BrushFalloffAmount);
	if (BrushIndicatorMaterial)
	{
		double FixedDimScale = ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(CameraState, LastBrushPosWorld, 1.5f);
		BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffWidth"), FixedDimScale);
	}

	if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::FixedPlane)
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		FColor GridColor(128, 128, 128, 32);
		float GridThickness = 0.5f;
		float GridLineSpacing = 25.0f;   // @todo should be relative to view
		int NumGridLines = 10;
		FFrame3f DrawFrame(GizmoProperties->Position, GizmoProperties->Rotation);
		MeshDebugDraw::DrawSimpleGrid(DrawFrame, NumGridLines, GridLineSpacing, GridThickness, GridColor, false, PDI, FTransform::Identity);
	}
}

void UDynamicMeshSculptTool::OnTick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_SculptToolTick);

	ActivePressure = GetCurrentDevicePressure();

	// Allow a tick to pass between application of brush stamps. Bizarrely this
	// improves responsiveness in the Editor...
	static int TICK_SKIP_HACK = 0;
	if (TICK_SKIP_HACK++ % 2 == 0)
	{
		return;
	}

	ShowWireframeWatcher.CheckAndUpdate();
	MaterialModeWatcher.CheckAndUpdate();
	FlatShadingWatcher.CheckAndUpdate();
	ColorWatcher.CheckAndUpdate();
	ImageWatcher.CheckAndUpdate();
	BrushTypeWatcher.CheckAndUpdate();
	GizmoPositionWatcher.CheckAndUpdate();
	GizmoRotationWatcher.CheckAndUpdate();

	bool bGizmoVisible = (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::FixedPlane)
		&& (GizmoProperties->bShowGizmo);
	UpdateFixedPlaneGizmoVisibility(bGizmoVisible);
	GizmoProperties->bPropertySetEnabled = (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::FixedPlane);

	if (PendingWorkPlaneUpdate != EPendingWorkPlaneUpdate::NoUpdatePending)
	{
		SetFixedSculptPlaneFromWorldPos((FVector)LastBrushPosWorld, (FVector)LastBrushPosNormalWorld, PendingWorkPlaneUpdate);
		PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::NoUpdatePending;
	}

	// if user changed to not-frozen, we need to update the target
	if (bCachedFreezeTarget != SculptProperties->bFreezeTarget)
	{
		UpdateTarget();
	}

	bool bMeshModified = false;
	bool bMeshShapeModified = false;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();

	//Octree->CheckValidity(EValidityCheckFailMode::Check, false, true);

	//
	// Apply stamp
	//

	if (bStampPending)
	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Tick_ApplyStampBlock);

		// would this ever be true? does stamp require this?
		bool bRemoveTrianglesBeforeStamp = false;

		if ( bRemoveTrianglesBeforeStamp )
		{
			SCOPE_CYCLE_COUNTER(SculptTool_Tick_ApplyStamp_Remove);
			Octree->RemoveTriangles(TriangleROI);
		}
		else
		{
			SCOPE_CYCLE_COUNTER(SculptTool_Tick_ApplyStamp_Remove);
			Octree->NotifyPendingModification(TriangleROI);		// to mark initial positions
		}

		ApplyStamp(PendingStampRay);
		bStampPending = false;

		bNormalUpdatePending = true;
		bMeshModified = true;
		bMeshShapeModified = true;

		// flow
		if (bInDrag)
		{
			bStampPending = true;
		}

		if (bRemoveTrianglesBeforeStamp)
		{
			SCOPE_CYCLE_COUNTER(SculptTool_Tick_ApplyStamp_Insert);
			Octree->InsertTriangles(TriangleROI);
		}
		else
		{
			SCOPE_CYCLE_COUNTER(SculptTool_Tick_ApplyStamp_Insert);
			Octree->ReinsertTriangles(TriangleROI);
		}

	}


	bool bUpdatedROIInRemesh = false;
	if (bRemeshPending)
	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Tick_RemeshBlock);

		check(bInDrag == true);    // this would break undo otherwise!

		RemeshROIPass();

		bMeshModified = true;
		bMeshShapeModified = true;
		bRemeshPending = false;
		bNormalUpdatePending = true;
		bHaveRemeshed = true;
		bUpdatedROIInRemesh = true;
	}

	//Octree->CheckValidity(EValidityCheckFailMode::Check, false, true);

	if (bNormalUpdatePending)
	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Tick_NormalsBlock);

		if (Mesh->HasAttributes() && Mesh->Attributes()->PrimaryNormals() != nullptr)
		{
			RecalculateNormals_Overlay();
		}
		else
		{
			RecalculateNormals_PerVertex();
		}
		bNormalUpdatePending = false;
		bMeshModified = true;
	}

	if (bMeshModified)
	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Tick_UpdateMeshBlock);

		if (bMeshModified)

		DynamicMeshComponent->NotifyMeshUpdated();
		GetToolManager()->PostInvalidation();

		bMeshModified = false;

		if (bUpdatedROIInRemesh == false)
		{
			UpdateROI(LastBrushPosLocal);
		}
	}

	if (bTargetDirty)
	{
		UpdateTarget();
		bTargetDirty = false;
	}
}

void UDynamicMeshSculptTool::PrecomputeRemeshInfo()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	// check if we have any open boundary edges
	bHaveMeshBoundaries = false;
	for (int eid : Mesh->EdgeIndicesItr())
	{
		if (Mesh->IsBoundaryEdge(eid))
		{
			bHaveMeshBoundaries = true;
			break;
		}
	}

	// check if we have any UV seams
	bHaveUVSeams = false;
	bHaveNormalSeams = false;
	if (Mesh->HasAttributes())
	{
		FDynamicMeshAttributeSet* Attribs = Mesh->Attributes();
		for (int k = 0; k < Attribs->NumUVLayers(); ++k)
		{
			bHaveUVSeams = bHaveUVSeams || Attribs->GetUVLayer(k)->HasInteriorSeamEdges();
		}

		bHaveNormalSeams = Attribs->PrimaryNormals()->HasInteriorSeamEdges();
	}
}

void UDynamicMeshSculptTool::ScheduleRemeshPass()
{
	if (bEnableRemeshing && RemeshProperties != nullptr && RemeshProperties->bEnableRemeshing)
	{
		bRemeshPending = true;
	}
}

/*
        Split	Collapse	Vertices Pinned	Flip
Fixed	FALSE	FALSE	TRUE	FALSE
Refine	TRUE	FALSE	TRUE	FALSE
Free	TRUE	TRUE	FALSE	FALSE
Ignore	TRUE	TRUE	FALSE	TRUE
*/

void UDynamicMeshSculptTool::ConfigureRemesher(FSubRegionRemesher& Remesher)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();

	const double SizeRange = 5;
	double LengthMultiplier = (RemeshProperties->TriangleSize >= 0) ?
		FMathd::Lerp(1.0, 5.0, FMathd::Pow( (double)RemeshProperties->TriangleSize / SizeRange, 2.0) )
		: FMathd::Lerp(0.25, 1.0, 1.0 - FMathd::Pow(FMathd::Abs((double)RemeshProperties->TriangleSize) / SizeRange, 2.0) );
	double TargetEdgeLength = LengthMultiplier * InitialEdgeLength;

	Remesher.SetTargetEdgeLength(TargetEdgeLength);

	double DetailT = (double)(RemeshProperties->PreserveDetail) / 5.0;
	double UseSmoothing = RemeshProperties->SmoothingStrength * 0.25;
	UseSmoothing *= FMathd::Lerp(1.0, 0.25, DetailT);
	Remesher.SmoothSpeedT = UseSmoothing;

	// this is a temporary tweak for Pinch brush. Remesh params should be per-brush!
	if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::Pinch && bSmoothing == false)
	{
		Remesher.MinEdgeLength = TargetEdgeLength * 0.1;

		Remesher.CustomSmoothSpeedF = [this, UseSmoothing](const FDynamicMesh3& Mesh, int vID)
		{
			FVector3d Pos = Mesh.GetVertex(vID);
			double Falloff = CalculateBrushFalloff(Pos.Distance((FVector3d)LastBrushPosLocal));
			return (1.0f - Falloff) * UseSmoothing;
		};
	}
	else if (bSmoothing && SculptProperties->bDetailPreservingSmooth)
	{
		// this is the case where we don't want remeshing in smoothing
		Remesher.MaxEdgeLength = 3 * InitialEdgeLength;
		Remesher.MinEdgeLength = InitialEdgeLength * 0.05;
	}
	else
	{
		if (RemeshProperties->PreserveDetail > 0)
		{
			Remesher.MinEdgeLength *= FMathd::Lerp(1.0, 0.1, DetailT);
			Remesher.CustomSmoothSpeedF = [this, UseSmoothing, DetailT](const FDynamicMesh3& Mesh, int vID)
			{
				FVector3d Pos = Mesh.GetVertex(vID);
				double FalloffT = 1.0 - CalculateBrushFalloff(Pos.Distance((FVector3d)LastBrushPosLocal));
				FalloffT = FMathd::Lerp(1.0, FalloffT, DetailT);
				return FalloffT * UseSmoothing;
			};
		}
	}


	Remesher.SmoothType = (SculptProperties->bPreserveUVFlow) ?
		FRemesher::ESmoothTypes::MeanValue : FRemesher::ESmoothTypes::Uniform;
	Remesher.bEnableCollapses = RemeshProperties->bCollapses;
	Remesher.bEnableFlips = RemeshProperties->bFlips;
	Remesher.bEnableSplits = RemeshProperties->bSplits;
	Remesher.bPreventNormalFlips = RemeshProperties->bPreventNormalFlips;

	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_Setup);
		for (int VertIdx : VertexROI)
		{
			Remesher.VertexROI.Add(VertIdx);
		}
		Remesher.InitializeFromVertexROI();
		Remesher.UpdateROI();	// required to use roi in constraints fn below
		Octree->RemoveTriangles(Remesher.GetCurrentTriangleROI());
	}

	FMeshConstraints Constraints;
	bool bConstraintAllowSplits = true;
	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_Constraints);

		// TODO: only constrain in ROI. This is quite difficult to do externally because we need to update based on
		// the changing triangle set in Remesher. Perhaps FSubRegionRemesher should update the constraints itself?

		FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, *Mesh,
															 (EEdgeRefineFlags)RemeshProperties->MeshBoundaryConstraint,
															 (EEdgeRefineFlags)RemeshProperties->GroupBoundaryConstraint,
															 (EEdgeRefineFlags)RemeshProperties->MaterialBoundaryConstraint,
															 bConstraintAllowSplits, !RemeshProperties->bPreserveSharpEdges);
		if ( Constraints.HasConstraints() )
		{
			Remesher.SetExternalConstraints(MoveTemp(Constraints));
		}
	}
}

void UDynamicMeshSculptTool::RemeshROIPass()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();
	FSubRegionRemesher Remesher(Mesh);
	ConfigureRemesher(Remesher);

	if (ActiveMeshChange != nullptr)
	{
		Remesher.SetMeshChangeTracker(ActiveMeshChange);
	}

	bool bIsUniformSmooth = (Remesher.SmoothType == FRemesher::ESmoothTypes::Uniform);
	for (int k = 0; k < 5; ++k)
	{
		if ( ( bIsUniformSmooth == false ) && ( k > 1 ) )
		{
			Remesher.bEnableFlips = false;
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_RemeshROIUpdate);

			Remesher.UpdateROI();

			if (ActiveMeshChange != nullptr)
			{
				// [TODO] would like to only save vertices here, as triangles will be saved by Remesher as necessary.
				// However currently FDynamicMeshChangeTracker cannot independently save vertices, only vertices 
				// that are part of saved triangles will be included in the output FDynamicMeshChange
				Remesher.SaveActiveROI(ActiveMeshChange);
				//ActiveMeshChange->VerifySaveState();    // useful for debugging
			}

			Remesher.BeginTrackRemovedTrisInPass();
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_RemeshPass);
			Remesher.BasicRemeshPass();
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_PassOctreeUpdate);
			const TSet<int32>& TrisRemovedInPass = Remesher.EndTrackRemovedTrisInPass();
			Octree->RemoveTriangles(TrisRemovedInPass);
		}
	}
	//UE_LOG(LogTemp, Warning, TEXT("Triangle Count %d after update"), Mesh->TriangleCount());

	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_Finish);

		// reinsert new ROI into octree
		Octree->ReinsertTriangles(Remesher.GetCurrentTriangleROI());

		//Octree->CheckValidity(EValidityCheckFailMode::Check, false, true);

		UpdateROI(LastBrushPosLocal);
	}
}

void UDynamicMeshSculptTool::RecalculateNormals_PerVertex()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();

	int MaxVertexID = Mesh->MaxVertexID();
	if (NormalsVertexFlags.Num() < MaxVertexID)
	{
		NormalsVertexFlags.Init(false, MaxVertexID * 2);
	}


	{
		SCOPE_CYCLE_COUNTER(SculptTool_Normals_Collect);

		TrianglesBuffer.Reset();
		NormalsBuffer.Reset();
		Octree->RangeQuery(Octree->ModifiedBounds, TrianglesBuffer);
		for (int TriangleID : TrianglesBuffer)
		{
			FIndex3i TriV = Mesh->GetTriangle(TriangleID);
			for (int j = 0; j < 3; ++j)
			{
				int vid = TriV[j];
				if (NormalsVertexFlags[vid] == false)
				{
					NormalsBuffer.Add(vid);
					NormalsVertexFlags[vid] = true;
				}
			}
		}
	}

	//UE_LOG(LogTemp, Warning, TEXT("Computing %d normals"), NormalsBuffer.Num());

	{
		SCOPE_CYCLE_COUNTER(SculptTool_Normals_Compute);

		ParallelFor(NormalsBuffer.Num(), [&](int k) {
			int vid = NormalsBuffer[k];
			FVector3d NewNormal = FMeshNormals::ComputeVertexNormal(*Mesh, vid);
			Mesh->SetVertexNormal(vid, (FVector3f)NewNormal);
			NormalsVertexFlags[vid] = false;
		});
	}
}

void UDynamicMeshSculptTool::RecalculateNormals_Overlay()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshNormalOverlay* Normals = Mesh->HasAttributes() ? Mesh->Attributes()->PrimaryNormals() : nullptr;
	check(Normals != nullptr);


	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();

	int MaxElementID = Normals->MaxElementID();
	if (NormalsVertexFlags.Num() < MaxElementID)
	{
		NormalsVertexFlags.Init(false, MaxElementID * 2);
	}


	{
		SCOPE_CYCLE_COUNTER(SculptTool_Normals_Collect);

		TrianglesBuffer.Reset();
		NormalsBuffer.Reset();
		Octree->RangeQuery(Octree->ModifiedBounds, TrianglesBuffer);
		for (int TriangleID : TrianglesBuffer)
		{
			FIndex3i TriElems = Normals->GetTriangle(TriangleID);
			for (int j = 0; j < 3; ++j)
			{
				int elemid = TriElems[j];
				if (NormalsVertexFlags[elemid] == false)
				{
					NormalsBuffer.Add(elemid);
					NormalsVertexFlags[elemid] = true;
				}
			}
		}
	}

	//UE_LOG(LogTemp, Warning, TEXT("Computing %d normals"), NormalsBuffer.Num());

	{
		SCOPE_CYCLE_COUNTER(SculptTool_Normals_Compute);

		ParallelFor(NormalsBuffer.Num(), [&](int k) {
			int elemid = NormalsBuffer[k];
			FVector3d NewNormal = FMeshNormals::ComputeOverlayNormal(*Mesh, Normals, elemid);
			Normals->SetElement(elemid, (FVector3f)NewNormal);
			NormalsVertexFlags[elemid] = false;
		});
	}

}

void UDynamicMeshSculptTool::UpdateTarget()
{
	if (SculptProperties != nullptr )
	{
		bCachedFreezeTarget = SculptProperties->bFreezeTarget;
		if (SculptProperties->bFreezeTarget)
		{
			return;   // do not update frozen target
		}
	}

	BrushTargetMesh.Copy(*DynamicMeshComponent->GetMesh(), false, false, false, false);
	BrushTargetMeshSpatial.SetMesh(&BrushTargetMesh, true);

	BrushTargetNormals.SetMesh(&BrushTargetMesh);
	BrushTargetNormals.ComputeVertexNormals();
}

bool UDynamicMeshSculptTool::GetTargetMeshNearest(const FVector3d& Position, double SearchRadius, FVector3d& TargetPosOut, FVector3d& TargetNormalOut)
{
	double fDistSqr;
	int NearTID = BrushTargetMeshSpatial.FindNearestTriangle(Position, fDistSqr, SearchRadius);
	if (NearTID <= 0)
	{
		return false;
	}
	FTriangle3d Triangle;
	BrushTargetMesh.GetTriVertices(NearTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
	FDistPoint3Triangle3d Query(Position, Triangle);
	Query.Get();
	FIndex3i Tri = BrushTargetMesh.GetTriangle(NearTID);
	TargetNormalOut =
		Query.TriangleBaryCoords.X*BrushTargetNormals[Tri.A]
		+ Query.TriangleBaryCoords.Y*BrushTargetNormals[Tri.B]
		+ Query.TriangleBaryCoords.Z*BrushTargetNormals[Tri.C];
	TargetNormalOut.Normalize();
	TargetPosOut = Query.ClosestTrianglePoint;
	return true;
}

double UDynamicMeshSculptTool::EstimateIntialSafeTargetLength(const FDynamicMesh3& Mesh, int MinTargetTriCount)
{
	double AreaSum = 0;
	for (int tid : Mesh.TriangleIndicesItr())
	{
		AreaSum += Mesh.GetTriArea(tid);
	}

	int TriCount = Mesh.TriangleCount();
	double TargetTriArea = 1.0;
	if (TriCount < MinTargetTriCount)
	{
		TargetTriArea = AreaSum / (double)MinTargetTriCount;
	}
	else
	{
		TargetTriArea = AreaSum / (double)TriCount;
	}

	double EdgeLen = TriangleUtil::EquilateralEdgeLengthForArea(TargetTriArea);
	return (double)FMath::RoundToInt(EdgeLen*100.0) / 100.0;
}

UPreviewMesh* UDynamicMeshSculptTool::MakeDefaultSphereMesh(UObject* Parent, UWorld* World, int Resolution /*= 32*/)
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

void UDynamicMeshSculptTool::IncreaseBrushRadiusAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.025f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UDynamicMeshSculptTool::DecreaseBrushRadiusAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.025f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UDynamicMeshSculptTool::IncreaseBrushRadiusSmallStepAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.005f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UDynamicMeshSculptTool::DecreaseBrushRadiusSmallStepAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.005f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UDynamicMeshSculptTool::IncreaseBrushSpeedAction()
{
	SculptProperties->PrimaryBrushSpeed = FMath::Clamp(SculptProperties->PrimaryBrushSpeed + 0.05f, 0.0f, 1.0f);
}

void UDynamicMeshSculptTool::DecreaseBrushSpeedAction()
{
	SculptProperties->PrimaryBrushSpeed = FMath::Clamp(SculptProperties->PrimaryBrushSpeed - 0.05f, 0.0f, 1.0f);
}

void UDynamicMeshSculptTool::IncreaseBrushFalloffAction()
{
	const float ChangeAmount = 0.1f;
	const float OldValue = BrushProperties->BrushFalloffAmount;

	float NewValue = OldValue + ChangeAmount;
	BrushProperties->BrushFalloffAmount = FMath::Clamp(NewValue, 0.f, 1.f);
}

void UDynamicMeshSculptTool::DecreaseBrushFalloffAction()
{
	const float ChangeAmount = 0.1f;
	const float OldValue = BrushProperties->BrushFalloffAmount;

	float NewValue = OldValue - ChangeAmount;
	BrushProperties->BrushFalloffAmount = FMath::Clamp(NewValue, 0.f, 1.f);
}



void UDynamicMeshSculptTool::NextBrushModeAction()
{
	uint8 LastMode = (uint8)EDynamicMeshSculptBrushType::LastValue;
	SculptProperties->PrimaryBrushType = (EDynamicMeshSculptBrushType)(((uint8)SculptProperties->PrimaryBrushType + 1) % LastMode);
}

void UDynamicMeshSculptTool::PreviousBrushModeAction()
{
	uint8 LastMode = (uint8)EDynamicMeshSculptBrushType::LastValue;
	uint8 CurMode = (uint8)SculptProperties->PrimaryBrushType;
	if (CurMode == 0)
	{
		SculptProperties->PrimaryBrushType = (EDynamicMeshSculptBrushType)((uint8)LastMode - 1);
	}
	else
	{
		SculptProperties->PrimaryBrushType = (EDynamicMeshSculptBrushType)((uint8)CurMode - 1);
	}
}

void UDynamicMeshSculptTool::NextHistoryBrushModeAction()
{
	int MaxHistory = BrushTypeHistory.Num() - 1;
	if (BrushTypeHistoryIndex < MaxHistory)
	{
		BrushTypeHistoryIndex++;
		SculptProperties->PrimaryBrushType = BrushTypeHistory[BrushTypeHistoryIndex];
		LastStampType = SculptProperties->PrimaryBrushType;
	}
}

void UDynamicMeshSculptTool::PreviousHistoryBrushModeAction()
{
	if (BrushTypeHistoryIndex > 0)
	{
		BrushTypeHistoryIndex--;
		SculptProperties->PrimaryBrushType = BrushTypeHistory[BrushTypeHistoryIndex];
		LastStampType = SculptProperties->PrimaryBrushType;
	}
}

void UDynamicMeshSculptTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::IncreaseBrushSize,
		TEXT("SculptIncreaseRadius"),
		LOCTEXT("SculptIncreaseRadius", "Increase Sculpt Radius"),
		LOCTEXT("SculptIncreaseRadiusTooltip", "Increase radius of sculpting brush"),
		EModifierKey::None, EKeys::RightBracket,
		[this]() { IncreaseBrushRadiusAction(); } );

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::DecreaseBrushSize,
		TEXT("SculptDecreaseRadius"),
		LOCTEXT("SculptDecreaseRadius", "Decrease Sculpt Radius"),
		LOCTEXT("SculptDecreaseRadiusTooltip", "Decrease radius of sculpting brush"),
		EModifierKey::None, EKeys::LeftBracket,
		[this]() { DecreaseBrushRadiusAction(); } );

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 12,
		TEXT("BrushIncreaseFalloff"),
		LOCTEXT("BrushIncreaseFalloff", "Increase Brush Falloff"),
		LOCTEXT("BrushIncreaseFalloffTooltip", "Press this key to increase brush falloff by a fixed increment."),
		EModifierKey::Shift | EModifierKey::Control, EKeys::RightBracket,
		[this]() { IncreaseBrushFalloffAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 13,
		TEXT("BrushDecreaseFalloff"),
		LOCTEXT("BrushDecreaseFalloff", "Decrease Brush Falloff"),
		LOCTEXT("BrushDecreaseFalloffTooltip", "Press this key to decrease brush falloff by a fixed increment."),
		EModifierKey::Shift | EModifierKey::Control, EKeys::LeftBracket,
		[this]() { DecreaseBrushFalloffAction(); });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID+1,
		TEXT("NextBrushMode"),
		LOCTEXT("SculptNextBrushMode", "Next Brush Type"),
		LOCTEXT("SculptNextBrushModeTooltip", "Cycle to next Brush Type"),
		EModifierKey::None, EKeys::A,
		[this]() { NextBrushModeAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID+2,
		TEXT("PreviousBrushMode"),
		LOCTEXT("SculptPreviousBrushMode", "Previous Brush Type"),
		LOCTEXT("SculptPreviousBrushModeTooltip", "Cycle to previous Brush Type"),
		EModifierKey::None, EKeys::Q,
		[this]() { PreviousBrushModeAction(); });

	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 10,
	//	TEXT("NextBrushHistoryState"),
	//	LOCTEXT("SculptNextBrushHistoryState", "Next Brush History State"),
	//	LOCTEXT("SculptSculptNextBrushHistoryStateTooltip", "Cycle to next Brush History state"),
	//	EModifierKey::Shift, EKeys::Q,
	//	[this]() { NextHistoryBrushModeAction(); });

	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 11,
	//	TEXT("PreviousBrushHistoryState"),
	//	LOCTEXT("SculptPreviousBrushHistoryState", "Previous Brush History State"),
	//	LOCTEXT("SculptPreviousBrushHistoryStateTooltip", "Cycle to previous Brush History state"),
	//	EModifierKey::Shift, EKeys::A,
	//	[this]() { PreviousHistoryBrushModeAction(); });

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

//
// Change Tracking
//
void UDynamicMeshSculptTool::BeginChange(bool bIsVertexChange)
{
	check(ActiveVertexChange == nullptr);
	check(ActiveMeshChange == nullptr);
	if (bIsVertexChange)
	{
		ActiveVertexChange = new FMeshVertexChangeBuilder();
	}
	else
	{
		ActiveMeshChange = new FDynamicMeshChangeTracker(DynamicMeshComponent->GetMesh());
		ActiveMeshChange->BeginChange();
	}
}

void UDynamicMeshSculptTool::EndChange()
{
	if (ActiveVertexChange != nullptr)
	{
		GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(ActiveVertexChange->Change), LOCTEXT("VertexSculptChange", "Brush Stroke"));

		delete ActiveVertexChange;
		ActiveVertexChange = nullptr;
	}

	if (ActiveMeshChange != nullptr)
	{
		FMeshChange* NewMeshChange = new FMeshChange();
		NewMeshChange->DynamicMeshChange = ActiveMeshChange->EndChange();
		//NewMeshChange->DynamicMeshChange->CheckValidity();
		TUniquePtr<FMeshChange> NewChange(NewMeshChange);
		GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(NewChange), LOCTEXT("MeshSculptChange", "Brush Stroke"));
		delete ActiveMeshChange;
		ActiveMeshChange = nullptr;
	}
}

void UDynamicMeshSculptTool::SaveActiveROI()
{
	if (ActiveMeshChange != nullptr)
	{
		// must save triangles containing vertex ROI or they will not be included in emitted mesh change (due to limitations of change tracker)
		ActiveMeshChange->SaveVertexOneRingTriangles(VertexROI, true);
	}
}

void UDynamicMeshSculptTool::UpdateSavedVertex(int vid, const FVector3d& OldPosition, const FVector3d& NewPosition)
{
	if (ActiveVertexChange != nullptr)
	{
		UpdateSavedVertexLock.Lock();
		ActiveVertexChange->UpdateVertex(vid, OldPosition, NewPosition);
		UpdateSavedVertexLock.Unlock();
	}
}

void UDynamicMeshSculptTool::UpdateMaterialMode(EMeshEditingMaterialModes MaterialMode)
{
	if (MaterialMode == EMeshEditingMaterialModes::ExistingMaterial)
	{
		DynamicMeshComponent->ClearOverrideRenderMaterial();
		DynamicMeshComponent->bCastDynamicShadow = ComponentTarget->GetOwnerComponent()->bCastDynamicShadow;
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
			DynamicMeshComponent->SetOverrideRenderMaterial(ActiveOverrideMaterial);
			ActiveOverrideMaterial->SetScalarParameterValue(TEXT("FlatShading"), (ViewProperties->bFlatShading) ? 1.0f : 0.0f);
		}

		DynamicMeshComponent->bCastDynamicShadow = false;
	}
}


void UDynamicMeshSculptTool::UpdateFlatShadingSetting(bool bNewValue)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetScalarParameterValue(TEXT("FlatShading"), (bNewValue) ? 1.0f : 0.0f);
	}
}


void UDynamicMeshSculptTool::UpdateColorSetting(FLinearColor NewColor)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetVectorParameterValue(TEXT("Color"), NewColor);
	}
}

void UDynamicMeshSculptTool::UpdateImageSetting(UTexture2D* NewImage)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetTextureParameterValue(TEXT("ImageTexture"), NewImage);
	}
}

void UDynamicMeshSculptTool::UpdateBrushType(EDynamicMeshSculptBrushType BrushType)
{
	static const FText BaseMessage = LOCTEXT("OnStartSculptTool", "Hold Shift to Smooth, Ctrl to Invert (where applicable). Q/A keys cycle Brush Type. S/D changes Size (+Shift to small-step), W/E changes Strength.");
	FTextBuilder Builder;
	Builder.AppendLine(BaseMessage);

	SetToolPropertySourceEnabled(GizmoProperties, false);
	SetToolPropertySourceEnabled(PlaneBrushProperties, false);
	SetToolPropertySourceEnabled(SculptMaxBrushProperties, false);

	if (BrushType == EDynamicMeshSculptBrushType::FixedPlane)
	{
		Builder.AppendLine(LOCTEXT("FixedPlaneTip", "Use T to reposition Work Plane at cursor, Shift+T to align to Normal, Ctrl+Shift+T to align to View"));
		SetToolPropertySourceEnabled(PlaneBrushProperties, true);
		SetToolPropertySourceEnabled(GizmoProperties, true);
	}
	if (BrushType == EDynamicMeshSculptBrushType::Plane || BrushType == EDynamicMeshSculptBrushType::PlaneViewAligned || BrushType == EDynamicMeshSculptBrushType::Flatten)
	{
		SetToolPropertySourceEnabled(PlaneBrushProperties, true);
	}
	if (BrushType == EDynamicMeshSculptBrushType::SculptMax)
	{
		SetToolPropertySourceEnabled(SculptMaxBrushProperties, true);
	}

	GetToolManager()->DisplayMessage(Builder.ToText(), EToolMessageLevel::UserNotification);
}


void UDynamicMeshSculptTool::SetFixedSculptPlaneFromWorldPos(const FVector& Position, const FVector& Normal, EPendingWorkPlaneUpdate UpdateType)
{
	if (UpdateType == EPendingWorkPlaneUpdate::MoveToHitPositionNormal)
	{
		UpdateFixedSculptPlanePosition(Position);
		FFrame3d CurFrame(FVector::ZeroVector, GizmoProperties->Rotation);
		CurFrame.AlignAxis(2, (FVector3d)Normal );
		UpdateFixedSculptPlaneRotation( (FQuat)CurFrame.Rotation );
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

void UDynamicMeshSculptTool::PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	UpdateFixedSculptPlaneRotation(Transform.GetRotation());
	UpdateFixedSculptPlanePosition(Transform.GetLocation());
}

void UDynamicMeshSculptTool::UpdateFixedSculptPlanePosition(const FVector& Position)
{
	GizmoProperties->Position = Position;
	GizmoPositionWatcher.SilentUpdate();
}

void UDynamicMeshSculptTool::UpdateFixedSculptPlaneRotation(const FQuat& Rotation)
{
	GizmoProperties->Rotation = Rotation;
	GizmoRotationWatcher.SilentUpdate();
}

void UDynamicMeshSculptTool::UpdateGizmoFromProperties()
{
	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
	}
}

void UDynamicMeshSculptTool::UpdateFixedPlaneGizmoVisibility(bool bVisible)
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
			PlaneTransformGizmo->SetNewGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
		}

		PlaneTransformGizmo->bSnapToWorldGrid = GizmoProperties->bSnapToGrid;
	}
}

#undef LOCTEXT_NAMESPACE
