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
#include "AssetUtils/Texture2DUtil.h"

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
		Octree.RootDimension = Bounds.MaxDim() / 2.0;
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
	SculptProperties->RestoreProperties(this);
	AddToolPropertySource(SculptProperties);
	CalculateBrushRadius();

	AlphaProperties = NewObject<UVertexBrushAlphaProperties>(this);
	AlphaProperties->RestoreProperties(this);
	AddToolPropertySource(AlphaProperties);

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

	SculptProperties->WatchProperty(AlphaProperties->Alpha,
		[this](UTexture2D* NewAlpha) { UpdateBrushAlpha(AlphaProperties->Alpha); });

	// must call before updating brush type so that we register all brush properties?
	UMeshSculptToolBase::OnCompleteSetup();

	UpdateBrushType(SculptProperties->PrimaryBrushType);
	SetPrimaryFalloffType(SculptProperties->PrimaryFalloffType);
	UpdateBrushAlpha(AlphaProperties->Alpha);
	SetActiveSecondaryBrushType((int32)EMeshVertexSculptBrushType::Smooth);

	StampRandomStream = FRandomStream(31337);
}

void UMeshVertexSculptTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);
	}

	SculptProperties->SaveProperties(this);
	AlphaProperties->SaveProperties(this);

	// this call will commit result, unregister and destroy DynamicMeshComponent
	UMeshSculptToolBase::Shutdown(ShutdownType);
}


void UMeshVertexSculptTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	CalculateBrushRadius();
}



void UMeshVertexSculptTool::OnBeginStroke(const FRay& WorldRay)
{
	WaitForPendingUndoRedo();		// cannot start stroke if there is an outstanding undo/redo update

	UpdateBrushPosition(WorldRay);

	if (SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::Plane ||
		SculptProperties->PrimaryBrushType == EMeshVertexSculptBrushType::PlaneViewAligned)
	{
		UpdateROI(GetBrushFrameLocal().Origin);
		UpdateStrokeReferencePlaneForROI(GetBrushFrameLocal(), TriangleROIArray,
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
	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_UpdateROI);

	float RadiusSqr = GetCurrentBrushRadius() * GetCurrentBrushRadius();
	FAxisAlignedBox3d BrushBox(
		BrushPos - GetCurrentBrushRadius() * FVector3d::One(),
		BrushPos + GetCurrentBrushRadius() * FVector3d::One());

	// do a parallel range quer
	RangeQueryTriBuffer.Reset();
	FDynamicMesh3* Mesh = GetSculptMesh();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_UpdateROI_RangeQuery);
		Octree.ParallelRangeQuery(BrushBox, RangeQueryTriBuffer);
	}

#if 1
	// in this path we use more memory but this lets us do more in parallel

	// Construct array of inside/outside flags for each triangle's vertices. If no
	// vertices are inside, clear the triangle ID from the range query buffer.
	// This can be done in parallel and it's cheaper to do repeated distance computations
	// than to try to do it inside the ROI building below (todo: profile this some more?)
	TriangleROIInBuf.SetNum(RangeQueryTriBuffer.Num(), false);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_TriVerts);
		ParallelFor(RangeQueryTriBuffer.Num(), [&](int k)
		{
			const FIndex3i& TriV = Mesh->GetTriangleRef(RangeQueryTriBuffer[k]);
			TriangleROIInBuf[k].A = (BrushPos.DistanceSquared(Mesh->GetVertexRef(TriV.A)) < RadiusSqr) ? 1 : 0;
			TriangleROIInBuf[k].B = (BrushPos.DistanceSquared(Mesh->GetVertexRef(TriV.B)) < RadiusSqr) ? 1 : 0;
			TriangleROIInBuf[k].C = (BrushPos.DistanceSquared(Mesh->GetVertexRef(TriV.C)) < RadiusSqr) ? 1 : 0;
			if (TriangleROIInBuf[k].A + TriangleROIInBuf[k].B + TriangleROIInBuf[k].C == 0)
			{
				RangeQueryTriBuffer[k] = -1;
			}
		});
	}

	// collect set of vertices inside brush sphere, from that box
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_3Collect);
		VertexROIBuilder.Initialize(Mesh->MaxVertexID());
		TriangleROIBuilder.Initialize(Mesh->MaxTriangleID());
		int32 N = RangeQueryTriBuffer.Num();
		for ( int32 k = 0; k < N; ++k )
		{
			int32 tid = RangeQueryTriBuffer[k];
			if (tid == -1) continue;		// triangle was deleted in previous step
			const FIndex3i& TriV = Mesh->GetTriangleRef(RangeQueryTriBuffer[k]);
			const FIndex3i& Inside = TriangleROIInBuf[k];
			int InsideCount = 0;
			for (int j = 0; j < 3; ++j)
			{
				if (Inside[j])
				{
					VertexROIBuilder.Add(TriV[j]);
					InsideCount++;
				}
			}
			if (InsideCount > 0)
			{
				TriangleROIBuilder.Add(tid);
			}
		}
		VertexROIBuilder.SwapValuesWith(VertexROI);
		TriangleROIBuilder.SwapValuesWith(TriangleROIArray);
	}

#else
	// In this path we combine everything into one loop. Does fewer distance checks
	// but nothing can be done in parallel (would change if ROIBuilders had atomic-try-add)

	// collect set of vertices and triangles inside brush sphere, from range query result
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_2Collect);
		VertexROIBuilder.Initialize(Mesh->MaxVertexID());
		TriangleROIBuilder.Initialize(Mesh->MaxTriangleID());
		for (int32 TriIdx : RangeQueryTriBuffer)
		{
			FIndex3i TriV = Mesh->GetTriangle(TriIdx);
			int InsideCount = 0;
			for (int j = 0; j < 3; ++j)
			{
				if (VertexROIBuilder.Contains(TriV[j]))
				{
					InsideCount++;
				} 
				else if (BrushPos.DistanceSquared(Mesh->GetVertexRef(TriV[j])) < RadiusSqr)
				{
					VertexROIBuilder.Add(TriV[j]);
					InsideCount++;
				}
			}
			if (InsideCount > 0)
			{
				TriangleROIBuilder.Add(tid);
			}
		}
		VertexROIBuilder.SwapValuesWith(VertexROI);
		TriangleROIBuilder.SwapValuesWith(TriangleROIArray);
	}
#endif

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMeshSculptTool_UpdateROI_4ROI);
		ROIPositionBuffer.SetNum(VertexROI.Num(), false);
		ROIPrevPositionBuffer.SetNum(VertexROI.Num(), false);
		ParallelFor(VertexROI.Num(), [&](int i)
		{
			ROIPrevPositionBuffer[i] = Mesh->GetVertexRef(VertexROI[i]);
		});
	}
}

bool UMeshVertexSculptTool::UpdateStampPosition(const FRay& WorldRay)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_UpdateStampPosition);

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
	//CurrentStamp.DeltaTime = FMathd::Min((FDateTime::Now() - LastStamp.TimeStamp).GetTotalSeconds(), 1.0);
	CurrentStamp.DeltaTime = 0.03;		// 30 fps - using actual time is no good now that we support variable stamps!
	CurrentStamp.WorldFrame = GetBrushFrameWorld();
	CurrentStamp.LocalFrame = GetBrushFrameLocal();
	CurrentStamp.Power = GetActivePressure() * GetCurrentBrushStrength();

	if (bHaveBrushAlpha && (AlphaProperties->RotationAngle != 0 || AlphaProperties->bRandomize))
	{
		float UseAngle = AlphaProperties->RotationAngle;
		if (AlphaProperties->bRandomize)
		{
			UseAngle += (StampRandomStream.GetFraction() - 0.5f) * 2.0f * AlphaProperties->RandomRange;
		}

		// possibly should be done in base brush...
		CurrentStamp.WorldFrame.Rotate(FQuaterniond(CurrentStamp.WorldFrame.Z(), UseAngle, true));
		CurrentStamp.LocalFrame.Rotate(FQuaterniond(CurrentStamp.LocalFrame.Z(), UseAngle, true));
	}

	CurrentStamp.PrevLocalFrame = LastStamp.LocalFrame;
	CurrentStamp.PrevWorldFrame = LastStamp.WorldFrame;

	FVector3d MoveDelta = CurrentStamp.LocalFrame.Origin - CurrentStamp.PrevLocalFrame.Origin;
	if (UseBrushOp->IgnoreZeroMovements() && MoveDelta.SquaredLength() < FMathd::ZeroTolerance)
	{
		return false;
	}

	return true;
}


TFuture<void> UMeshVertexSculptTool::ApplyStamp()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_ApplyStamp);

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	// compute region plane if necessary. This may currently be expensive?
	if (UseBrushOp->WantsStampRegionPlane())
	{
		CurrentStamp.RegionPlane = ComputeStampRegionPlane(CurrentStamp.LocalFrame, TriangleROIArray, true, false, false);
	}

	// set up alpha function if we have one
	if (bHaveBrushAlpha)
	{
		CurrentStamp.StampAlphaFunc = [this](const FSculptBrushStamp& Stamp, const FVector3d& Position)
		{
			return this->SampleBrushAlpha(Stamp, Position);
		};
	}

	// apply the stamp, which computes new positions
	FDynamicMesh3* Mesh = GetSculptMesh();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_ApplyStamp_Apply);
		UseBrushOp->ApplyStamp(Mesh, CurrentStamp, VertexROI, ROIPositionBuffer);
	}

	// can discard alpha now
	CurrentStamp.StampAlphaFunc = nullptr;

	// once stamp is applied, we can start updating vertex change, which can happen async as we saved all necessary info
	TFuture<void> SaveVertexFuture;
	if (ActiveVertexChange != nullptr)
	{
		SaveVertexFuture = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_SyncMeshWithPositionBuffer_UpdateChange);
			const int32 NumV = ROIPositionBuffer.Num();
			for (int k = 0; k < NumV; ++k)
			{
				int VertIdx = VertexROI[k];
				ActiveVertexChange->UpdateVertex(VertIdx, ROIPrevPositionBuffer[k], ROIPositionBuffer[k]);
			}
		});
	}

	// now actually update the mesh, which happens 
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_ApplyStamp_Sync);
		const int32 NumV = ROIPositionBuffer.Num();
		ParallelFor(NumV, [&](int32 k)
		{
			int VertIdx = VertexROI[k];
			const FVector3d& NewPos = ROIPositionBuffer[k];
			Mesh->SetVertex_NoTimeStampUpdate(VertIdx, NewPos);
		});
		Mesh->IncrementTimeStamps(1, true, false);
	}

	LastStamp = CurrentStamp;
	LastStamp.TimeStamp = FDateTime::Now();

	// let caller wait for this to finish
	return SaveVertexFuture;
}







int32 UMeshVertexSculptTool::FindHitSculptMeshTriangle(const FRay3d& LocalRay)
{
	// need this to finish before we can touch Octree
	WaitForPendingStampUpdate();

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

	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick);

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

	if (InStroke())
	{
		//UE_LOG(LogTemp, Warning, TEXT("dt is %.3f, tick fps %.2f - roi size %d/%d"), DeltaTime, 1.0 / DeltaTime, VertexROI.Num(), TriangleROI.Num());
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_StrokeUpdate);
		FDynamicMesh3* Mesh = GetSculptMesh();

		// update brush position
		if (UpdateStampPosition(GetPendingStampRayWorld()) == false)
		{
			return;
		}
		UpdateStampPendingState();
		if (IsStampPending() == false)
		{
			return;
		}

		// need to make sure previous stamp finished
		WaitForPendingStampUpdate();

		// update sculpt ROI
		UpdateROI(CurrentStamp.LocalFrame.Origin);

		// Append updated ROI to modified region (async). For some reason this is very expensive,
		// maybe because of TSet? but we have a lot of time to do it.
		TFuture<void> AccumulateROI = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_AccumROI);
			for (int32 tid : TriangleROIArray)
			{
				AccumulatedTriangleROI.Add(tid);
			}
		});

		// Start precomputing the normals ROI. This is currently the most expensive single thing we do next
		// to Octree re-insertion, despite it being almost trivial. Why?!?
		bool bUsingOverlayNormalsOut = false;
		TFuture<void> NormalsROI = Async(VertexSculptToolAsyncExecTarget, [Mesh, &bUsingOverlayNormalsOut, this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_NormalsROI);

			//UE::SculptUtil::PrecalculateNormalsROI(Mesh, TriangleROIArray, NormalsROIBuilder, bUsingOverlayNormals, false);
			UE::SculptUtil::PrecalculateNormalsROI(Mesh, TriangleROIArray, NormalsFlags, bUsingOverlayNormalsOut, false);
		});

		// NOTE: you might try to speculatively do the octree remove here, to save doing it later on Reinsert().
		// This will not improve things, as Reinsert() checks if it needs to actually re-insert, which avoids many
		// removes, and does much of the work of Remove anyway.

		// Apply the stamp. This will return a future that is updating the vertex-change record, 
		// which can run until the end of the frame, as it is using cached information
		TFuture<void> UpdateChangeFuture = ApplyStamp();

		// begin octree rebuild calculation
		StampUpdateOctreeFuture = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_OctreeReinsert);
			Octree.ReinsertTrianglesParallel(TriangleROIArray, OctreeUpdateTempBuffer, OctreeUpdateTempFlagBuffer);
		});
		bStampUpdatePending = true;
		//TFuture<void> OctreeRebuild = Async(VertexSculptToolAsyncExecTarget, [&]()
		//{
		//	TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_OctreeReinsert);
		//	Octree.ReinsertTriangles(TriangleROIArray);
		//});

		// TODO: first step of RecalculateROINormals() is to convert TriangleROI into vertex or element ROI.
		// We can do this while we are computing stamp!

		// precompute dynamic mesh update info
		TArray<int32> RenderUpdateSets; FAxisAlignedBox3d RenderUpdateBounds;
		TFuture<bool> RenderUpdatePrecompute = DynamicMeshComponent->FastNotifyTriangleVerticesUpdated_TryPrecompute(
				TriangleROIArray, RenderUpdateSets, RenderUpdateBounds);

		// recalculate normals. This has to complete before we can update component
		// (in fact we could do it per-chunk...)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_RecalcNormals);
			NormalsROI.Wait();
			UE::SculptUtil::RecalculateROINormals(Mesh, NormalsFlags, bUsingOverlayNormalsOut);
			//UE::SculptUtil::RecalculateROINormals(Mesh, NormalsROIBuilder.Indices(), bUsingOverlayNormals);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_UpdateMesh);
			RenderUpdatePrecompute.Wait();
			DynamicMeshComponent->FastNotifyTriangleVerticesUpdated_ApplyPrecompute(TriangleROIArray,
				EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals,
				RenderUpdatePrecompute, RenderUpdateSets, RenderUpdateBounds);

			GetToolManager()->PostInvalidation();
		}

		// we don't really need to wait for these to happen to end Tick()...
		UpdateChangeFuture.Wait();
		AccumulateROI.Wait();
	} 
	else if (bTargetDirty)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Tick_UpdateTarget);
		check(InStroke() == false);

		// this spawns futures that we could allow to run while other things happen...
		UpdateBaseMesh(&AccumulatedTriangleROI);
		AccumulatedTriangleROI.Reset();

		bTargetDirty = false;
	}

}



void UMeshVertexSculptTool::WaitForPendingStampUpdate()
{
	if (bStampUpdatePending)
	{
		StampUpdateOctreeFuture.Wait();
		bStampUpdatePending = true;
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
		TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Target_FullUpdate);
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
		auto UpdateBaseNormals = Async(VertexSculptToolAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Target_UpdateBaseNormals);
			FMeshNormals::QuickComputeVertexNormalsForTriangles(BaseMesh, BaseMeshIndexBuffer);
		});
		auto ReinsertTriangles = Async(VertexSculptToolAsyncExecTarget, [TriangleSet, this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VtxSculptTool_Target_Reinsert);
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

void UMeshVertexSculptTool::UpdateBrushAlpha(UTexture2D* NewAlpha)
{
	if (this->BrushAlpha != NewAlpha)
	{
		this->BrushAlpha = NewAlpha;
		if (this->BrushAlpha != nullptr)
		{
			TImageBuilder<FVector4f> AlphaValues;
			FImageDimensions AlphaDimensions;

			bool bReadOK = UE::AssetUtils::ReadTexture(this->BrushAlpha, AlphaDimensions, AlphaValues, true);
			if (bReadOK)
			{
				BrushAlphaValues = MoveTemp(AlphaValues);
				BrushAlphaDimensions = AlphaDimensions;
				bHaveBrushAlpha = true;
				return;
			}
		}
		bHaveBrushAlpha = false;
		BrushAlphaValues = TImageBuilder<FVector4f>();
		BrushAlphaDimensions = FImageDimensions();
	}
}


double UMeshVertexSculptTool::SampleBrushAlpha(const FSculptBrushStamp& Stamp, const FVector3d& Position) const
{
	if (! bHaveBrushAlpha) return 1.0;

	static const FVector4f InvalidValue(0, 0, 0, 0);

	FVector2d AlphaUV = Stamp.LocalFrame.ToPlaneUV(Position, 2);
	double u = AlphaUV.X / Stamp.Radius;
	u = 1.0 - (u + 1.0) / 2.0;
	double v = AlphaUV.Y / Stamp.Radius;
	v = 1.0 - (v + 1.0) / 2.0;
	if (u < 0 || u > 1) return 0.0;
	if (v < 0 || v > 1) return 0.0;
	FVector4f AlphaValue = BrushAlphaValues.BilinearSampleUV<float>(FVector2d(u, v), InvalidValue);
	return FMathd::Clamp(AlphaValue.X, 0.0, 1.0);
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
	// have to wait for any outstanding stamp update to finish...
	WaitForPendingStampUpdate();
	// wait for previous Undo to finish (possibly never hit because the change records do it?)
	WaitForPendingUndoRedo();

	FDynamicMesh3* Mesh = GetSculptMesh();

	// figure out the set of modified triangles
	AccumulatedTriangleROI.Reset();
	MeshIndexUtil::VertexToTriangleOneRing(Mesh, Change->Vertices, AccumulatedTriangleROI);

	// start the normal recomputation
	UndoNormalsFuture = Async(VertexSculptToolAsyncExecTarget, [this, Mesh]()
	{
		UE::SculptUtil::RecalculateROINormals(Mesh, AccumulatedTriangleROI, NormalsROIBuilder);
		return true;
	});

	// start the octree update
	UndoUpdateOctreeFuture = Async(VertexSculptToolAsyncExecTarget, [this, Mesh]()
	{
		Octree.ReinsertTriangles(AccumulatedTriangleROI);
		return true;
	});

	// start the base mesh update
	UndoUpdateBaseMeshFuture = Async(VertexSculptToolAsyncExecTarget, [this, Mesh]()
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


	bool bEnableAlpha = false;
	switch (BrushType)
	{
	case EMeshVertexSculptBrushType::Offset:
	case EMeshVertexSculptBrushType::SculptView:
	case EMeshVertexSculptBrushType::SculptMax:
		bEnableAlpha = true; break;
	}
	SetToolPropertySourceEnabled(AlphaProperties, bEnableAlpha);


	GetToolManager()->DisplayMessage(Builder.ToText(), EToolMessageLevel::UserNotification);
}



#undef LOCTEXT_NAMESPACE
