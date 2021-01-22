// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshVertexSculptTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"

#include "MeshWeights.h"
#include "MeshNormals.h"
#include "MeshIndexUtil.h"
#include "Util/BufferUtil.h"

#include "Changes/MeshVertexChange.h"

#include "Sculpting/KelvinletBrushOp.h"
#include "Sculpting/MeshSmoothingBrushOps.h"
#include "Sculpting/MeshInflateBrushOps.h"
#include "Sculpting/MeshMoveBrushOps.h"
#include "Sculpting/MeshPlaneBrushOps.h"
#include "Sculpting/MeshPinchBrushOps.h"
#include "Sculpting/MeshSculptBrushOps.h"
#include "Sculpting/StampFalloffs.h"
#include "Sculpting/MeshSculptUtil.h"

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
	InitializeSculptMeshComponent(DynamicMeshComponent);

	// assign materials
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->bInvalidateProxyOnChange = false;
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshVerticesChanged.AddUObject(this, &UMeshVertexSculptTool::OnDynamicMeshComponentChanged);

	// initialize dynamic octree
	FDynamicMesh3* Mesh = GetSculptMesh();
	FAxisAlignedBox3d Bounds = Mesh->GetCachedBounds();
	if (Mesh->TriangleCount() > 100000)
	{
		Octree.RootDimension = Bounds.MaxDim() / 10.0;
		Octree.SetMaxTreeDepth(4);
	} 
	else
	{
		Octree.RootDimension = Bounds.MaxDim();
		Octree.SetMaxTreeDepth(8);
	}
	Octree.Initialize(Mesh);
	//Octree.CheckValidity(EValidityCheckFailMode::Check, true, true);
	//FDynamicMeshOctree3::FStatistics Stats;
	//Octree.ComputeStatistics(Stats);
	//UE_LOG(LogTemp, Warning, TEXT("Octree Stats: %s"), *Stats.ToString());

	// initialize render decomposition
	TUniquePtr<FMeshRenderDecomposition> Decomp = MakeUnique<FMeshRenderDecomposition>();
	FMeshRenderDecomposition::BuildChunkedDecomposition(Mesh, &MaterialSet, *Decomp);
	Decomp->BuildAssociations(Mesh);
	//UE_LOG(LogTemp, Warning, TEXT("Decomposition has %d groups"), Decomp->Num());
	DynamicMeshComponent->SetExternalDecomposition(MoveTemp(Decomp));

	// initialize target mesh
	UpdateBaseMesh(nullptr);
	bTargetDirty = false;

	// initialize brush radius range interval, brush properties
	UMeshSculptToolBase::InitializeBrushSizeRange(Bounds);

	// initialize other properties
	SculptProperties = NewObject<UVertexBrushSculptProperties>(this);

	// init state flags flags
	ActiveVertexChange = nullptr;

	InitializeIndicator();

	// initialize our properties
	AddToolPropertySource(UMeshSculptToolBase::BrushProperties);
	UMeshSculptToolBase::BrushProperties->bShowPerBrushProps = false;
	UMeshSculptToolBase::BrushProperties->bShowFalloff = false;
	AddToolPropertySource(SculptProperties);
	CalculateBrushRadius();
	SculptProperties->RestoreProperties(this);

	this->BaseMeshQueryFunc = [&](int32 VertexID, const FVector3d& Position, double MaxDist, FVector3d& PosOut, FVector3d& NormalOut)
	{
		return GetBaseMeshNearest(VertexID, Position, MaxDist, PosOut, NormalOut);
	};

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Smooth,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSmoothBrushOp>>(),
		NewObject<USmoothBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::SmoothFill,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSmoothFillBrushOp>>(),
		NewObject<USmoothFillBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Move,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FMoveBrushOp>>(),
		NewObject<UMoveBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Offset,
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FSurfaceSculptBrushOp>(BaseMeshQueryFunc); }),
		NewObject<UStandardSculptBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::SculptView,
		MakeUnique<FLambdaMeshSculptBrushOpFactory>( [this]() { return MakeUnique<FViewAlignedSculptBrushOp>(BaseMeshQueryFunc); } ),
		NewObject<UViewAlignedSculptBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::SculptMax,
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FSurfaceMaxSculptBrushOp>(BaseMeshQueryFunc); }),
		NewObject<USculptMaxBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Inflate,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FInflateBrushOp>>(),
		NewObject<UInflateBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Pinch,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPinchBrushOp>>(),
		NewObject<UPinchBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Flatten,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FFlattenBrushOp>>(),
		NewObject<UFlattenBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::Plane,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPlaneBrushOp>>(),
		NewObject<UPlaneBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::PlaneViewAligned,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPlaneBrushOp>>(),
		NewObject<UViewAlignedPlaneBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::FixedPlane,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPlaneBrushOp>>(),
		NewObject<UFixedPlaneBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::ScaleKelvin ,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FScaleKelvinletBrushOp>>(),
		NewObject<UScaleKelvinletBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::PullKelvin,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FPullKelvinletBrushOp>>(),
		NewObject<UPullKelvinletBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::PullSharpKelvin,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSharpPullKelvinletBrushOp>>(),
		NewObject<USharpPullKelvinletBrushOpProps>(this));

	RegisterBrushType((int32)EMeshVertexSculptBrushType::TwistKelvin,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FTwistKelvinletBrushOp>>(),
		NewObject<UTwistKelvinletBrushOpProps>(this));

	// secondary brushes
	RegisterSecondaryBrushType((int32)EMeshVertexSculptBrushType::Smooth,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FSmoothBrushOp>>(),
		NewObject<USecondarySmoothBrushOpProps>(this));


	AddToolPropertySource(UMeshSculptToolBase::GizmoProperties);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::GizmoProperties, false);

	AddToolPropertySource(UMeshSculptToolBase::ViewProperties);

	// register watchers
	SculptProperties->WatchProperty( SculptProperties->PrimaryBrushType,
		[this](EMeshVertexSculptBrushType NewType) { UpdateBrushType(NewType); });

	SculptProperties->WatchProperty( SculptProperties->PrimaryFalloffType,
		[this](EMeshSculptFalloffType NewType) { SetPrimaryFalloffType(NewType); });

	// must call before updating brush type so that we register all brush properties?
	UMeshSculptToolBase::OnCompleteSetup();

	UpdateBrushType(SculptProperties->PrimaryBrushType);
	SetActiveSecondaryBrushType((int32)EMeshVertexSculptBrushType::Smooth);
}

void UMeshVertexSculptTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);
	}

	SculptProperties->SaveProperties(this);

	// this call will commit result, unregister and destroy DynamicMeshComponent
	UMeshSculptToolBase::Shutdown(ShutdownType);
}


void UMeshVertexSculptTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	CalculateBrushRadius();
}



void UMeshVertexSculptTool::OnBeginStroke(const FRay& WorldRay)
{
	UpdateBrushPosition(WorldRay);

	if (SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::Plane ||
		SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::PlaneViewAligned)
	{
		UpdateROI(GetBrushFrameLocal().Origin);
		UpdateStrokeReferencePlaneForROI(GetBrushFrameLocal(), TriangleROI,
			(SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::PlaneViewAligned));
	}
	else if (SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::FixedPlane)
	{
		UpdateStrokeReferencePlaneFromWorkPlane();
	}

	// initialize first "Last Stamp", so that we can assume all stamps in stroke have a valid previous stamp
	LastStamp.WorldFrame = GetBrushFrameWorld();
	LastStamp.LocalFrame = GetBrushFrameLocal();
	LastStamp.Radius = GetCurrentBrushRadius();
	LastStamp.Falloff = GetCurrentBrushFalloff();
	LastStamp.Direction = GetInInvertStroke() ? -1.0 : 1.0;
	LastStamp.Depth = GetCurrentBrushDepth();
	LastStamp.Power = GetActivePressure() * GetCurrentBrushStrength();
	LastStamp.TimeStamp = FDateTime::Now();

	FSculptBrushOptions SculptOptions;
	//SculptOptions.bPreserveUVFlow = false; // SculptProperties->bPreserveUVFlow;
	SculptOptions.ConstantReferencePlane = GetCurrentStrokeReferencePlane();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	UseBrushOp->ConfigureOptions(SculptOptions);
	UseBrushOp->BeginStroke(GetSculptMesh(), LastStamp, VertexROI);

	AccumulatedTriangleROI.Reset();

	// begin change here? or wait for first stamp?
	BeginChange();
}

void UMeshVertexSculptTool::OnEndStroke()
{
	// update spatial
	bTargetDirty = true;

	GetActiveBrushOp()->EndStroke(GetSculptMesh(), LastStamp, VertexROI);

	// close change record
	EndChange();
}



void UMeshVertexSculptTool::UpdateROI(const FVector3d& BrushPos)
{
	SCOPE_CYCLE_COUNTER(VtxSculptTool_UpdateROI);

	float RadiusSqr = GetCurrentBrushRadius() * GetCurrentBrushRadius();
	FAxisAlignedBox3d BrushBox(
		BrushPos - GetCurrentBrushRadius() * FVector3d::One(),
		BrushPos + GetCurrentBrushRadius() * FVector3d::One());

	VertexSetBuffer.Reset();
	TriangleROI.Reset();
	FDynamicMesh3* Mesh = GetSculptMesh();
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

	ROIPositionBuffer.SetNum(VertexROI.Num(), false);
}

bool UMeshVertexSculptTool::UpdateStampPosition(const FRay& WorldRay)
{
	CalculateBrushRadius();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

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
	CurrentStamp.DeltaTime = FMathd::Min((FDateTime::Now() - LastStamp.TimeStamp).GetTotalSeconds(), 1.0);
	CurrentStamp.WorldFrame = GetBrushFrameWorld();
	CurrentStamp.LocalFrame = GetBrushFrameLocal();
	CurrentStamp.Power = GetActivePressure() * GetCurrentBrushStrength();

	CurrentStamp.PrevLocalFrame = LastStamp.LocalFrame;
	CurrentStamp.PrevWorldFrame = LastStamp.WorldFrame;

	FVector3d MoveDelta = CurrentStamp.LocalFrame.Origin - CurrentStamp.PrevLocalFrame.Origin;
	if (UseBrushOp->IgnoreZeroMovements() && MoveDelta.SquaredLength() < FMathd::ZeroTolerance)
	{
		return false;
	}

	return true;
}


void UMeshVertexSculptTool::ApplyStamp()
{
	SCOPE_CYCLE_COUNTER(VtxSculptToolApplyStamp);

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	if (UseBrushOp->WantsStampRegionPlane())
	{
		CurrentStamp.RegionPlane = ComputeStampRegionPlane(CurrentStamp.LocalFrame, TriangleROI, true, false, false);
	}

	FDynamicMesh3* Mesh = GetSculptMesh();
	UseBrushOp->ApplyStamp(Mesh, CurrentStamp, VertexROI, ROIPositionBuffer);

	SyncMeshWithPositionBuffer(Mesh);

	LastStamp = CurrentStamp;
	LastStamp.TimeStamp = FDateTime::Now();
}




void UMeshVertexSculptTool::SyncMeshWithPositionBuffer(FDynamicMesh3* Mesh)
{
	const int32 NumV = ROIPositionBuffer.Num();
	checkSlow(VertexROI.Num() <= NumV);

	// change update could be async here if we collected array of <idx,orig,new> and dispatched independenlty
	for ( int32 k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);

		ActiveVertexChange->UpdateVertex(VertIdx, OrigPos, NewPos);
	}
}






int32 UMeshVertexSculptTool::FindHitSculptMeshTriangle(const FRay3d& LocalRay)
{
	if (GetBrushCanHitBackFaces())
	{
		return Octree.FindNearestHitObject(LocalRay);
	}
	else
	{
		FDynamicMesh3* Mesh = GetSculptMesh();

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

int32 UMeshVertexSculptTool::FindHitTargetMeshTriangle(const FRay3d& LocalRay)
{
	IMeshSpatial::FQueryOptions RaycastOptions;

	if (GetBrushCanHitBackFaces())
	{
		FDynamicMesh3* Mesh = GetSculptMesh();

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



bool UMeshVertexSculptTool::UpdateBrushPosition(const FRay& WorldRay)
{
	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	bool bHit = false; 
	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	case ESculptBrushOpTargetType::TargetMesh:
		bHit = UpdateBrushPositionOnTargetMesh(WorldRay, false);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		//UpdateBrushPositionOnActivePlane(WorldRay);
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	}

	if (bHit && UseBrushOp->GetAlignStampToView())
	{
		AlignBrushToView();
	}

	return bHit;
}




bool UMeshVertexSculptTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	// 4.26 HOTFIX: update LastWorldRay position so that we have it for updating WorkPlane position
	UMeshSurfacePointTool::LastWorldRay = DevicePos.WorldRay;

	PendingStampType = SculptProperties->PrimaryBrushType;
	if(ensure(InStroke() == false))
	{
		UpdateBrushPosition(DevicePos.WorldRay);
	}
	return true;
}



void UMeshVertexSculptTool::OnTick(float DeltaTime)
{
	UMeshSculptToolBase::OnTick(DeltaTime);

	SCOPE_CYCLE_COUNTER(VtxSculptToolTick);

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


	if (IsStampPending())
	{
		//UE_LOG(LogTemp, Warning, TEXT("dt is %.3f, tick fps %.2f - roi size %d/%d"), DeltaTime, 1.0 / DeltaTime, VertexROI.Num(), TriangleROI.Num());
		SCOPE_CYCLE_COUNTER(VtxSculptTool_Tick_ApplyStampBlock);

		ApplyStrokeFlowInTick();

		// update brush position
		if (UpdateStampPosition(GetPendingStampRayWorld()) == false)
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
		FDynamicMesh3* Mesh = GetSculptMesh();
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
		check(InStroke() == false);

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

	const FDynamicMesh3* SculptMesh = GetSculptMesh();
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
}






void UMeshVertexSculptTool::IncreaseBrushSpeedAction()
{
	//SculptProperties->PrimaryBrushSpeed = FMath::Clamp(SculptProperties->PrimaryBrushSpeed + 0.05f, 0.0f, 1.0f);
}

void UMeshVertexSculptTool::DecreaseBrushSpeedAction()
{
	//SculptProperties->PrimaryBrushSpeed = FMath::Clamp(SculptProperties->PrimaryBrushSpeed - 0.05f, 0.0f, 1.0f);
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
	FDynamicMesh3* Mesh = GetSculptMesh();

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
	static const FText BaseMessage = LOCTEXT("OnStartSculptTool", "Hold Shift to Smooth, Ctrl to Invert (where applicable). [/] and S/D change Size (+Shift to small-step), W/E changes Strength.");
	FTextBuilder Builder;
	Builder.AppendLine(BaseMessage);

	SetActivePrimaryBrushType((int32)BrushType);

	SetToolPropertySourceEnabled(GizmoProperties, false);
	if (BrushType == EMeshVertexSculptBrushType::FixedPlane)
	{
		Builder.AppendLine(LOCTEXT("FixedPlaneTip", "Use T to reposition Work Plane at cursor, Shift+T to align to Normal, Ctrl+Shift+T to align to View"));
		SetToolPropertySourceEnabled(GizmoProperties, true);
	}

	GetToolManager()->DisplayMessage(Builder.ToText(), EToolMessageLevel::UserNotification);
}



#undef LOCTEXT_NAMESPACE
