// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshGroupPaintTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"

#include "MeshWeights.h"
#include "MeshNormals.h"
#include "MeshIndexUtil.h"
#include "Util/BufferUtil.h"
#include "Util/ColorConstants.h"
#include "Selections/MeshConnectedComponents.h"
#include "Selections/MeshFaceSelection.h"
#include "Polygroups/PolygroupUtil.h"

#include "Changes/MeshVertexChange.h"
#include "Changes/MeshPolygroupChange.h"
#include "Changes/BasicChanges.h"

#include "Sculpting/MeshGroupPaintBrushOps.h"
#include "Sculpting/StampFalloffs.h"
#include "Sculpting/MeshSculptUtil.h"

#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UMeshGroupPaintTool"

namespace
{
	// probably should be something defined for the whole tool framework...
#if WITH_EDITOR
	static EAsyncExecution GroupPaintToolAsyncExecTarget = EAsyncExecution::LargeThreadPool;
#else
	static EAsyncExecution GroupPaintToolAsyncExecTarget = EAsyncExecution::ThreadPool;
#endif
}


/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UMeshGroupPaintToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMeshGroupPaintTool* SculptTool = NewObject<UMeshGroupPaintTool>(SceneState.ToolManager);
	SculptTool->SetWorld(SceneState.World);
	return SculptTool;
}


/*
 * Properties
 */
void UMeshGroupPaintToolActionPropertySet::PostAction(EMeshGroupPaintToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}





/*
 * Tool
 */

void UMeshGroupPaintTool::Setup()
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
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshVerticesChanged.AddUObject(this, &UMeshGroupPaintTool::OnDynamicMeshComponentChanged);

	FDynamicMesh3* Mesh = GetSculptMesh();
	Mesh->EnableVertexColors(FVector3f::One());
	FAxisAlignedBox3d Bounds = Mesh->GetCachedBounds();

	TFuture<void> PrecomputeFuture = Async(GroupPaintToolAsyncExecTarget, [&]()
	{
		PrecomputeFilterData();
	});

	TFuture<void> OctreeFuture = Async(GroupPaintToolAsyncExecTarget, [&]()
	{
		// initialize dynamic octree
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
	});

	// initialize render decomposition
	TUniquePtr<FMeshRenderDecomposition> Decomp = MakeUnique<FMeshRenderDecomposition>();
	FMeshRenderDecomposition::BuildChunkedDecomposition(Mesh, &MaterialSet, *Decomp);
	Decomp->BuildAssociations(Mesh);
	//UE_LOG(LogTemp, Warning, TEXT("Decomposition has %d groups"), Decomp->Num());
	DynamicMeshComponent->SetExternalDecomposition(MoveTemp(Decomp));

	// initialize brush radius range interval, brush properties
	UMeshSculptToolBase::InitializeBrushSizeRange(Bounds);

	PolygroupLayerProperties = NewObject<UPolygroupLayersProperties>(this);
	PolygroupLayerProperties->RestoreProperties(this);
	PolygroupLayerProperties->InitializeGroupLayers(GetSculptMesh());
	PolygroupLayerProperties->WatchProperty(PolygroupLayerProperties->ActiveGroupLayer, [&](FName) { OnSelectedGroupLayerChanged(); });
	UpdateActiveGroupLayer();
	AddToolPropertySource(PolygroupLayerProperties);

	ToolProperties = NewObject<UGroupPaintToolProperties>(this);
	AddToolPropertySource(ToolProperties);
	ToolProperties->WatchProperty(ToolProperties->SubToolType,
		[this](EMeshGroupPaintInteractionType NewType) { UpdateSubToolType(NewType); });
	ToolProperties->RestoreProperties(this);

	// initialize other properties
	FilterProperties = NewObject<UGroupPaintBrushFilterProperties>(this);

	InitializeIndicator();

	// initialize our properties
	AddToolPropertySource(UMeshSculptToolBase::BrushProperties);
	UMeshSculptToolBase::BrushProperties->bShowPerBrushProps = false;
	UMeshSculptToolBase::BrushProperties->bShowFalloff = false;
	CalculateBrushRadius();
	FilterProperties->RestoreProperties(this);

	PaintBrushOpOperties = NewObject<UGroupPaintBrushOpProps>(this);
	RegisterBrushType((int32)EMeshGroupPaintBrushType::Paint,
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FGroupPaintBrushOp>(); }),
		PaintBrushOpOperties);

	//RegisterBrushType((int32)EMeshGroupPaintBrushType::Erase,
	//	MakeUnique<TBasicMeshSculptBrushOpFactory<FGroupEraseBrushOp>>(),
	//	EraseBrushOpOperties);

	// secondary brushes
	EraseBrushOpOperties = NewObject<UGroupEraseBrushOpProps>(this);
	EraseBrushOpOperties->GetCurrentGroupLambda = [this]() { return PaintBrushOpOperties->GetGroup(); };

	RegisterSecondaryBrushType((int32)EMeshGroupPaintBrushType::Erase,
		MakeUnique<TBasicMeshSculptBrushOpFactory<FGroupEraseBrushOp>>(),
		EraseBrushOpOperties);

	AddToolPropertySource(FilterProperties);
	AddToolPropertySource(UMeshSculptToolBase::ViewProperties);

	AddToolPropertySource(UMeshSculptToolBase::GizmoProperties);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::GizmoProperties, false);


	// register watchers
	FilterProperties->WatchProperty( FilterProperties->PrimaryBrushType,
		[this](EMeshGroupPaintBrushType NewType) { UpdateBrushType(NewType); });

	// must call before updating brush type so that we register all brush properties?
	UMeshSculptToolBase::OnCompleteSetup();

	UpdateBrushType(FilterProperties->PrimaryBrushType);
	SetActiveSecondaryBrushType((int32)EMeshGroupPaintBrushType::Erase);

	UpdateSubToolType(ToolProperties->SubToolType);

	FreezeActions = NewObject<UMeshGroupPaintToolFreezeActions>(this);
	FreezeActions->Initialize(this);
	AddToolPropertySource(FreezeActions);

	MeshElementsDisplay = NewObject<UMeshElementsVisualizer>(this);
	MeshElementsDisplay->CreateInWorld(DynamicMeshComponent->GetWorld(), DynamicMeshComponent->GetComponentTransform());
	if (ensure(MeshElementsDisplay->Settings))
	{
		MeshElementsDisplay->Settings->RestoreProperties(this);
		AddToolPropertySource(MeshElementsDisplay->Settings);
	}
	MeshElementsDisplay->SetMeshAccessFunction([&](void) { return GetSculptMesh(); });

	// force colors update... ?
	DynamicMeshComponent->TriangleColorFunc = [this](const FDynamicMesh3* Mesh, int TriangleID)
	{
		return GetColorForGroup(ActiveGroupSet->GetGroup(TriangleID));
	};
	DynamicMeshComponent->FastNotifyColorsUpdated();

	// disable view properties
	SetViewPropertiesEnabled(false);
	UpdateMaterialMode(EMeshEditingMaterialModes::VertexColor);
	UpdateWireframeVisibility(false);
	UpdateFlatShadingSetting(true);

	PrecomputeFuture.Wait();
	OctreeFuture.Wait();
}

void UMeshGroupPaintTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);
	}

	if (ensure(MeshElementsDisplay->Settings))
	{
		MeshElementsDisplay->Settings->SaveProperties(this);
	}
	MeshElementsDisplay->Disconnect();

	FilterProperties->SaveProperties(this);
	ToolProperties->SaveProperties(this);
	PolygroupLayerProperties->SaveProperties(this);


	// TODO
	// TODO
	// TODO
	// Bake should not have to replace entire mesh just to update groups...

	// do our own bake
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("GroupPaintMeshToolTransactionName", "Paint Groups"));
		ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FConversionToMeshDescriptionOptions ConversionOptions;
			DynamicMeshComponent->Bake(CommitParams.MeshDescription, true, ConversionOptions);
		});
		GetToolManager()->EndUndoTransaction();
	}


	// this call will unregister and destroy DynamicMeshComponent
	UMeshSculptToolBase::Shutdown(EToolShutdownType::Completed);
}


void UMeshGroupPaintTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	UMeshSculptToolBase::RegisterActions(ActionSet);

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 500,
		TEXT("PickGroupColorUnderCursor"),
		LOCTEXT("PickGroupColorUnderCursor", "Pick PolyGroup"),
		LOCTEXT("PickGroupColorUnderCursorTooltip", "Switch the active PolyGroup to the group currently under the cursor"),
		EModifierKey::Shift, EKeys::G,
		[this]() { bPendingPickGroup = true; });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 501,
		TEXT("ToggleFrozenGroup"),
		LOCTEXT("ToggleFrozenGroup", "Toggle Group Frozen State"),
		LOCTEXT("ToggleFrozenGroupTooltip", "Toggle Group Frozen State"),
		EModifierKey::Shift, EKeys::F,
		[this]() { bPendingToggleFreezeGroup = true; });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 502,
		TEXT("CreateNewGroup"),
		LOCTEXT("CreateNewGroup", "New Group"),
		LOCTEXT("CreateNewGroupTooltip", "Allocate a new Polygroup and set as Current"),
		EModifierKey::Shift, EKeys::Q,
		[this]() { AllocateNewGroupAndSetAsCurrentAction(); });
};


void UMeshGroupPaintTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	CalculateBrushRadius();
}


void UMeshGroupPaintTool::OnBeginStroke(const FRay& WorldRay)
{
	UpdateBrushPosition(WorldRay);

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
	//SculptOptions.bPreserveUVFlow = false; // FilterProperties->bPreserveUVFlow;
	SculptOptions.ConstantReferencePlane = GetCurrentStrokeReferencePlane();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	UseBrushOp->ConfigureOptions(SculptOptions);
	UseBrushOp->BeginStroke(GetSculptMesh(), LastStamp, VertexROI);

	AccumulatedTriangleROI.Reset();

	// begin change here? or wait for first stamp?
	BeginChange();
}

void UMeshGroupPaintTool::OnEndStroke()
{
	GetActiveBrushOp()->EndStroke(GetSculptMesh(), LastStamp, VertexROI);

	// close change record
	EndChange();
}



void UMeshGroupPaintTool::UpdateROI(const FSculptBrushStamp& BrushStamp)
{
	SCOPE_CYCLE_COUNTER(GroupPaintTool_UpdateROI);

	const FVector3d& BrushPos = BrushStamp.LocalFrame.Origin;
	const FDynamicMesh3* Mesh = GetSculptMesh();
	float RadiusSqr = GetCurrentBrushRadius() * GetCurrentBrushRadius();
	FAxisAlignedBox3d BrushBox(
		BrushPos - GetCurrentBrushRadius() * FVector3d::One(),
		BrushPos + GetCurrentBrushRadius() * FVector3d::One());

	TriangleROI.Reset();

	int32 CenterTID = GetBrushTriangleID();
	if (Mesh->IsTriangle(CenterTID))
	{
		TriangleROI.Add(CenterTID);
	}

	if (FilterProperties->bVolumetric)
	{
		Octree.RangeQuery(BrushBox,
			[&](int TriIdx) {

			if ((Mesh->GetTriCentroid(TriIdx) - BrushPos).SquaredLength() < RadiusSqr)
			{
				TriangleROI.Add(TriIdx);
			}
		});
	}
	else
	{
		if (Mesh->IsTriangle(CenterTID))
		{
			FVector3d CenterNormal = TriNormals[CenterTID];
			bool bUseAngleThreshold = (FilterProperties->AngleThreshold < 180.0f);
			double DotAngleThreshold = FMathd::Cos(FilterProperties->AngleThreshold * FMathd::DegToRad);

			bool bStopAtUVSeams = FilterProperties->bUVSeams;
			bool bStopAtNormalSeams = FilterProperties->bNormalSeams;

			TArray<int32> StartROI;
			StartROI.Add(CenterTID);
			FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, StartROI, TriangleROI, &TempROIBuffer,
				[&](int t1, int t2) 
			{ 
				if ((Mesh->GetTriCentroid(t2) - BrushPos).SquaredLength() < RadiusSqr)
				{
					if (bUseAngleThreshold == false || CenterNormal.Dot(TriNormals[t2]) > DotAngleThreshold)
					{
						int32 eid = Mesh->FindEdgeFromTriPair(t1, t2);
						if (bStopAtUVSeams == false || UVSeamEdges[eid] == false)
						{
							if (bStopAtNormalSeams == false || NormalSeamEdges[eid] == false)
							{
								return true;
							}
						}
					}
				}
				return false;
			});
		}
	}


	// apply visibility filter
	if (FilterProperties->VisibilityFilter != EMeshGroupPaintVisibilityType::None)
	{
		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(ComponentTarget->GetWorldTransform().InverseTransformPosition(StateOut.Position));
		TempROIBuffer.SetNum(0, false);
		for (int32 tid : TriangleROI)
		{
			TempROIBuffer.Add(tid);
		}
		ParallelFor(TempROIBuffer.Num(), [&](int32 idx)
		{
			FVector3d Centroid = Mesh->GetTriCentroid(TempROIBuffer[idx]);
			FVector3d FaceNormal = Mesh->GetTriNormal(TempROIBuffer[idx]);
			if (FaceNormal.Dot((Centroid - LocalEyePosition)) > 0)
			{
				TempROIBuffer[idx] = -1;
			}
			if (FilterProperties->VisibilityFilter == EMeshGroupPaintVisibilityType::Unoccluded)
			{
				int32 HitTID = Octree.FindNearestHitObject(FRay3d(LocalEyePosition, (Centroid - LocalEyePosition).Normalized()));
				if (HitTID != TempROIBuffer[idx])
				{
					TempROIBuffer[idx] = -1;
				}
			}
		});
		TriangleROI.Reset();
		for (int32 tid : TempROIBuffer)
		{
			if (tid >= 0)
			{
				TriangleROI.Add(tid);
			}
		}
	}


	VertexSetBuffer.Reset();
	for (int32 tid : TriangleROI)
	{
		FIndex3i Tri = Mesh->GetTriangle(tid);
		VertexSetBuffer.Add(Tri.A);  VertexSetBuffer.Add(Tri.B);  VertexSetBuffer.Add(Tri.C);
	}
	VertexROI.SetNum(0, false);
	BufferUtil::AppendElements(VertexROI, VertexSetBuffer);


	ROITriangleBuffer.Reserve(TriangleROI.Num());
	ROITriangleBuffer.SetNum(0, false);
	for (int32 tid : TriangleROI)
	{
		ROITriangleBuffer.Add(tid);
	}
	ROIGroupBuffer.SetNum(ROITriangleBuffer.Num(), false);
}

bool UMeshGroupPaintTool::UpdateStampPosition(const FRay& WorldRay)
{
	CalculateBrushRadius();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
	case ESculptBrushOpTargetType::TargetMesh:
		UpdateBrushPositionOnSculptMesh(WorldRay, true);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		check(false);
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


void UMeshGroupPaintTool::ApplyStamp()
{
	SCOPE_CYCLE_COUNTER(GroupPaintToolApplyStamp);

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	// yuck
	FMeshTriangleGroupEditBrushOp* GroupBrushOp = (FMeshTriangleGroupEditBrushOp*)UseBrushOp.Get();

	FDynamicMesh3* Mesh = GetSculptMesh();
	GroupBrushOp->ApplyStampByTriangles(Mesh, CurrentStamp, ROITriangleBuffer, ROIGroupBuffer);

	SyncMeshWithGroupBuffer(Mesh);

	LastStamp = CurrentStamp;
	LastStamp.TimeStamp = FDateTime::Now();
}




void UMeshGroupPaintTool::SyncMeshWithGroupBuffer(FDynamicMesh3* Mesh)
{
	const int32 NumT = ROITriangleBuffer.Num();
	// change update could be async here if we collected array of <idx,orig,new> and dispatched independenlty
	for ( int32 k = 0; k < NumT; ++k)
	{
		int TriIdx = ROITriangleBuffer[k];
		int32 CurGroupID = ActiveGroupSet->GetGroup(TriIdx);

		if (FrozenGroups.Contains(CurGroupID))		// skip frozen groups
		{
			continue;
		}

		ActiveGroupEditBuilder->SaveTriangle(TriIdx, CurGroupID, ROIGroupBuffer[k]);

		ActiveGroupSet->SetGroup(TriIdx, ROIGroupBuffer[k]);

		//ActiveVertexChange->UpdateVertexColor(VertIdx, OrigColor, NewColor);
	}
}






int32 UMeshGroupPaintTool::FindHitSculptMeshTriangle(const FRay3d& LocalRay)
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

int32 UMeshGroupPaintTool::FindHitTargetMeshTriangle(const FRay3d& LocalRay)
{
	check(false);
	return IndexConstants::InvalidID;
}



bool UMeshGroupPaintTool::UpdateBrushPosition(const FRay& WorldRay)
{
	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	bool bHit = false; 
	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
	case ESculptBrushOpTargetType::TargetMesh:
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		check(false);
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	}

	if (bHit && UseBrushOp->GetAlignStampToView())
	{
		AlignBrushToView();
	}

	return bHit;
}




bool UMeshGroupPaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	PendingStampType = FilterProperties->PrimaryBrushType;

	if(ensure(InStroke() == false))
	{
		UpdateBrushPosition(DevicePos.WorldRay);
	}
	return true;
}



void UMeshGroupPaintTool::OnTick(float DeltaTime)
{
	UMeshSculptToolBase::OnTick(DeltaTime);
	MeshElementsDisplay->OnTick(DeltaTime);

	ConfigureIndicator(FilterProperties->bVolumetric);

	if (bHavePendingAction)
	{
		ApplyAction(PendingAction);
		bHavePendingAction = false;
		PendingAction = EMeshGroupPaintToolActions::NoAction;
	}

	SCOPE_CYCLE_COUNTER(GroupPaintToolTick);

	// process the undo update
	if (bUndoUpdatePending)
	{
		// wait for updates
		WaitForPendingUndoRedo();

		// post rendering update
		DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(AccumulatedTriangleROI, EMeshRenderAttributeFlags::VertexColors);
		GetToolManager()->PostInvalidation();

		// ignore stamp and wait for next tick to do anything else
		bUndoUpdatePending = false;
		return;
	}

	if (bPendingPickGroup || bPendingToggleFreezeGroup)
	{
		if (GetBrushTriangleID() >= 0 && IsStampPending() == false )
		{
			if (GetSculptMesh()->IsTriangle(GetBrushTriangleID()))
			{
				int32 HitGroupID = ActiveGroupSet->GetGroup(GetBrushTriangleID());
				if (bPendingPickGroup)
				{
					PaintBrushOpOperties->Group = HitGroupID;
				}
				else if (bPendingToggleFreezeGroup)
				{
					ToggleFrozenGroup(HitGroupID);
				}
			}
		}
		bPendingPickGroup = bPendingToggleFreezeGroup = false;
	}


	if (ToolProperties->SubToolType == EMeshGroupPaintInteractionType::Brush)
	{
		if (IsStampPending())
		{
			//UE_LOG(LogTemp, Warning, TEXT("dt is %.3f, tick fps %.2f - roi size %d/%d"), DeltaTime, 1.0 / DeltaTime, VertexROI.Num(), TriangleROI.Num());
			SCOPE_CYCLE_COUNTER(GroupPaintTool_Tick_ApplyStampBlock);

			ApplyStrokeFlowInTick();

			// update brush position
			if (UpdateStampPosition(GetPendingStampRayWorld()) == false)
			{
				return;
			}

			// update sculpt ROI
			UpdateROI(CurrentStamp);

			// append updated ROI to modified region (async)
			TFuture<void> AccumulateROI = Async(GroupPaintToolAsyncExecTarget, [&]()
			{
				AccumulatedTriangleROI.Append(TriangleROI);
			});

			// apply the stamp
			ApplyStamp();

			{
				SCOPE_CYCLE_COUNTER(GroupPaintTool_Tick_UpdateMeshBlock);
				DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TriangleROI, EMeshRenderAttributeFlags::VertexColors);
				GetToolManager()->PostInvalidation();
			}

			// we don't really need to wait for these to happen to end Tick()...
			AccumulateROI.Wait();
		}
	}

}







void UMeshGroupPaintTool::AllocateNewGroupAndSetAsCurrentAction()
{
	int32 NewGroupID = ActiveGroupSet->AllocateNewGroupID();
	PaintBrushOpOperties->Group = NewGroupID;
}



FColor UMeshGroupPaintTool::GetColorForGroup(int32 GroupID)
{
	FColor Color = LinearColors::SelectFColor(GroupID);
	if (FrozenGroups.Contains(GroupID))
	{
		int32 GrayValue = (Color.R + Color.G + Color.B) / 3;
		Color.R = Color.G = Color.B = FMath::Clamp(GrayValue, 0, 255);
	}
	return Color;
}

void UMeshGroupPaintTool::ToggleFrozenGroup(int32 FreezeGroupID)
{
	if (FreezeGroupID == 0) return;

	TArray<int32> InitialFrozenGroups = FrozenGroups;
	if (FrozenGroups.Contains(FreezeGroupID))
	{
		FrozenGroups.Remove(FreezeGroupID);
	}
	else
	{
		FrozenGroups.Add(FreezeGroupID);
	}

	const FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	TempROIBuffer.SetNum(0, false);
	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		int32 TriGroupID = ActiveGroupSet->GetGroup(tid);
		if (TriGroupID == FreezeGroupID)
		{
			TempROIBuffer.Add(tid);
		}
	}
	EmitFrozenGroupsChange(InitialFrozenGroups, FrozenGroups, LOCTEXT("ToggleFrozenGroup", "Toggle Frozen Group"));
	DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TempROIBuffer, EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();
}

void UMeshGroupPaintTool::FreezeOtherGroups(int32 KeepGroupID)
{
	TArray<int32> InitialFrozenGroups = FrozenGroups;
	FrozenGroups.Reset();
	const FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	TempROIBuffer.SetNum(0, false);
	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		int32 GroupID = ActiveGroupSet->GetGroup(tid);
		if ( GroupID != 0 && GroupID != KeepGroupID)
		{
			FrozenGroups.AddUnique(ActiveGroupSet->GetGroup(tid));
			TempROIBuffer.Add(tid);
		}
	}
	EmitFrozenGroupsChange(InitialFrozenGroups, FrozenGroups, LOCTEXT("FreezeOtherGroups", "Freeze Other Groups"));
	DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TempROIBuffer, EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();
}

void UMeshGroupPaintTool::ClearAllFrozenGroups()
{
	TArray<int32> InitialFrozenGroups = FrozenGroups;
	const FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	TempROIBuffer.SetNum(0, false);
	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		if ( FrozenGroups.Contains(ActiveGroupSet->GetGroup(tid)) )
		{
			TempROIBuffer.Add(tid);
		}
	}
	FrozenGroups.Reset();
	EmitFrozenGroupsChange(InitialFrozenGroups, FrozenGroups, LOCTEXT("ClearAllFrozenGroups", "Clear Frozen Groups"));
	DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TempROIBuffer, EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();
}



void UMeshGroupPaintTool::EmitFrozenGroupsChange(const TArray<int32>& FromGroups, const TArray<int32>& ToGroups, const FText& ChangeText)
{
	if (FromGroups != ToGroups)
	{
		TUniquePtr<TSimpleValueLambdaChange<TArray<int32>>> FrozenGroupsChange = MakeUnique<TSimpleValueLambdaChange<TArray<int32>>>();
		FrozenGroupsChange->FromValue = FromGroups;
		FrozenGroupsChange->ToValue = ToGroups;
		FrozenGroupsChange->ValueChangeFunc = [this](UObject*, const TArray<int32>& FromGroups, const TArray<int32>& ToGroups, bool)
		{	
			FrozenGroups = ToGroups;
			DynamicMeshComponent->FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexColors);
		};
		GetToolManager()->EmitObjectChange(this, MoveTemp(FrozenGroupsChange), ChangeText);
	}
}


void UMeshGroupPaintTool::GrowCurrentGroupAction()
{
	BeginChange();

	int32 CurrentGroupID = PaintBrushOpOperties->Group;
	const FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FMeshFaceSelection InitialSelection(Mesh);
	InitialSelection.Select([&](int32 tid) { return ActiveGroupSet->GetGroup(tid) == CurrentGroupID; });
	FMeshFaceSelection ExpandSelection(InitialSelection);
	ExpandSelection.ExpandToOneRingNeighbours([&](int32 tid) { return FrozenGroups.Contains(ActiveGroupSet->GetGroup(tid)) == false; });
	TempROIBuffer.SetNum(0, false);
	ExpandSelection.SetDifference(InitialSelection, TempROIBuffer);

	ActiveGroupEditBuilder->SaveTriangles(TempROIBuffer);
	for (int32 tid : TempROIBuffer)
	{
		ActiveGroupSet->SetGroup(tid, CurrentGroupID);
	}
	ActiveGroupEditBuilder->SaveTriangles(TempROIBuffer);

	DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TempROIBuffer, EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();
	EndChange();
}


void UMeshGroupPaintTool::ShrinkCurrentGroupAction()
{
	BeginChange();

	int32 CurrentGroupID = PaintBrushOpOperties->Group;
	const FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FMeshFaceSelection InitialSelection(Mesh);
	InitialSelection.Select([&](int32 tid) { return ActiveGroupSet->GetGroup(tid) == CurrentGroupID; });
	FMeshFaceSelection ContractSelection(InitialSelection);
	ContractSelection.ContractBorderByOneRingNeighbours();
	TempROIBuffer.SetNum(0, false);
	InitialSelection.SetDifference(ContractSelection, TempROIBuffer);

	ActiveGroupEditBuilder->SaveTriangles(TempROIBuffer);
	for (int32 tid : TempROIBuffer)
	{
		// todo: could probably guess boundary groups here...
		ActiveGroupSet->SetGroup(tid, 0);
	}
	ActiveGroupEditBuilder->SaveTriangles(TempROIBuffer);

	DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TempROIBuffer, EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();
	EndChange();
}



//
// Change Tracking
//
void UMeshGroupPaintTool::BeginChange()
{
	check(ActiveGroupEditBuilder == nullptr);
	ActiveGroupEditBuilder = MakeUnique<FDynamicMeshGroupEditBuilder>(ActiveGroupSet.Get());
}

void UMeshGroupPaintTool::EndChange()
{
	check(ActiveGroupEditBuilder);

	TUniquePtr<FDynamicMeshGroupEdit> EditResult = ActiveGroupEditBuilder->ExtractResult();
	ActiveGroupEditBuilder = nullptr;

	TUniquePtr<TWrappedToolCommandChange<FMeshPolygroupChange>> NewChange = MakeUnique<TWrappedToolCommandChange<FMeshPolygroupChange>>();
	NewChange->WrappedChange = MakeUnique<FMeshPolygroupChange>(MoveTemp(EditResult));
	NewChange->BeforeModify = [this](bool bRevert)
	{
		this->WaitForPendingUndoRedo();
	};

	GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(NewChange), LOCTEXT("GroupPaintChange", "Group Stroke"));
}


void UMeshGroupPaintTool::WaitForPendingUndoRedo()
{
	if (bUndoUpdatePending)
	{
		bUndoUpdatePending = false;
	}
}

void UMeshGroupPaintTool::OnDynamicMeshComponentChanged(USimpleDynamicMeshComponent* Component, const FMeshVertexChange* Change, bool bRevert)
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

	// note that we have a pending update
	bUndoUpdatePending = true;
}


void UMeshGroupPaintTool::PrecomputeFilterData()
{
	const FDynamicMesh3* Mesh = GetSculptMesh();
	
	TriNormals.SetNum(Mesh->MaxTriangleID());
	ParallelFor(Mesh->MaxTriangleID(), [&](int32 tid)
	{
		if (Mesh->IsTriangle(tid))
		{
			TriNormals[tid] = Mesh->GetTriNormal(tid);
		}
	});

	const FDynamicMeshNormalOverlay* Normals = Mesh->Attributes()->PrimaryNormals();
	const FDynamicMeshUVOverlay* UVs = Mesh->Attributes()->PrimaryUV();
	UVSeamEdges.SetNum(Mesh->MaxEdgeID());
	NormalSeamEdges.SetNum(Mesh->MaxEdgeID());
	ParallelFor(Mesh->MaxEdgeID(), [&](int32 eid)
	{
		if (Mesh->IsEdge(eid))
		{
			UVSeamEdges[eid] = UVs->IsSeamEdge(eid);
			NormalSeamEdges[eid] = Normals->IsSeamEdge(eid);
		}
	});
}


void UMeshGroupPaintTool::OnSelectedGroupLayerChanged()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ChangeActiveGroupLayer", "Change Polygroup Layer"));

	TArray<int32> InitialFrozenGroups = FrozenGroups;

	int32 ActiveLayerIndex = (ActiveGroupSet) ? ActiveGroupSet->GetPolygroupIndex() : -1;
	UpdateActiveGroupLayer();
	int32 NewLayerIndex = (ActiveGroupSet) ? ActiveGroupSet->GetPolygroupIndex() : -1;

	if (ActiveLayerIndex != NewLayerIndex)
	{
		// clear frozen groups
		EmitFrozenGroupsChange(InitialFrozenGroups, FrozenGroups, LOCTEXT("ClearAllFrozenGroups", "Clear Frozen Groups"));

		TUniquePtr<TSimpleValueLambdaChange<int32>> GroupLayerChange = MakeUnique<TSimpleValueLambdaChange<int32>>();
		GroupLayerChange->FromValue = ActiveLayerIndex;
		GroupLayerChange->ToValue = NewLayerIndex;
		GroupLayerChange->ValueChangeFunc = [this](UObject*, int32 FromIndex, int32 ToIndex, bool)
		{
			this->PolygroupLayerProperties->SetSelectedFromPolygroupIndex(ToIndex);
			this->PolygroupLayerProperties->SilentUpdateWatched();		// to prevent OnSelectedGroupLayerChanged() from being called immediately
			this->UpdateActiveGroupLayer();
		};
		GetToolManager()->EmitObjectChange(this, MoveTemp(GroupLayerChange), LOCTEXT("ChangeActiveGroupLayer", "Change Polygroup Layer"));
	}

	GetToolManager()->EndUndoTransaction();
}


void UMeshGroupPaintTool::UpdateActiveGroupLayer()
{
	if (PolygroupLayerProperties->HasSelectedPolygroup() == false)
	{
		ActiveGroupSet = MakeUnique<UE::Geometry::FPolygroupSet>(GetSculptMesh());
	}
	else
	{
		FName SelectedName = PolygroupLayerProperties->ActiveGroupLayer;
		FDynamicMeshPolygroupAttribute* FoundAttrib = UE::Geometry::FindPolygroupLayerByName(*GetSculptMesh(), SelectedName);
		ensureMsgf(FoundAttrib, TEXT("Selected Attribute Not Found! Falling back to Default group layer."));
		ActiveGroupSet = MakeUnique<UE::Geometry::FPolygroupSet>(GetSculptMesh(), FoundAttrib);
	}

	// need to reset everything here...
	FrozenGroups.Reset();

	// update colors
	DynamicMeshComponent->FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();
}



void UMeshGroupPaintTool::UpdateSubToolType(EMeshGroupPaintInteractionType NewType)
{
	bool bSculptPropsVisible = (NewType == EMeshGroupPaintInteractionType::Brush);
	SetToolPropertySourceEnabled(FilterProperties, bSculptPropsVisible);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::BrushProperties, bSculptPropsVisible);
	SetBrushOpPropsVisibility(bSculptPropsVisible);
}


void UMeshGroupPaintTool::UpdateBrushType(EMeshGroupPaintBrushType BrushType)
{
	static const FText BaseMessage = LOCTEXT("OnStartTool", "Hold Shift to Erase. [/] and S/D change Size (+Shift to small-step). Shift+Q for New Group, Shift+G to pick Group, Shift+F to Freeze Group.");
	FTextBuilder Builder;
	Builder.AppendLine(BaseMessage);

	SetActivePrimaryBrushType((int32)BrushType);

	SetToolPropertySourceEnabled(GizmoProperties, false);

	GetToolManager()->DisplayMessage(Builder.ToText(), EToolMessageLevel::UserNotification);
}




void UMeshGroupPaintTool::RequestAction(EMeshGroupPaintToolActions ActionType)
{
	if (!bHavePendingAction)
	{
		PendingAction = ActionType;
		bHavePendingAction = true;
	}
}


void UMeshGroupPaintTool::ApplyAction(EMeshGroupPaintToolActions ActionType)
{
	switch (ActionType)
	{
	case EMeshGroupPaintToolActions::ClearFrozen:
		ClearAllFrozenGroups();
		break;

	case EMeshGroupPaintToolActions::FreezeCurrent:
		ToggleFrozenGroup(PaintBrushOpOperties->Group);
		break;

	case EMeshGroupPaintToolActions::FreezeOthers:
		FreezeOtherGroups(PaintBrushOpOperties->Group);
		break;

	case EMeshGroupPaintToolActions::GrowCurrent:
		GrowCurrentGroupAction();
		break;

	case EMeshGroupPaintToolActions::ShrinkCurrent:
		ShrinkCurrentGroupAction();
		break;
	}
}



#undef LOCTEXT_NAMESPACE
