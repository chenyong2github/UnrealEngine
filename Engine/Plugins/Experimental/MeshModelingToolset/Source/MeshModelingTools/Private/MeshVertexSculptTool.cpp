// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshVertexSculptTool.h"
#include "Containers/Map.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"

#include "ProjectionTargets.h"
#include "MeshWeights.h"
#include "MeshNormals.h"
#include "MeshIndexUtil.h"
#include "PreviewMesh.h"
#include "ToolSetupUtil.h"
#include "Util/BufferUtil.h"

#include "Changes/MeshVertexChange.h"

#include "ToolDataVisualizer.h"
#include "Components/PrimitiveComponent.h"

//#include "Sculpting/KelvinletBrushOp.h"
#include "Sculpting/MeshSmoothingBrushOps.h"
#include "Sculpting/MeshInflateBrushOps.h"
#include "Sculpting/MeshMoveBrushOps.h"
#include "Sculpting/MeshPlaneBrushOps.h"
#include "Sculpting/MeshPinchBrushOps.h"
#include "Sculpting/MeshSculptBrushOps.h"
#include "Sculpting/MeshSculptUtil.h"

#include "UObject/ObjectMacros.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "UMeshVertexSculptTool"

namespace
{
	// probably should be something defined for the whole tool framework...
#if WITH_EDITOR
	static EAsyncExecution VertexSculptToolAsyncExecTarget = EAsyncExecution::LargeThreadPool;
#else
	static EAsyncExecution VertexSculptToolAsyncExecTarget = EAsyncExecution::ThreadPool;
#endif
}


/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UMeshVertexSculptToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMeshVertexSculptTool* SculptTool = NewObject<UMeshVertexSculptTool>(SceneState.ToolManager);
	SculptTool->SetWorld(SceneState.World);
	return SculptTool;
}

/*
 * Tool
 */

void UMeshVertexSculptTool::Setup()
{
	UMeshSculptToolBase::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor());
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();

	// initialize from LOD-0 MeshDescription
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	double MaxDimension = DynamicMeshComponent->GetMesh()->GetCachedBounds().MaxDim();

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

	// assign materials
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->bInvalidateProxyOnChange = false;
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshVerticesChanged.AddUObject(this, &UMeshVertexSculptTool::OnDynamicMeshComponentChanged);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	// do we always want to keep vertex normals updated? Perhaps should discard vertex normals before baking?
	//FMeshNormals::QuickComputeVertexNormals(*Mesh);

	// initialize dynamic octree
	Octree.RootDimension = MaxDimension / 10.0;
	Octree.SetMaxTreeDepth(3);
	Octree.Initialize(Mesh);
	FDynamicMeshOctree3::FStatistics Stats;
	Octree.ComputeStatistics(Stats);
	//UE_LOG(LogTemp, Warning, TEXT("Octree Stats: %s"), *Stats.ToString());

	// initialize decomposition
	TUniquePtr<FMeshRenderDecomposition> Decomp = MakeUnique<FMeshRenderDecomposition>();
	FMeshRenderDecomposition::BuildChunkedDecomposition(Mesh, &MaterialSet, *Decomp);
	Decomp->BuildAssociations(Mesh);
	//UE_LOG(LogTemp, Warning, TEXT("Decomposition has %d groups"), Decomp->Num());
	DynamicMeshComponent->SetExternalDecomposition(MoveTemp(Decomp));

	// initialize target mesh
	UpdateBaseMesh(nullptr);
	bTargetDirty = false;

	// initialize brush radius range interval, brush properties
	UMeshSculptToolBase::InitializeBrushSizeRange(DynamicMeshComponent->GetMesh()->GetCachedBounds());

	// initialize other properties
	SculptProperties = NewObject<UVertexBrushSculptProperties>(this);
	//KelvinBrushProperties = NewObject<UKelvinBrushProperties>(this);

	// hide input Component
	ComponentTarget->SetOwnerVisibility(false);

	// init state flags flags
	bInDrag = false;
	bStampPending = false;
	ActiveVertexChange = nullptr;

	InitializeIndicator();

	// initialize our properties
	AddToolPropertySource(UMeshSculptToolBase::BrushProperties);
	AddToolPropertySource(SculptProperties);

	// add brush-specific properties 
	PlaneBrushProperties = NewObject<UPlaneBrushProperties>(this);
	PlaneBrushProperties->RestoreProperties(this);
	AddToolPropertySource(PlaneBrushProperties);
	SetToolPropertySourceEnabled(PlaneBrushProperties, false);

	SculptMaxBrushProperties = NewObject<USculptMaxBrushProperties>();
	SculptMaxBrushProperties->RestoreProperties(this);
	AddToolPropertySource(SculptMaxBrushProperties);

	//AddToolPropertySource(KelvinBrushProperties);
	//KelvinBrushProperties->RestoreProperties(this);

	AddToolPropertySource(UMeshSculptToolBase::GizmoProperties);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::GizmoProperties, false);

	CalculateBrushRadius();
	SculptProperties->RestoreProperties(this);

	// disable tool-specific properties
	SetToolPropertySourceEnabled(SculptMaxBrushProperties, false);
	//SetToolPropertySourceEnabled(KelvinBrushProperties, false);

	AddToolPropertySource(UMeshSculptToolBase::ViewProperties);

	// register watchers
	BrushTypeWatcher.Initialize(
		[this]() { return SculptProperties->PrimaryBrushType; },
		[this](EMeshVertexSculptBrushType NewBrushType) { UpdateBrushType(NewBrushType); }, SculptProperties->PrimaryBrushType);

	ActiveFalloff = MakeShared<FMeshSculptFallofFunc>();
	ActiveFalloff->FalloffFunc = [&](const FSculptBrushStamp& StampInfo, const FVector3d& Position)
	{
		return CalculateBrushFalloff(Position.Distance(StampInfo.LocalFrame.Origin));
	};

	SecondaryBrushOp = MakeUnique<FSmoothBrushOp>();
	SecondaryBrushOp->Falloff = ActiveFalloff;
	PrimaryBrushOp = MakeUnique<FSmoothBrushOp>();  // set default
	PrimaryBrushOp->Falloff = ActiveFalloff;

	UpdateBrushType(SculptProperties->PrimaryBrushType);
}

void UMeshVertexSculptTool::Shutdown(EToolShutdownType ShutdownType)
{
	UMeshSculptToolBase::Shutdown(ShutdownType);

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
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, false);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}

	SculptProperties->SaveProperties(this);
	//KelvinBrushProperties->SaveProperties(this);
	PlaneBrushProperties->SaveProperties(this);
	SculptMaxBrushProperties->SaveProperties(this);
}


void UMeshVertexSculptTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	CalculateBrushRadius();
}

bool UMeshVertexSculptTool::HitTest(const FRay& Ray, FHitResult& OutHit)
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

void UMeshVertexSculptTool::OnBeginDrag(const FRay& Ray)
{
	bSmoothing = GetShiftToggle();
	bInvert = GetCtrlToggle();

	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		FVector3d BrushStartCenterWorld = Ray.PointAt(OutHit.Distance) + BrushProperties->Depth*CurrentBrushRadius*Ray.Direction;

		bInDrag = true;

		ActiveDragPlane = FFrame3d(BrushStartCenterWorld, -Ray.Direction);
		ActiveDragPlane.RayPlaneIntersection(Ray.Origin, Ray.Direction, 2, LastHitPosWorld);

		LastBrushPosWorld = LastHitPosWorld;
		LastBrushPosNormalWorld = ActiveDragPlane.Z();
		LastBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastHitPosWorld);

		BeginChange();

		UpdateROI(LastBrushPosLocal);

		if (SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::Plane)
		{
			ActiveFixedBrushPlane = ComputeROIBrushPlane(LastBrushPosLocal, false, false);
		}
		else if (SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::PlaneViewAligned)
		{
			AlignBrushToView();
			ActiveFixedBrushPlane = ComputeROIBrushPlane(LastBrushPosLocal, false, true);
		}
		else if (SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::FixedPlane)
		{
			ActiveFixedBrushPlane = FFrame3d(
				CurTargetTransform.InverseTransformPosition(GizmoProperties->Position),
				CurTargetTransform.GetRotation().Inverse() * (FQuaterniond)GizmoProperties->Rotation);
		}

		// init last stamp
		LastStamp.WorldFrame = FFrame3d(LastBrushPosWorld, LastBrushPosNormalWorld);
		LastStamp.LocalFrame = LastStamp.WorldFrame;
		LastStamp.LocalFrame.Transform(CurTargetTransform.Inverse());
		LastStamp.Radius = CurrentBrushRadius;
		LastStamp.Direction = (bInvert) ? -1.0 : 1.0;
		LastStamp.Depth = BrushProperties->Depth;
		LastStamp.Power = GetActivePressure() * ((bSmoothing) ? SculptProperties->SmoothBrushSpeed : SculptProperties->PrimaryBrushSpeed);
		TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = (bSmoothing) ? SecondaryBrushOp : PrimaryBrushOp;

		FSculptBrushOptions SculptOptions;
		SculptOptions.bPreserveUVFlow = SculptProperties->bPreserveUVFlow;
		SculptOptions.MaxHeight = CurrentBrushRadius * SculptMaxBrushProperties->MaxHeight;
		if (SculptMaxBrushProperties->bFreezeCurrentHeight && SculptMaxFixedHeight >= 0)
		{
			SculptOptions.MaxHeight = SculptMaxFixedHeight;
		}
		SculptMaxFixedHeight = SculptOptions.MaxHeight;		// yech
		SculptOptions.WhichPlaneSideIndex = (int32)PlaneBrushProperties->WhichSide;
		SculptOptions.ConstantReferencePlane = ActiveFixedBrushPlane;
		UseBrushOp->ConfigureOptions(SculptOptions);

		UseBrushOp->BeginStroke(DynamicMeshComponent->GetMesh(), LastStamp, VertexROI);

		AccumulatedTriangleROI.Reset();

		// apply initial stamp
		PendingStampRay = Ray;
		bStampPending = true;
	}
}



void UMeshVertexSculptTool::UpdateROI(const FVector3d& BrushPos)
{
	SCOPE_CYCLE_COUNTER(VtxSculptTool_UpdateROI);

	// TODO: need dynamic vertex hash table?

	float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;

	FAxisAlignedBox3d BrushBox(
		BrushPos - CurrentBrushRadius * FVector3d::One(),
		BrushPos + CurrentBrushRadius * FVector3d::One());

	VertexSetBuffer.Reset();
	TriangleROI.Reset();
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	Octree.RangeQuery(BrushBox,
		[&](int TriIdx) {
		FIndex3i TriV = Mesh->GetTriangle(TriIdx);
		int Count = 0;
		for (int j = 0; j < 3; ++j)
		{
			if (VertexSetBuffer.Contains(TriV[j]))
			{
				Count++;
			}
			else if (BrushPos.DistanceSquared(Mesh->GetVertex(TriV[j])) < RadiusSqr)
			{
				VertexSetBuffer.Add(TriV[j]);
				Count++;
			}
		}
		if (Count > 0)
		{
			TriangleROI.Add(TriIdx);
		}
	});

	VertexROI.SetNum(0, false);
	BufferUtil::AppendElements(VertexROI, VertexSetBuffer);

	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);
}

void UMeshVertexSculptTool::OnUpdateDrag(const FRay& WorldRay)
{
	if (bInDrag)
	{
		PendingStampRay = WorldRay;
		bStampPending = true;
	}
}


bool UMeshVertexSculptTool::UpdateStampPosition(const FRay& WorldRay)
{
	StampTimestamp++;

	CalculateBrushRadius();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = (bSmoothing) ? SecondaryBrushOp : PrimaryBrushOp;
	UseBrushOp->Falloff = ActiveFalloff;

	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
		UpdateBrushPositionOnSculptMesh(WorldRay, true);
		break;
	case ESculptBrushOpTargetType::TargetMesh:
		UpdateBrushPositionOnTargetMesh(WorldRay, true);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		UpdateBrushPositionOnActivePlane(WorldRay);
		break;
	}

	if (UseBrushOp->GetAlignStampToView())
	{
		AlignBrushToView();
	}

	CurrentStamp = LastStamp;
	CurrentStamp.WorldFrame = FFrame3d(LastBrushPosWorld, LastBrushPosNormalWorld);
	CurrentStamp.LocalFrame = CurrentStamp.WorldFrame;
	CurrentStamp.LocalFrame.Transform(CurTargetTransform.Inverse());
	CurrentStamp.Power = GetActivePressure() * ((bSmoothing) ? SculptProperties->SmoothBrushSpeed : SculptProperties->PrimaryBrushSpeed);

	CurrentStamp.PrevLocalFrame = LastStamp.LocalFrame;
	CurrentStamp.PrevWorldFrame = LastStamp.WorldFrame;

	FVector3d MoveDelta = CurrentStamp.LocalFrame.Origin - CurrentStamp.PrevLocalFrame.Origin;
	if (UseBrushOp->IgnoreZeroMovements() && MoveDelta.SquaredLength() < FMathd::ZeroTolerance)
	{
		return false;
	}

	LastBrushPosLocal = CurrentStamp.LocalFrame.Origin;

	return true;
}


void UMeshVertexSculptTool::ApplyStamp()
{
	SCOPE_CYCLE_COUNTER(VtxSculptToolApplyStamp);

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = (bSmoothing) ? SecondaryBrushOp : PrimaryBrushOp;
	if (UseBrushOp->WantsStampRegionPlane())
	{
		CurrentStamp.RegionPlane = ComputeROIBrushPlane(CurrentStamp.LocalFrame.Origin, true, false);
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	UseBrushOp->ApplyStamp(Mesh, CurrentStamp, VertexROI, ROIPositionBuffer);

	SyncMeshWithPositionBuffer(Mesh);

	LastStamp = CurrentStamp;
}



double UMeshVertexSculptTool::CalculateBrushFalloff(double Distance)
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



void UMeshVertexSculptTool::SyncMeshWithPositionBuffer(FDynamicMesh3* Mesh)
{
	const int NumV = ROIPositionBuffer.Num();
	checkSlow(VertexROI.Num() <= NumV);

	// actually doing this in parallel is slower here due to locking (?)
	ParallelFor(NumV, [&](int32 k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);

		//UpdateSavedVertexLock.Lock();
		ActiveVertexChange->UpdateVertex(VertIdx, OrigPos, NewPos);
		//UpdateSavedVertexLock.Unlock();
	}, true);
}








FFrame3d UMeshVertexSculptTool::ComputeROIBrushPlane(const FVector3d& BrushCenter, bool bIgnoreDepth, bool bViewAligned)
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




int UMeshVertexSculptTool::FindHitSculptMeshTriangle(const FRay3d& LocalRay)
{
	if (BrushProperties->bHitBackFaces)
	{
		return Octree.FindNearestHitObject(LocalRay);
	}
	else
	{
		FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition(StateOut.Position));
		int HitTID = Octree.FindNearestHitObject(LocalRay,
			[this, Mesh, &LocalEyePosition](int TriangleID) {
			FVector3d Normal, Centroid;
			double Area;
			Mesh->GetTriInfo(TriangleID, Normal, Area, Centroid);
			return Normal.Dot((Centroid - LocalEyePosition)) < 0;
		});
		return HitTID;
	}
}

int UMeshVertexSculptTool::FindHitTargetMeshTriangle(const FRay3d& LocalRay)
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

		return BaseMeshSpatial.FindNearestHitObject(LocalRay, RaycastOptions.TriangleFilterF);
	}
	else
	{
		return BaseMeshSpatial.FindNearestHitObject(LocalRay);
	}
}

bool UMeshVertexSculptTool::UpdateBrushPositionOnActivePlane(const FRay& WorldRay)
{
	FVector3d NewHitPosWorld;
	ActiveDragPlane.RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, 2, NewHitPosWorld);
	LastBrushPosWorld = NewHitPosWorld;
	LastBrushPosNormalWorld = ActiveDragPlane.Z();
	return true;
}

bool UMeshVertexSculptTool::UpdateBrushPositionOnTargetMesh(const FRay& WorldRay, bool bFallbackToViewPlane)
{
	FRay3d LocalRay(CurTargetTransform.InverseTransformPosition(WorldRay.Origin),
		CurTargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	int32 HitTID = FindHitTargetMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		FIntrRay3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleIntersection(BaseMesh, HitTID, LocalRay);
		LastBrushPosNormalWorld = CurTargetTransform.TransformNormal(BaseMesh.GetTriNormal(HitTID));
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

bool UMeshVertexSculptTool::UpdateBrushPositionOnSculptMesh(const FRay& WorldRay, bool bFallbackToViewPlane)
{
	FRay3d LocalRay(CurTargetTransform.InverseTransformPosition(WorldRay.Origin),
		CurTargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	int32 HitTID = FindHitSculptMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		const FDynamicMesh3* SculptMesh = DynamicMeshComponent->GetMesh();
		FIntrRay3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleIntersection(*SculptMesh, HitTID, LocalRay);
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

void UMeshVertexSculptTool::AlignBrushToView()
{
	LastBrushPosNormalWorld = -CameraState.Forward();
}


bool UMeshVertexSculptTool::UpdateBrushPosition(const FRay& WorldRay)
{
	// This is an unfortunate hack necessary because we haven't refactored brushes properly yet

	if (bSmoothing)
	{
		return UpdateBrushPositionOnSculptMesh(WorldRay, false);
	}

	bool bHit = false;
	switch (SculptProperties->PrimaryBrushType)
	{
	case EMeshVertexSculptBrushType::Offset:
	case EMeshVertexSculptBrushType::SculptMax:
	case EMeshVertexSculptBrushType::Pinch:
		bHit = UpdateBrushPositionOnTargetMesh(WorldRay, false);
		break;

	case EMeshVertexSculptBrushType::SculptView:
	case EMeshVertexSculptBrushType::PlaneViewAligned:
		bHit = UpdateBrushPositionOnTargetMesh(WorldRay, false);
		AlignBrushToView();
		break;

	case EMeshVertexSculptBrushType::Move:
		//return UpdateBrushPositionOnActivePlane(WorldRay);
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;

	case EMeshVertexSculptBrushType::Smooth:
	case EMeshVertexSculptBrushType::Inflate:
	case EMeshVertexSculptBrushType::Flatten:
	case EMeshVertexSculptBrushType::Plane:
	case EMeshVertexSculptBrushType::FixedPlane:
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;

	default:
		UE_LOG(LogTemp, Warning, TEXT("UMeshVertexSculptTool: unknown brush type in UpdateBrushPosition"));
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	}

	return bHit;
}



void UMeshVertexSculptTool::OnEndDrag(const FRay& Ray)
{
	bInDrag = false;

	// cancel these! otherwise change record could become invalid
	bStampPending = false;

	// update spatial
	bTargetDirty = true;

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = (bSmoothing) ? SecondaryBrushOp : PrimaryBrushOp;
	UseBrushOp->EndStroke(DynamicMeshComponent->GetMesh(), LastStamp, VertexROI);

	// close change record
	EndChange();
}


FInputRayHit UMeshVertexSculptTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	return UMeshSurfacePointTool::BeginHoverSequenceHitTest(PressPos);
}

bool UMeshVertexSculptTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
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
	}
	return true;
}



void UMeshVertexSculptTool::OnTick(float DeltaTime)
{
	UMeshSculptToolBase::OnTick(DeltaTime);

	SCOPE_CYCLE_COUNTER(VtxSculptToolTick);

	UpdateHoverStamp(LastBrushPosWorld, LastBrushPosNormalWorld);

	BrushTypeWatcher.CheckAndUpdate();

	UpdateWorkPlane();

	// process the undo update
	if (bUndoUpdatePending)
	{
		// wait for updates
		WaitForPendingUndoRedo();

		// post rendering update
		DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(AccumulatedTriangleROI,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals);
		GetToolManager()->PostInvalidation();

		// ignore stamp and wait for next tick to do anything else
		bUndoUpdatePending = false;
		return;
	}

	// if user changed to not-frozen, we need to reinitialize the target
	if (bCachedFreezeTarget != SculptProperties->bFreezeTarget)
	{
		UpdateBaseMesh(nullptr);
		bTargetDirty = false;
	}


	if (bStampPending)
	{
		//UE_LOG(LogTemp, Warning, TEXT("dt is %.3f, tick fps %.2f - roi size %d/%d"), DeltaTime, 1.0 / DeltaTime, VertexROI.Num(), TriangleROI.Num());
		SCOPE_CYCLE_COUNTER(VtxSculptTool_Tick_ApplyStampBlock);
		// hardcoded brush flow (todo: make optional)
		bStampPending = bInDrag;

		// update brush position
		if (UpdateStampPosition(PendingStampRay) == false)
		{
			return;
		}

		// update sculpt ROI
		UpdateROI(CurrentStamp.LocalFrame.Origin);

		// append updated ROI to modified region (async)
		TFuture<void> AccumulateROI = Async(VertexSculptToolAsyncExecTarget, [&]()
		{
			AccumulatedTriangleROI.Append(TriangleROI);
		});

		// apply the stamp
		ApplyStamp();

		// begin octree rebuild calculation
		TFuture<void> OctreeRebuild = Async(VertexSculptToolAsyncExecTarget, [&]()
		{
			SCOPE_CYCLE_COUNTER(VtxSculptTool_Tick_ApplyStamp_Insert);
			Octree.ReinsertTriangles(TriangleROI);
		});

		// recalculate normals. This has to complete before we can update component
		// (in fact we could do it per-chunk...)
		FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
		{
			SCOPE_CYCLE_COUNTER(VtxSculptTool_Tick_NormalsBlock);
			UE::SculptUtil::RecalculateROINormals(Mesh, TriangleROI, VertexSetBuffer, NormalsBuffer);
		}

		{
			SCOPE_CYCLE_COUNTER(VtxSculptTool_Tick_UpdateMeshBlock);
			DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TriangleROI,
				EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals);
			GetToolManager()->PostInvalidation();
		}

		// we don't really need to wait for these to happen to end Tick()...
		AccumulateROI.Wait();
		OctreeRebuild.Wait();
	} 
	else if (bTargetDirty)
	{
		SCOPE_CYCLE_COUNTER(VtxSculptTool_Tick_UpdateTargetBlock);
		check(bInDrag == false);

		// this spawns futures that we could allow to run while other things happen...
		UpdateBaseMesh(&AccumulatedTriangleROI);
		AccumulatedTriangleROI.Reset();

		bTargetDirty = false;
	}

}




void UMeshVertexSculptTool::UpdateBaseMesh(const TSet<int32>* TriangleSet)
{
	if (SculptProperties != nullptr)
	{
		bCachedFreezeTarget = SculptProperties->bFreezeTarget;
		if (SculptProperties->bFreezeTarget)
		{
			return;   // do not update frozen target
		}
	}

	const FDynamicMesh3* SculptMesh = DynamicMeshComponent->GetMesh();
	if ( ! TriangleSet )
	{
		BaseMesh.Copy(*SculptMesh, false, false, false, false);
		BaseMesh.EnableVertexNormals(FVector3f::UnitZ());
		FMeshNormals::QuickComputeVertexNormals(BaseMesh);
		BaseMeshSpatial.SetMaxTreeDepth(8);
		BaseMeshSpatial = FDynamicMeshOctree3();   // need to clear...
		BaseMeshSpatial.Initialize(&BaseMesh);
	}
	else
	{
		BaseMeshIndexBuffer.Reset();
		for ( int32 tid : *TriangleSet)
		{ 
			FIndex3i Tri = BaseMesh.GetTriangle(tid);
			BaseMesh.SetVertex(Tri.A, SculptMesh->GetVertex(Tri.A));
			BaseMesh.SetVertex(Tri.B, SculptMesh->GetVertex(Tri.B));
			BaseMesh.SetVertex(Tri.C, SculptMesh->GetVertex(Tri.C));
			BaseMeshIndexBuffer.Add(tid);
		}
		auto UpdateBaseNormals = Async(EAsyncExecution::Thread, [&]()
		{
			FMeshNormals::QuickComputeVertexNormalsForTriangles(BaseMesh, BaseMeshIndexBuffer);
		});
		auto ReinsertTriangles = Async(EAsyncExecution::Thread, [&]()
		{
			BaseMeshSpatial.ReinsertTriangles(*TriangleSet);
		});
		UpdateBaseNormals.Wait();
		ReinsertTriangles.Wait();
	}
}


bool UMeshVertexSculptTool::GetBaseMeshNearest(int32 VertexID, const FVector3d& Position, double SearchRadius, FVector3d& TargetPosOut, FVector3d& TargetNormalOut)
{
	TargetPosOut = BaseMesh.GetVertex(VertexID);
	TargetNormalOut = (FVector3d)BaseMesh.GetVertexNormal(VertexID);
	return true;

	// Octree FindNearestObject is currently quite expensive...
	//int32 NearestTID = BaseMeshSpatial.FindNearestObject(Position, SearchRadius);
	//if (NearestTID <= 0)
	//{
	//	return false;
	//}
	//FDistPoint3Triangle3d DistanceQuery = TMeshQueries<FDynamicMesh3>::TriangleDistance(BaseMesh, NearestTID, Position);
	//TargetPosOut = DistanceQuery.ClosestTrianglePoint;
	//TargetNormalOut = BaseMesh.GetTriBaryNormal(NearestTID, DistanceQuery.TriangleBaryCoords.X, DistanceQuery.TriangleBaryCoords.Y, DistanceQuery.TriangleBaryCoords.Z);
	//return true;
}






void UMeshVertexSculptTool::IncreaseBrushSpeedAction()
{
	SculptProperties->PrimaryBrushSpeed = FMath::Clamp(SculptProperties->PrimaryBrushSpeed + 0.05f, 0.0f, 1.0f);
}

void UMeshVertexSculptTool::DecreaseBrushSpeedAction()
{
	SculptProperties->PrimaryBrushSpeed = FMath::Clamp(SculptProperties->PrimaryBrushSpeed - 0.05f, 0.0f, 1.0f);
}



void UMeshVertexSculptTool::NextBrushModeAction()
{
	uint8 LastMode = (uint8)EMeshVertexSculptBrushType::LastValue;
	SculptProperties->PrimaryBrushType = (EMeshVertexSculptBrushType)(((uint8)SculptProperties->PrimaryBrushType + 1) % LastMode);
}

void UMeshVertexSculptTool::PreviousBrushModeAction()
{
	uint8 LastMode = (uint8)EMeshVertexSculptBrushType::LastValue;
	uint8 CurMode = (uint8)SculptProperties->PrimaryBrushType;
	if (CurMode == 0)
	{
		SculptProperties->PrimaryBrushType = (EMeshVertexSculptBrushType)((uint8)LastMode - 1);
	}
	else
	{
		SculptProperties->PrimaryBrushType = (EMeshVertexSculptBrushType)((uint8)CurMode - 1);
	}
}



//
// Change Tracking
//
void UMeshVertexSculptTool::BeginChange()
{
	check(ActiveVertexChange == nullptr);
	ActiveVertexChange = new FMeshVertexChangeBuilder();
}

void UMeshVertexSculptTool::EndChange()
{
	check(ActiveVertexChange);

	TUniquePtr<TWrappedToolCommandChange<FMeshVertexChange>> NewChange = MakeUnique<TWrappedToolCommandChange<FMeshVertexChange>>();
	NewChange->WrappedChange = MoveTemp(ActiveVertexChange->Change);
	NewChange->BeforeModify = [this](bool bRevert)
	{
		this->WaitForPendingUndoRedo();
	};

	GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(NewChange), LOCTEXT("VertexSculptChange", "Brush Stroke"));

	delete ActiveVertexChange;
	ActiveVertexChange = nullptr;
}


void UMeshVertexSculptTool::WaitForPendingUndoRedo()
{
	if (bUndoUpdatePending)
	{
		UndoNormalsFuture.Wait();
		UndoUpdateOctreeFuture.Wait();
		UndoUpdateBaseMeshFuture.Wait();
		bUndoUpdatePending = false;
	}
}

void UMeshVertexSculptTool::OnDynamicMeshComponentChanged(USimpleDynamicMeshComponent* Component, const FMeshVertexChange* Change, bool bRevert)
{
	// update octree
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	// make sure any previous async computations are done, and update the undo ROI
	if (bUndoUpdatePending)
	{
		// we should never hit this anymore, because of pre-change calling WaitForPendingUndoRedo()
		WaitForPendingUndoRedo();

		// this is not right because now we are going to do extra recomputation, but it's very messy otherwise...
		MeshIndexUtil::VertexToTriangleOneRing(Mesh, Change->Vertices, AccumulatedTriangleROI);
	}
	else
	{
		AccumulatedTriangleROI.Reset();
		MeshIndexUtil::VertexToTriangleOneRing(Mesh, Change->Vertices, AccumulatedTriangleROI);
	}

	// start the normal recomputation
	UndoNormalsFuture = Async(EAsyncExecution::Thread, [&]()
	{
		UE::SculptUtil::RecalculateROINormals(Mesh, AccumulatedTriangleROI, VertexSetBuffer, NormalsBuffer);
		return true;
	});

	// start the octree update
	UndoUpdateOctreeFuture = Async(EAsyncExecution::Thread, [&]()
	{
		Octree.ReinsertTriangles(AccumulatedTriangleROI);
		return true;
	});

	// start the base mesh update
	UndoUpdateBaseMeshFuture = Async(EAsyncExecution::Thread, [&]()
	{
		UpdateBaseMesh(&AccumulatedTriangleROI);
		return true;
	});

	// note that we have a pending update
	bUndoUpdatePending = true;
}










void UMeshVertexSculptTool::UpdateBrushType(EMeshVertexSculptBrushType BrushType)
{
	static const FText BaseMessage = LOCTEXT("OnStartSculptTool", "Hold Shift to Smooth, Ctrl to Invert (where applicable). Q/A keys cycle Brush Type. S/D changes Size (+Shift to small-step), W/E changes Strength.");
	FTextBuilder Builder;
	Builder.AppendLine(BaseMessage);

	TUniqueFunction<bool(int32, const FVector3d&, double MaxDist, FVector3d&, FVector3d&)> BaseMeshQueryFunc =
		[&](int32 VertexID, const FVector3d& Position, double MaxDist, FVector3d& PosOut, FVector3d& NormalOut)
		{
			//return GetTargetMeshNearest(VertexID, Position, MaxDist, PosOut, NormalOut);
			return GetBaseMeshNearest(VertexID, Position, MaxDist, PosOut, NormalOut);
		};

	switch (BrushType)
	{
	case EMeshVertexSculptBrushType::Smooth:
		PrimaryBrushOp = MakeUnique<FSmoothBrushOp>();
		break;
	case EMeshVertexSculptBrushType::Offset:
		PrimaryBrushOp = MakeUnique<FSurfaceSculptBrushOp>(MoveTemp(BaseMeshQueryFunc));
		break;
	case EMeshVertexSculptBrushType::SculptView:
		PrimaryBrushOp = MakeUnique<FViewAlignedSculptBrushOp>(MoveTemp(BaseMeshQueryFunc));
		break;
	case EMeshVertexSculptBrushType::SculptMax:
		PrimaryBrushOp = MakeUnique<FSurfaceMaxSculptBrushOp>(MoveTemp(BaseMeshQueryFunc));
		break;
	case EMeshVertexSculptBrushType::Inflate:
		PrimaryBrushOp = MakeUnique<FInflateBrushOp>();
		break;
	case EMeshVertexSculptBrushType::Move:
		PrimaryBrushOp = MakeUnique<FMoveBrushOp>();
		break;
	case EMeshVertexSculptBrushType::Flatten:
		PrimaryBrushOp = MakeUnique<FFlattenBrushOp>();
		break;
	case EMeshVertexSculptBrushType::Plane:
		PrimaryBrushOp = MakeUnique<FPlaneBrushOp>();
		break;
	case EMeshVertexSculptBrushType::PlaneViewAligned:
		PrimaryBrushOp = MakeUnique<FPlaneBrushOp>();
		break;
	case EMeshVertexSculptBrushType::FixedPlane:
		PrimaryBrushOp = MakeUnique<FPlaneBrushOp>();
		break;
	case EMeshVertexSculptBrushType::Pinch:
		PrimaryBrushOp = MakeUnique<FPinchBrushOp>();
		break;
	}
	PrimaryBrushOp->Falloff = ActiveFalloff;


	SetToolPropertySourceEnabled(GizmoProperties, false);
	SetToolPropertySourceEnabled(PlaneBrushProperties, false);
	SetToolPropertySourceEnabled(SculptMaxBrushProperties, false);

	if (BrushType == EMeshVertexSculptBrushType::FixedPlane)
	{
		Builder.AppendLine(LOCTEXT("FixedPlaneTip", "Use T to reposition Work Plane at cursor, Shift+T to align to Normal, Ctrl+Shift+T to align to View"));
		SetToolPropertySourceEnabled(PlaneBrushProperties, true);
		SetToolPropertySourceEnabled(GizmoProperties, true);
	}
	if (BrushType == EMeshVertexSculptBrushType::Plane || BrushType == EMeshVertexSculptBrushType::PlaneViewAligned || BrushType == EMeshVertexSculptBrushType::Flatten)
	{
		SetToolPropertySourceEnabled(PlaneBrushProperties, true);
	}
	if (BrushType == EMeshVertexSculptBrushType::SculptMax)
	{
		SetToolPropertySourceEnabled(SculptMaxBrushProperties, true);
	}

	GetToolManager()->DisplayMessage(Builder.ToText(), EToolMessageLevel::UserNotification);
}



#undef LOCTEXT_NAMESPACE
