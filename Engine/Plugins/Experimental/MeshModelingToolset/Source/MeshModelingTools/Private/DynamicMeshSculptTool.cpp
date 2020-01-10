// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshSculptTool.h"
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

#include "Async/ParallelFor.h"
#include "ToolDataVisualizer.h"
#include "Components/PrimitiveComponent.h"
#include "Generators/SphereGenerator.h"

#include "InteractiveGizmoManager.h"
#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"


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



UBrushSculptProperties::UBrushSculptProperties()
{
	SmoothSpeed = 0.25;
	BrushSpeed = 0.5;
	PrimaryBrushType = EDynamicMeshSculptBrushType::Move;
	bPreserveUVFlow = false;
	BrushDepth = 0;
	bFreezeTarget = false;
	bHitBackFaces = true;
}

void UBrushSculptProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UBrushSculptProperties* PropertyCache = GetPropertyCache<UBrushSculptProperties>();
	PropertyCache->SmoothSpeed = this->SmoothSpeed;
	PropertyCache->BrushSpeed = this->BrushSpeed;
	PropertyCache->PrimaryBrushType = this->PrimaryBrushType;
	PropertyCache->bPreserveUVFlow = this->bPreserveUVFlow;
	PropertyCache->BrushDepth = this->BrushDepth;
	PropertyCache->bHitBackFaces = this->bHitBackFaces;
}

void UBrushSculptProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UBrushSculptProperties* PropertyCache = GetPropertyCache<UBrushSculptProperties>();
	this->SmoothSpeed = PropertyCache->SmoothSpeed;
	this->BrushSpeed = PropertyCache->BrushSpeed;
	this->PrimaryBrushType = PropertyCache->PrimaryBrushType;
	this->bPreserveUVFlow = PropertyCache->bPreserveUVFlow;
	this->BrushDepth = PropertyCache->BrushDepth;
	this->bHitBackFaces = PropertyCache->bHitBackFaces;
}



UFixedPlaneBrushProperties::UFixedPlaneBrushProperties()
{
	bPropertySetEnabled = true;
	bSnapToGrid = true;
	bShowGizmo = true;
	Position = FVector::ZeroVector;
}

void UFixedPlaneBrushProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UFixedPlaneBrushProperties* PropertyCache = GetPropertyCache<UFixedPlaneBrushProperties>();
	PropertyCache->bShowGizmo = this->bShowGizmo;
	PropertyCache->bSnapToGrid = this->bSnapToGrid;
	PropertyCache->Position = this->Position;
}
void UFixedPlaneBrushProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UFixedPlaneBrushProperties* PropertyCache = GetPropertyCache<UFixedPlaneBrushProperties>();
	this->bShowGizmo = PropertyCache->bShowGizmo;
	this->bSnapToGrid = PropertyCache->bSnapToGrid;
	this->Position = PropertyCache->Position;
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


void UDynamicMeshSculptTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


const FString BrushIndicatorGizmoType = TEXT("BrushIndicatorGizmoType");

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
	BrushProperties = NewObject<UBrushBaseProperties>(this, TEXT("Brush"));
	CalculateBrushRadius();

	// initialize other properties
	SculptProperties = NewObject<UBrushSculptProperties>(this, TEXT("Sculpting"));
	RemeshProperties = NewObject<UBrushRemeshProperties>(this, TEXT("Remeshing"));
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
	BrushIndicator->bDrawIndicatorLines = false;

	// initialize our properties
	AddToolPropertySource(SculptProperties);
	AddToolPropertySource(BrushProperties);
	if (this->bEnableRemeshing)
	{
		AddToolPropertySource(RemeshProperties);
	}

	BrushProperties->RestoreProperties(this);
	CalculateBrushRadius();
	SculptProperties->RestoreProperties(this);

	GizmoProperties = NewObject<UFixedPlaneBrushProperties>();
	GizmoProperties->RestoreProperties(this);
	AddToolPropertySource(GizmoProperties);

	ViewProperties = NewObject<UMeshEditingViewProperties>();
	ViewProperties->RestoreProperties(this);
	AddToolPropertySource(ViewProperties);
	ShowWireframeWatcher.Initialize(
		[this]() { return ViewProperties->bShowWireframe; },
		[this](bool bNewValue) { DynamicMeshComponent->bExplicitShowWireframe = bNewValue; }, false);
	MaterialModeWatcher.Initialize(
		[this]() { return ViewProperties->MaterialMode; },
		[this](EMeshEditingMaterialModes NewMode) { UpdateMaterialMode(NewMode); }, EMeshEditingMaterialModes::ExistingMaterial);

	// create proxy for plane gizmo, but not gizmo itself, as it only appears in FixedPlane brush mode
	// listen for changes to the proxy and update the plane when that happens
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UDynamicMeshSculptTool::PlaneTransformChanged);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartSculptTool", "Hold Shift to Smooth, Ctrl to Invert (where applicable). Shift+Q/A keys cycle through Brush Types. Shift+S/D change Size (Ctrl+Shift to small-step), Shift+W/E change Speed."),
		EToolMessageLevel::UserNotification);

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
	ViewProperties->SaveProperties(this);
	GizmoProperties->SaveProperties(this);
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
		BrushStartCenterWorld = Ray.PointAt(OutHit.Distance) + SculptProperties->BrushDepth*CurrentBrushRadius*Ray.Direction;

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
			ActiveFixedBrushPlane = ComputeROIBrushPlane(LastBrushPosLocal, false);
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
			ApplyOffsetBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::SculptMax:
			ApplySculptMaxBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Move:
			ApplyMoveBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Smooth:
			ApplySmoothBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Pinch:
			ApplyPinchBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Inflate:
			ApplyInflateBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Flatten:
			ApplyFlattenBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::Plane:
			ApplyPlaneBrush(WorldRay);
			break;
		case EDynamicMeshSculptBrushType::FixedPlane:
			ApplyFixedPlaneBrush(WorldRay);
			break;
	}
}





double UDynamicMeshSculptTool::CalculateBrushFalloff(double Distance)
{
	double d = Distance / CurrentBrushRadius;
	double w = 1;
	if (d > 0.5)
	{
		d = VectorUtil::Clamp((d - 0.5) / 0.5, 0.0, 1.0);
		w = (1 - d * d);
		w = w * w * w;
	}
	return w;
}





void UDynamicMeshSculptTool::ApplySmoothBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay);
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

		FVector3d NewPos = FVector3d::Lerp(OrigPos, SmoothedPos, Falloff*SculptProperties->SmoothSpeed);

		ROIPositionBuffer[k] = NewPos;

	});


	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;
	LastBrushPosLocal = NewBrushPosLocal;
}





void UDynamicMeshSculptTool::ApplyMoveBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnActivePlane(WorldRay);

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

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;
	LastBrushPosLocal = NewBrushPosLocal;
}





void UDynamicMeshSculptTool::ApplyOffsetBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnTargetMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->BrushSpeed) * ActivePressure;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, UseSpeed](int k)
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
			FVector3d MoveVec = UseSpeed * BaseNormal;
			double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
			FVector3d NewPos = OrigPos + Falloff * MoveVec;
			ROIPositionBuffer[k] = NewPos;
		}
	});

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}




void UDynamicMeshSculptTool::ApplySculptMaxBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnTargetMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->BrushSpeed) * ActivePressure;
	double MaxOffset = CurrentBrushRadius * 0.5;

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

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
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

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	FVector3d OffsetBrushPosLocal = NewBrushPosLocal - SculptProperties->BrushDepth * CurrentBrushRadius * BrushNormalLocal;

	// hardcoded lazybrush...
	FVector3d NewSmoothBrushPosLocal = (0.75f)*LastSmoothBrushPosLocal + (0.25f)*NewBrushPosLocal;

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * FMathd::Sqrt(CurrentBrushRadius) * (SculptProperties->BrushSpeed*0.05) * ActivePressure;

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

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}


	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
	LastSmoothBrushPosLocal = NewSmoothBrushPosLocal;
}




FFrame3d UDynamicMeshSculptTool::ComputeROIBrushPlane(const FVector3d& BrushCenter, bool bIgnoreDepth)
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

	FFrame3d Result = FFrame3d(AveragePos, AverageNormal);
	if (bIgnoreDepth == false)
	{
		Result.Origin -= SculptProperties->BrushDepth * CurrentBrushRadius * Result.Z();
	}

	return Result;
}

void UDynamicMeshSculptTool::ApplyPlaneBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	double UseSpeed = FMathd::Sqrt(CurrentBrushRadius) * FMathd::Sqrt(SculptProperties->BrushSpeed) * 0.05 * ActivePressure;


	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, UseSpeed](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d PlanePos = ActiveFixedBrushPlane.ToPlane(OrigPos, 2);
		FVector3d Delta = PlanePos - OrigPos;
		FVector3d MoveVec = UseSpeed * Delta;

		double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));

		FVector3d NewPos = OrigPos + Falloff * MoveVec;
		ROIPositionBuffer[k] = NewPos;
	});

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}





void UDynamicMeshSculptTool::ApplyFixedPlaneBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);
	double UseSpeed = CurrentBrushRadius * FMathd::Sqrt(SculptProperties->BrushSpeed) * 0.1 * ActivePressure;

	FFrame3d FixedPlaneLocal(
		CurTargetTransform.InverseTransformPosition(GizmoProperties->Position),
		CurTargetTransform.GetRotation().Inverse() * (FQuaterniond)DrawPlaneOrientation);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, UseSpeed, FixedPlaneLocal](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d PlanePos = FixedPlaneLocal.ToPlane(OrigPos, 2);
		FVector3d Delta = PlanePos - OrigPos;
		double MaxDist = Delta.Normalize();
		double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));
		FVector3d MoveVec = Falloff * UseSpeed * Delta;
		FVector3d NewPos = (MoveVec.SquaredLength() > MaxDist*MaxDist) ? 
			PlanePos : OrigPos + Falloff * MoveVec;

		ROIPositionBuffer[k] = NewPos;
	});

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}




void UDynamicMeshSculptTool::ApplyFlattenBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
	FVector3d BrushNormalLocal = CurTargetTransform.InverseTransformNormal(LastBrushPosNormalWorld);

	double UseSpeed = FMathd::Sqrt(CurrentBrushRadius) * FMathd::Sqrt(SculptProperties->BrushSpeed) * 0.05 * ActivePressure;
	FFrame3d StampFlattenPlane = ComputeROIBrushPlane(NewBrushPosLocal, true);
	//StampFlattenPlane.Origin -= SculptProperties->BrushDepth * CurrentBrushRadius * StampFlattenPlane.Z();

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	int NumV = VertexROI.Num();
	ROIPositionBuffer.SetNum(NumV, false);

	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, UseSpeed, StampFlattenPlane](int k)
	{
		int VertIdx = VertexROI[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		FVector3d PlanePos = StampFlattenPlane.ToPlane(OrigPos, 2);
		FVector3d Delta = PlanePos - OrigPos;
		FVector3d MoveVec = UseSpeed * Delta;

		double Falloff = CalculateBrushFalloff(OrigPos.Distance(NewBrushPosLocal));

		FVector3d NewPos = OrigPos + Falloff * MoveVec;
		ROIPositionBuffer[k] = NewPos;
	});

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}



void UDynamicMeshSculptTool::ApplyInflateBrush(const FRay& WorldRay)
{
	bool bHit = UpdateBrushPositionOnSculptMesh(WorldRay);
	if (bHit == false)
	{
		return;
	}

	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);

	double Direction = (bInvert) ? -1.0 : 1.0;
	double UseSpeed = Direction * CurrentBrushRadius * SculptProperties->BrushSpeed * 0.05 * ActivePressure;

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

	for (int k = 0; k < NumV; ++k)
	{
		int VertIdx = VertexROI[k];
		const FVector3d& NewPos = ROIPositionBuffer[k];
		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
		Mesh->SetVertex(VertIdx, NewPos);
		UpdateSavedVertex(VertIdx, OrigPos, NewPos);
	}

	bRemeshPending = bEnableRemeshing;

	LastBrushPosLocal = NewBrushPosLocal;
}





int UDynamicMeshSculptTool::FindHitSculptMeshTriangle(const FRay3d& LocalRay)
{
	if (SculptProperties->bHitBackFaces)
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
	if (SculptProperties->bHitBackFaces == false)
	{
		FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition(StateOut.Position));

		BrushTargetMeshSpatial.TriangleFilterF = [this, Mesh, &LocalEyePosition](int TriangleID) {
			FVector3d Normal, Centroid;
			double Area;
			Mesh->GetTriInfo(TriangleID, Normal, Area, Centroid);
			return Normal.Dot((Centroid - LocalEyePosition)) < 0;
		};
	}

	int HitTID =  BrushTargetMeshSpatial.FindNearestHitTriangle(LocalRay);

	if (SculptProperties->bHitBackFaces == false)
	{
		BrushTargetMeshSpatial.TriangleFilterF = nullptr;
	}

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

bool UDynamicMeshSculptTool::UpdateBrushPositionOnTargetMesh(const FRay& WorldRay)
{
	FRay3d LocalRay(CurTargetTransform.InverseTransformPosition(WorldRay.Origin),
		CurTargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	int HitTID = FindHitTargetMeshTriangle(LocalRay);
	if (HitTID != IndexConstants::InvalidID)
	{
		const FDynamicMesh3* TargetMesh = BrushTargetMeshSpatial.GetMesh();

		FTriangle3d Triangle;
		TargetMesh->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		LastBrushPosNormalWorld = CurTargetTransform.TransformNormal(TargetMesh->GetTriNormal(HitTID));
		LastBrushPosWorld = CurTargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
		return true;
	}
	return false;
}


bool UDynamicMeshSculptTool::UpdateBrushPositionOnSculptMesh(const FRay& WorldRay)
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
		FHitResult OutHit;
		if (HitTest(DevicePos.WorldRay, OutHit))
		{
			LastBrushPosWorld = DevicePos.WorldRay.PointAt(OutHit.Distance + SculptProperties->BrushDepth*CurrentBrushRadius);
			LastBrushPosNormalWorld = OutHit.Normal;
		}
	}
	return true;
}




void UDynamicMeshSculptTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);

	BrushIndicator->Update( (float)this->CurrentBrushRadius, (FVector)this->LastBrushPosWorld, (FVector)this->LastBrushPosNormalWorld, 1.0f);

	if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::FixedPlane)
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		FColor GridColor(128, 128, 128, 32);
		float GridThickness = 0.5f;
		float GridLineSpacing = 25.0f;   // @todo should be relative to view
		int NumGridLines = 10;
		FFrame3f DrawFrame(GizmoProperties->Position, DrawPlaneOrientation);
		MeshDebugDraw::DrawSimpleGrid(DrawFrame, NumGridLines, GridLineSpacing, GridThickness, GridColor, false, PDI, FTransform::Identity);
	}
}


void UDynamicMeshSculptTool::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_SculptToolTick);

	UMeshSurfacePointTool::Tick(DeltaTime);

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

	bool bGizmoVisible = (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::FixedPlane)
		&& (GizmoProperties->bShowGizmo);
	UpdateFixedPlaneGizmoVisibility(bGizmoVisible);
	GizmoProperties->bPropertySetEnabled = (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::FixedPlane);

	if (bPendingSetFixedPlanePosition)
	{
		SetFixedSculptPlaneFromWorldPos((FVector)LastBrushPosWorld);
		bPendingSetFixedPlanePosition = false;
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




void UDynamicMeshSculptTool::RemeshROIPass()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshOctree3* Octree = DynamicMeshComponent->GetOctree();

	FSubRegionRemesher Remesher(Mesh);
	double TargetEdgeLength = RemeshProperties->RelativeSize * InitialEdgeLength;
	Remesher.SetTargetEdgeLength(TargetEdgeLength);

	double UseSmoothing = RemeshProperties->Smoothing * 0.25;
	Remesher.SmoothSpeedT = UseSmoothing;

	// this is a temporary tweak for Pinch brush. Remesh params should be per-brush!
	if (SculptProperties->PrimaryBrushType == EDynamicMeshSculptBrushType::Pinch && bSmoothing == false)
	{
		Remesher.MinEdgeLength = TargetEdgeLength * 0.1;

		Remesher.CustomSmoothSpeedF = [this, &UseSmoothing](const FDynamicMesh3& Mesh, int vID)
		{
			FVector3d Pos = Mesh.GetVertex(vID);
			double Falloff = CalculateBrushFalloff(Pos.Distance((FVector3d)LastBrushPosLocal));
			return (1.0f - Falloff) * UseSmoothing;
		};
	}

	// tweak remesh params for Smooth brush
	if (bSmoothing && RemeshProperties->bRemeshSmooth == false)
	{
		Remesher.MaxEdgeLength = 2*InitialEdgeLength;
		Remesher.MinEdgeLength = InitialEdgeLength * 0.01;
	}

	Remesher.SmoothType = (SculptProperties->bPreserveUVFlow) ?
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

		if (bHaveUVSeams || bHaveNormalSeams)
		{
			FMeshConstraintsUtil::ConstrainAllSeams(constraints, *Mesh, bConstraintAllowSplits, bConstraintAllowSmoothing);
			Remesher.SetExternalConstraints(&constraints);
		}
	}

	for (int k = 0; k < 5; ++k)
	{
		if (bIsUniformSmooth == false)
		{
			Remesher.bEnableFlips = RemeshProperties->bFlips && (k < 2);
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
	SphereMesh->SetMaterial(ToolSetupUtil::GetDefaultBrushVolumeMaterial(nullptr));
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
	SculptProperties->BrushSpeed = FMath::Clamp(SculptProperties->BrushSpeed + 0.05f, 0.0f, 1.0f);
}

void UDynamicMeshSculptTool::DecreaseBrushSpeedAction()
{
	SculptProperties->BrushSpeed = FMath::Clamp(SculptProperties->BrushSpeed - 0.05f, 0.0f, 1.0f);
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


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID+1,
		TEXT("NextBrushMode"),
		LOCTEXT("SculptNextBrushMode", "Next Brush Type"),
		LOCTEXT("SculptNextBrushModeTooltip", "Cycle to next Brush Type"),
		EModifierKey::Shift, EKeys::A,
		[this]() { NextBrushModeAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID+2,
		TEXT("PreviousBrushMode"),
		LOCTEXT("SculptPreviousBrushMode", "Previous Brush Type"),
		LOCTEXT("SculptPreviousBrushModeTooltip", "Cycle to previous Brush Type"),
		EModifierKey::Shift, EKeys::Q,
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
		EModifierKey::Shift, EKeys::D,
		[this]() { IncreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 51,
		TEXT("SculptDecreaseSize"),
		LOCTEXT("SculptDecreaseSize", "Decrease Size"),
		LOCTEXT("SculptDecreaseSizeTooltip", "Decrease Brush Size"),
		EModifierKey::Shift, EKeys::S,
		[this]() { DecreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 52,
		TEXT("SculptIncreaseSizeSmallStep"),
		LOCTEXT("SculptIncreaseSize", "Increase Size"),
		LOCTEXT("SculptIncreaseSizeTooltip", "Increase Brush Size"),
		EModifierKey::Shift | EModifierKey::Control, EKeys::D,
		[this]() { IncreaseBrushRadiusSmallStepAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 53,
		TEXT("SculptDecreaseSizeSmallStemp"),
		LOCTEXT("SculptDecreaseSize", "Decrease Size"),
		LOCTEXT("SculptDecreaseSizeTooltip", "Decrease Brush Size"),
		EModifierKey::Shift | EModifierKey::Control, EKeys::S,
		[this]() { DecreaseBrushRadiusSmallStepAction(); });




	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 60,
		TEXT("SculptIncreaseSpeed"),
		LOCTEXT("SculptIncreaseSpeed", "Increase Speed"),
		LOCTEXT("SculptIncreaseSpeedTooltip", "Increase Brush Speed"),
		EModifierKey::Shift, EKeys::E,
		[this]() { IncreaseBrushSpeedAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 61,
		TEXT("SculptDecreaseSpeed"),
		LOCTEXT("SculptDecreaseSpeed", "Decrease Speed"),
		LOCTEXT("SculptDecreaseSpeedTooltip", "Decrease Brush Speed"),
		EModifierKey::Shift, EKeys::W,
		[this]() { DecreaseBrushSpeedAction(); });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::ToggleWireframe,
		TEXT("ToggleWireframe"),
		LOCTEXT("ToggleWireframe", "Toggle Wireframe"),
		LOCTEXT("ToggleWireframeTooltip", "Toggle visibility of wireframe overlay"),
		EModifierKey::Alt, EKeys::W,
		[this]() { ViewProperties->bShowWireframe = !ViewProperties->bShowWireframe; });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 100,
		TEXT("SetFixedSculptPlane"),
		LOCTEXT("SetFixedSculptPlane", "Set Fixed Sculpt Plane"),
		LOCTEXT("SetFixedSculptPlaneTooltip", "Set position of fixed sculpt plane"),
		EModifierKey::None, EKeys::P,
		[this]() { bPendingSetFixedPlanePosition = true; });

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
	} 
	else if (MaterialMode == EMeshEditingMaterialModes::MeshFocusMaterial)
	{ 
		UMaterialInterface* SculptMaterial = ToolSetupUtil::GetSculptMaterial1(GetToolManager());
		if (SculptMaterial != nullptr)
		{
			DynamicMeshComponent->SetOverrideRenderMaterial(SculptMaterial);
		}
		DynamicMeshComponent->bCastDynamicShadow = false;
	}
}




void UDynamicMeshSculptTool::SetFixedSculptPlaneFromWorldPos(const FVector& Position)
{
	UpdateFixedSculptPlanePosition(Position);
	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(DrawPlaneOrientation, GizmoProperties->Position));
	}
}


void UDynamicMeshSculptTool::PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	DrawPlaneOrientation = Transform.GetRotation();
	UpdateFixedSculptPlanePosition(Transform.GetLocation());
}


void UDynamicMeshSculptTool::UpdateFixedSculptPlanePosition(const FVector& Position)
{
	GizmoProperties->Position = Position;
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
			PlaneTransformGizmo = GetToolManager()->GetPairedGizmoManager()->Create3AxisTransformGizmo(this);
			PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy, GetToolManager());
			PlaneTransformGizmo->SetNewGizmoTransform(FTransform(DrawPlaneOrientation, GizmoProperties->Position));
		}

		PlaneTransformGizmo->bSnapToWorldGrid = GizmoProperties->bSnapToGrid;
	}
}




#undef LOCTEXT_NAMESPACE
