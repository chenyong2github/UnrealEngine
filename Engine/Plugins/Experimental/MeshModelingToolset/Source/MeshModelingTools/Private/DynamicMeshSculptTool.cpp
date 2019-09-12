// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshSculptTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "SubRegionRemesher.h"
#include "ProjectionTargets.h"
#include "MeshConstraints.h"
#include "MeshConstraintsUtil.h"
#include "MeshVertexChange.h"
#include "MeshWeights.h"
#include "MeshNormals.h"
#include "MeshIndexUtil.h"
#include "Drawing/MeshDebugDrawing.h"

#include "MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "DynamicMeshChangeTracker.h"

#include "Async/ParallelFor.h"

#define LOCTEXT_NAMESPACE "UDynamicMeshSculptTool"


/*
 * ToolBuilder
 */

UMeshSurfacePointTool* UDynamicMeshSculptToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UDynamicMeshSculptTool* SculptTool = NewObject<UDynamicMeshSculptTool>(SceneState.ToolManager);
	SculptTool->SetEnableRemeshing(this->bEnableRemeshing);
	return SculptTool;
}



UBrushSculptProperties::UBrushSculptProperties()
{
	SmoothPower = 0.25;
	OffsetPower = 0.5;
	PrimaryBrushType = EDynamicMeshSculptBrushType::Pull;
	SmoothingType = EMeshSculptToolSmoothType::TexturePreserving;
	Depth = 0;
}



UBrushRemeshProperties::UBrushRemeshProperties()
{
	RelativeSize = 1.0;
	Smoothing = 0.1;
}


/*
 * Tool
 */

UDynamicMeshSculptTool::UDynamicMeshSculptTool()
{
	// initialize parameters
	bEnableRemeshing = true;
}


void UDynamicMeshSculptTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<UOctreeDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMeshSculptToolMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());

	// copy material if there is one
	auto Material = ComponentTarget->GetMaterial(0);
	if (Material != nullptr)
	{
		DynamicMeshComponent->SetMaterial(0, Material);
	}

	// initialize from LOD-0 MeshDescription
	//DynamicMeshComponent->TangentsType = (bEnableRemeshing) ? EDynamicMeshTangentCalcType::NoTangents : EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshChanged.Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UDynamicMeshSculptTool::OnDynamicMeshComponentChanged));

	// initialize AABBTree
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	// do we always want to keep vertex normals updated?
	FMeshNormals::QuickComputeVertexNormals(*Mesh);

	// switch to vertex normals for testing
	//DynamicMeshComponent->GetMesh()->DiscardAttributes();

	UpdateTarget();
	bTargetDirty = false;

	double MaxDimension = DynamicMeshComponent->GetMesh()->GetCachedBounds().MaxDim();
	BrushRelativeSizeRange = FInterval1d(MaxDimension*0.01, 2*MaxDimension);
	BrushProperties = NewObject<UBrushBaseProperties>(this, TEXT("Brush"));
	CalculateBrushRadius();

	SculptProperties = NewObject<UBrushSculptProperties>(this, TEXT("Sculpting"));

	RemeshProperties = NewObject<UBrushRemeshProperties>(this, TEXT("Remeshing"));
	InitialEdgeLength = EstimateIntialSafeTargetLength(*Mesh, 5000);

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// init state flags flags
	bInDrag = false;
	bHaveRemeshed = false;
	bRemeshPending = false;
	bStampPending = false;
	ActiveVertexChange = nullptr;

	// create indicators
	Indicators = NewObject<UToolIndicatorSet>(this, "Indicators");
	Indicators->Connect(this);

	UBrushStampSizeIndicator* StampIndicator = NewObject<UBrushStampSizeIndicator>(this, "Brush Circle");
	StampIndicator->bDrawSecondaryLines = true;
	StampIndicator->DepthLayer = 1;
	StampIndicator->BrushRadius = MakeAttributeLambda([this] { return (float)this->CurrentBrushRadius; });
	StampIndicator->BrushPosition = MakeAttributeLambda([this] { return this->LastBrushPosWorld; });
	StampIndicator->BrushNormal = MakeAttributeLambda([this] { return this->LastBrushPosNormalWorld; });
	Indicators->AddIndicator(StampIndicator);

	// initialize our properties
	AddToolPropertySource(SculptProperties);
	AddToolPropertySource(BrushProperties);
	if (this->bEnableRemeshing)
	{
		AddToolPropertySource(RemeshProperties);
	}
}




void UDynamicMeshSculptTool::Shutdown(EToolShutdownType ShutdownType)
{
	Indicators->Disconnect();

	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("SculptMeshToolTransactionName", "Sculpt Mesh"));
			ComponentTarget->CommitMesh([=](FMeshDescription* MeshDescription)
			{
				DynamicMeshComponent->Bake(MeshDescription, bHaveRemeshed);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}

}



void UDynamicMeshSculptTool::OnDynamicMeshComponentChanged()
{
	bNormalUpdatePending = true;
	bTargetDirty = true;
}


void UDynamicMeshSculptTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	CalculateBrushRadius();
}



bool UDynamicMeshSculptTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	FTransform Transform = ComponentTarget->GetWorldTransform();

	FRay3d LocalRay(Transform.InverseTransformPosition(Ray.Origin),
		Transform.InverseTransformVector(Ray.Direction));
	LocalRay.Direction.Normalize();

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int HitTID = DynamicMeshComponent->GetOctree()->FindNearestHitObject(LocalRay);

	if (HitTID != IndexConstants::InvalidID)
	{
		FTriangle3d Triangle;
		Mesh->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		OutHit.FaceIndex = HitTID;
		OutHit.Distance = Query.RayParameter;
		OutHit.Normal = Transform.TransformVectorNoScale(Mesh->GetTriNormal(HitTID));
		OutHit.ImpactPoint = Transform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		return true;
	}

	return false;
}



void UDynamicMeshSculptTool::OnBeginDrag(const FRay& Ray)
{
	bSmoothing = GetShiftToggle();

	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		BrushStartCenterWorld = Ray.PointAt(OutHit.Distance) + SculptProperties->Depth*CurrentBrushRadius*Ray.Direction;

		bInDrag = true;

		ActiveDragPlane = FPlane(BrushStartCenterWorld, -Ray.Direction);
		LastHitPosWorld = FMath::RayPlaneIntersection(Ray.Origin, Ray.Direction, ActiveDragPlane);

		LastBrushPosWorld = LastHitPosWorld;
		LastBrushPosNormalWorld = ActiveDragPlane;
		FTransform Transform = ComponentTarget->GetWorldTransform();
		LastBrushPosLocal = Transform.InverseTransformPosition(LastHitPosWorld);

		BeginChange(bEnableRemeshing == false);

		UpdateROI(LastBrushPosLocal);

		if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::Flatten)
		{
			ComputeFlattenFrame();
		}

		// apply initial stamp
		PendingStampRay = Ray;
		bStampPending = true;
	}
}



void UDynamicMeshSculptTool::UpdateROI(const FVector& BrushPos)
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
			ApplyOffsetBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Pull:
			ApplyMoveBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Pinch:
			ApplyPinchBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Flatten:
			ApplyFlattenBrush(WorldRay);
			break;
	}
}




void UDynamicMeshSculptTool::ApplySmoothBrush(const FRay& WorldRay)
{
	// both these are useful...

	//bool bHit = UpdateBrushPositionOnActivePlane(WorldRay);
	bool bHit = UpdateBrushPositionOnTargetMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}


	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector NewBrushPosLocal = Transform.InverseTransformPosition(LastBrushPosWorld);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	for (int VertIdx : VertexROI)
	{
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);

		double UseDist = (OrigPos - NewBrushPosLocal).Length();
		double d = UseDist / CurrentBrushRadius;
		double w = 1;
		if (d > 0.5) 
		{
			d = VectorUtil::Clamp((d - 0.5) / 0.5, 0.0, 1.0);
			w = (1 - d * d);
			w = w * w * w;
		}

		FVector3d SmoothedPos = (SculptProperties->SmoothingType == EMeshSculptToolSmoothType::TexturePreserving) ?
			FMeshWeights::MeanValueCentroid(*Mesh, VertIdx) : FMeshWeights::UniformCentroid(*Mesh, VertIdx);

		FVector3d NewPos = FVector3d::Lerp(OrigPos, SmoothedPos, w*SculptProperties->SmoothPower);

		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;
	LastBrushPosLocal = NewBrushPosLocal;
}





void UDynamicMeshSculptTool::ApplyMoveBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnActivePlane(WorldRay);

	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector NewBrushPosLocal = Transform.InverseTransformPosition(LastBrushPosWorld);

	FVector3d MoveVec = NewBrushPosLocal - LastBrushPosLocal;

	if (MoveVec.SquaredLength() > 0)
	{
		FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
		for (int VertIdx : VertexROI)
		{
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			double PrevDist = (OrigPos - LastBrushPosLocal).Length();
			double NewDist = (OrigPos - NewBrushPosLocal).Length();
			double UseDist = FMath::Min(PrevDist, NewDist);
			double d = UseDist / CurrentBrushRadius;
			double w = 1;
			if (d > 0.5) 
			{
				d = VectorUtil::Clamp((d - 0.5) / 0.5, 0.0, 1.0);
				w = (1 - d * d);
				w = w * w * w;
			}

			FVector3d NewPos = OrigPos + w * MoveVec;

			Mesh->SetVertex(VertIdx, NewPos);
			UpdateSavedVertex(VertIdx, OrigPos, NewPos);
		}

		bRemeshPending = bEnableRemeshing;
	}

	LastBrushPosLocal = NewBrushPosLocal;
}






void UDynamicMeshSculptTool::ApplyOffsetBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnTargetMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector NewBrushPosLocal = Transform.InverseTransformPosition(LastBrushPosWorld);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	for (int VertIdx : VertexROI)
	{
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);

		FVector3d BasePos, BaseNormal;
		GetTargetMeshNearest(OrigPos, (double)(2 * CurrentBrushRadius), BasePos, BaseNormal);

		FVector3d MoveVec = SculptProperties->OffsetPower*FMathd::Sqrt(CurrentBrushRadius)*BaseNormal;

		double UseDist = (OrigPos - NewBrushPosLocal).Length();
		double d = UseDist / CurrentBrushRadius;
		double w = 1;
		if (d > 0.5) 
		{
			d = VectorUtil::Clamp((d - 0.5) / 0.5, 0.0, 1.0);
			w = (1 - d * d);
			w = w * w * w;
		}

		FVector3d NewPos = OrigPos + w * MoveVec;

		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}




void UDynamicMeshSculptTool::ApplyPinchBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnTargetMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector NewBrushPosLocal = Transform.InverseTransformPosition(LastBrushPosWorld);
	FVector BrushNormalLocal = Transform.InverseTransformVectorNoScale(LastBrushPosNormalWorld);
	FVector OffsetBrushPosLocal = NewBrushPosLocal - SculptProperties->Depth * CurrentBrushRadius * BrushNormalLocal;

	FVector3d MotionVec = NewBrushPosLocal - LastBrushPosLocal;
	MotionVec.Normalize();

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	for (int VertIdx : VertexROI)
	{
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d Delta = OffsetBrushPosLocal - OrigPos;
		FVector3d MoveVec = SculptProperties->OffsetPower*Delta;

		double UseDist = (OrigPos - NewBrushPosLocal).Length();
		double d = UseDist / CurrentBrushRadius;
		double w = 1;
		if (d > 0.5)
		{
			d = VectorUtil::Clamp((d - 0.5) / 0.5, 0.0, 1.0);
			w = (1 - d * d);
			w = w * w * w;
		}
		w = w * w * w;

		double AnglePower = 1.0 - FMathd::Abs(MoveVec.Normalized().Dot(MotionVec));
		w *= AnglePower;

		FVector3d NewPos = OrigPos + w * MoveVec;

		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}




void UDynamicMeshSculptTool::ComputeFlattenFrame()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FVector3d AverageNormal(0, 0, 0);
	FVector3d AveragePos(0, 0, 0);
	for (int TriID : TriangleROI)
	{
		AverageNormal += Mesh->GetTriNormal(TriID);
		AveragePos += Mesh->GetTriCentroid(TriID);
	}
	AverageNormal.Normalize();
	AveragePos /= TriangleROI.Num();
	ActiveFlattenFrame = FFrame3d(AveragePos, AverageNormal);

	ActiveFlattenFrame.Origin -= SculptProperties->Depth * CurrentBrushRadius * ActiveFlattenFrame.Z();
}

void UDynamicMeshSculptTool::ApplyFlattenBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnTargetMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector NewBrushPosLocal = Transform.InverseTransformPosition(LastBrushPosWorld);
	FVector BrushNormalLocal = Transform.InverseTransformVectorNoScale(LastBrushPosNormalWorld);
	FVector OffsetBrushPosLocal = NewBrushPosLocal - SculptProperties->Depth * CurrentBrushRadius * BrushNormalLocal;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	for (int VertIdx : VertexROI)
	{
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d PlanePos = ActiveFlattenFrame.ToPlane(OrigPos, 2);
		FVector3d Delta = PlanePos - OrigPos;
		FVector3d MoveVec = FMathd::Sqrt(SculptProperties->OffsetPower) * Delta;

		double UseDist = (OrigPos - NewBrushPosLocal).Length();
		double d = UseDist / CurrentBrushRadius;
		double w = 1;
		if (d > 0.5)
		{
			d = VectorUtil::Clamp((d - 0.5) / 0.5, 0.0, 1.0);
			w = (1 - d * d);
			w = w * w * w;
		}

		FVector3d NewPos = OrigPos + w * MoveVec;

		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}







bool UDynamicMeshSculptTool::UpdateBrushPositionOnActivePlane(const FRay& WorldRay)
{
	FVector NewHitPosWorld = FMath::RayPlaneIntersection(WorldRay.Origin, WorldRay.Direction, ActiveDragPlane);
	LastBrushPosWorld = NewHitPosWorld;
	LastBrushPosNormalWorld = ActiveDragPlane;
	return true;
}

bool UDynamicMeshSculptTool::UpdateBrushPositionOnTargetMesh(const FRay& WorldRay)
{
	FTransform Transform = ComponentTarget->GetWorldTransform();

	FRay3d LocalRay(Transform.InverseTransformPosition(WorldRay.Origin),
		Transform.InverseTransformVectorNoScale(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	int HitTID = BrushTargetMeshSpatial.FindNearestHitTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		const FDynamicMesh3* TargetMesh = BrushTargetMeshSpatial.GetMesh();

		FTriangle3d Triangle;
		TargetMesh->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		LastBrushPosNormalWorld = Transform.TransformVectorNoScale(TargetMesh->GetTriNormal(HitTID));
		LastBrushPosWorld = Transform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		return true;
	}
	return false;
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


void UDynamicMeshSculptTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (bInDrag)
	{
		FVector NewHitPosWorld = FMath::RayPlaneIntersection(DevicePos.WorldRay.Origin, DevicePos.WorldRay.Direction, ActiveDragPlane);
		LastBrushPosWorld = NewHitPosWorld;
		LastBrushPosNormalWorld = ActiveDragPlane;
	}
	else
	{
		FHitResult OutHit;
		if (HitTest(DevicePos.WorldRay, OutHit))
		{
			LastBrushPosWorld = DevicePos.WorldRay.PointAt(OutHit.Distance + SculptProperties->Depth*CurrentBrushRadius);
			LastBrushPosNormalWorld = OutHit.Normal;
		}
	}
}




void UDynamicMeshSculptTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);
	Indicators->Render(RenderAPI);

	//MeshDebugDraw::DrawNormals(DynamicMeshComponent->GetMesh()->Attributes()->PrimaryNormals(),
	//	25.0f, FColor::Red, 5.0f, true, RenderAPI->GetPrimitiveDrawInterface(), .GetWorldTransform() );
}


void UDynamicMeshSculptTool::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_SculptToolTick);

	UMeshSurfacePointTool::Tick(DeltaTime);
	Indicators->Tick(DeltaTime);

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



void UDynamicMeshSculptTool::RemeshROIPass()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();

	FSubRegionRemesher Remesher(Mesh);
	Remesher.SetTargetEdgeLength(RemeshProperties->RelativeSize * InitialEdgeLength);

	// this is a temporary tweak for pinch brush. Remesh params should be per-brush!
	if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::Pinch && bSmoothing == false)
	{
		Remesher.MinEdgeLength = Remesher.MinEdgeLength / 4.0;
	}

	Remesher.SmoothSpeedT = RemeshProperties->Smoothing;
	Remesher.SmoothType = (SculptProperties->SmoothingType == EMeshSculptToolSmoothType::TexturePreserving) ?
		FRemesher::ESmoothTypes::MeanValue : FRemesher::ESmoothTypes::Uniform;
	bool bIsUniformSmooth = (Remesher.SmoothType == FRemesher::ESmoothTypes::Uniform);

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

	FMeshConstraints constraints;
	bool bConstraintAllowSplits = true;
	bool bConstraintAllowSmoothing = false;
	{
		SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_Constraints);

		// TODO: only constrain in ROI. This is quite difficult to do externally because we need to update based on
		// the changing triangle set in Remesher. Perhaps FSubRegionRemesher should update the constraints itself?

		FMeshConstraintsUtil::ConstrainAllSeams(constraints, *Mesh, bConstraintAllowSplits, bConstraintAllowSmoothing);
		Remesher.SetExternalConstraints(&constraints);
	}

	for (int k = 0; k < 5; ++k)
	{
		if (bIsUniformSmooth == false)
		{
			Remesher.bEnableFlips = (k < 2);
		}

		{
			SCOPE_CYCLE_COUNTER(STAT_SculptTool_Remesh_RemeshROIUpdate);

			Remesher.UpdateROI();

			if (ActiveMeshChange != nullptr)
			{
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
		for (int vid : VertexROI)
		{
			ActiveMeshChange->SaveVertex(vid);
		}
	}
}


void UDynamicMeshSculptTool::UpdateSavedVertex(int vid, const FVector3d& OldPosition, const FVector3d& NewPosition)
{
	if (ActiveVertexChange != nullptr)
	{
		ActiveVertexChange->UpdateVertex(vid, OldPosition, NewPosition);
	}
}


#undef LOCTEXT_NAMESPACE
