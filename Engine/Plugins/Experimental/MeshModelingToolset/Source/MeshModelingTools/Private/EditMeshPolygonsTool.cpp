// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditMeshPolygonsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "SegmentTypes.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "ToolSceneQueriesUtil.h"
#include "Intersection/IntersectionUtil.h"
#include "FindPolygonsAlgorithm.h"
#include "Transforms/MultiTransformer.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "Util/ColorConstants.h"

#include "Async/ParallelFor.h"
#include "Containers/BitArray.h"

#define LOCTEXT_NAMESPACE "UEditMeshPolygonsTool"



/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UEditMeshPolygonsToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UEditMeshPolygonsTool* DeformTool = NewObject<UEditMeshPolygonsTool>(SceneState.ToolManager);
	return DeformTool;
}

/*
 * Tool
 */
UPolyEditTransformProperties::UPolyEditTransformProperties()
{
	TransformMode = EMultiTransformerMode::DefaultGizmo;
	bSelectVertices = true;
	bSelectFaces = true;
	bSelectEdges = true;
	bShowWireframe = false;
	bSnapToWorldGrid = false;
	PolygonMode = EPolygonGroupMode::KeepInputPolygons;
	PolygonGroupingAngleThreshold = .5;
}

#if WITH_EDITOR
void UPolyEditTransformProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// skip interactive updates for PolygonGroupingAngleThreshold
	// TODO: thread the polygon group compute and remove this update skip
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UPolyEditTransformProperties, PolygonGroupingAngleThreshold) && PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif


/*
* Tool methods
*/

UEditMeshPolygonsTool::UEditMeshPolygonsTool()
{
}

void UEditMeshPolygonsTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	// register click behavior
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());

	// set materials
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	// dynamic mesh configuration settings
	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	InitialMesh = MakeUnique<FDynamicMesh3>(*DynamicMeshComponent->GetMesh());
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshChanged.Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UEditMeshPolygonsTool::OnDynamicMeshComponentChanged));


	// add properties
	TransformProps = NewObject<UPolyEditTransformProperties>(this);
	TransformProps->PolygonMode = DynamicMeshComponent->GetMesh()->HasTriangleGroups() && DynamicMeshComponent->GetMesh()->MaxGroupID() > 1 ? EPolygonGroupMode::KeepInputPolygons : EPolygonGroupMode::RecomputePolygonsByAngleThreshold;
	if (TransformProps->PolygonMode == EPolygonGroupMode::RecomputePolygonsByAngleThreshold)
	{
		ComputePolygons(false);
	}
	BackupTriangleGroups();
	AddToolPropertySource(TransformProps);

	// initialize AABBTree
	MeshSpatial.SetMesh(DynamicMeshComponent->GetMesh());
	PrecomputeTopology();

	//initialize topology selector
	TopoSelector.Initialize(DynamicMeshComponent->GetMesh(), &Topology);
	TopoSelector.SetSpatialSource([this]() {return &GetSpatial(); });
	TopoSelector.PointsWithinToleranceTest = [this](const FVector3d& Position1, const FVector3d& Position2) {
		FTransform Transform = ComponentTarget->GetWorldTransform();
		return ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, 
			Transform.TransformPosition((FVector)Position1), Transform.TransformPosition((FVector)Position2), VisualAngleSnapThreshold);
	};

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// init state flags flags
	bInDrag = false;

	// initialize snap solver
	QuickAxisTranslater.Initialize();
	QuickAxisRotator.Initialize();

	// set up visualizers
	PolyEdgesRenderer.LineColor = FLinearColor::Red;
	PolyEdgesRenderer.LineThickness = 2.0;
	HilightRenderer.LineColor = FLinearColor::Green;
	HilightRenderer.LineThickness = 4.0f;
	SelectionRenderer.LineColor = LinearColors::Gold3f<FLinearColor>();
	SelectionRenderer.LineThickness = 4.0f;

	MultiTransformer = NewObject<UMultiTransformer>(this);
	MultiTransformer->Setup(GetToolManager()->GetPairedGizmoManager());
	MultiTransformer->OnTransformStarted.AddUObject(this, &UEditMeshPolygonsTool::OnMultiTransformerTransformBegin);
	MultiTransformer->OnTransformUpdated.AddUObject(this, &UEditMeshPolygonsTool::OnMultiTransformerTransformUpdate);
	MultiTransformer->OnTransformCompleted.AddUObject(this, &UEditMeshPolygonsTool::OnMultiTransformerTransformEnd);
	MultiTransformer->SetSnapToWorldGridSourceFunc([this]() {
		return TransformProps->bSnapToWorldGrid
			&& GetToolManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() == EToolContextCoordinateSystem::World;
	});
	MultiTransformer->SetGizmoVisibility(false);

	TransformerModeWatcher.Initialize(
		[this]() { return this->TransformProps->TransformMode; },
		[this](EMultiTransformerMode NewMode) { this->UpdateTransformerMode(NewMode); },
		TransformProps->TransformMode);

}

void UEditMeshPolygonsTool::Shutdown(EToolShutdownType ShutdownType)
{
	MultiTransformer->Shutdown();

	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("EditMeshPolygonsToolTransactionName", "Deform Mesh"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FConversionToMeshDescriptionOptions ConversionOptions;
				ConversionOptions.bSetPolyGroups = false; // don't save polygroups, as we may change these temporarily in this tool just to get a different edit effect
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, false, ConversionOptions);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}



void UEditMeshPolygonsTool::NextTransformTypeAction()
{
	if (bInDrag == false)
	{
		if (TransformProps->TransformMode == EMultiTransformerMode::DefaultGizmo)
		{
			TransformProps->TransformMode = EMultiTransformerMode::QuickAxisTranslation;
		}
		else
		{
			TransformProps->TransformMode = EMultiTransformerMode::DefaultGizmo;
		}
		UpdateQuickTransformer();
	}
}



void UEditMeshPolygonsTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
		TEXT("NextTransformType"),
		LOCTEXT("NextTransformType", "Next Transform Type"),
		LOCTEXT("NextTransformTypeTooltip", "Cycle to next transform type"),
		EModifierKey::None, EKeys::Q,
		[this]() { NextTransformTypeAction(); });
}




void UEditMeshPolygonsTool::OnDynamicMeshComponentChanged()
{
	bSpatialDirty = true;
	TopoSelector.Invalidate(true, false);
}

FDynamicMeshAABBTree3& UEditMeshPolygonsTool::GetSpatial()
{
	if (bSpatialDirty)
	{
		MeshSpatial.Build();
		bSpatialDirty = false;
	}
	return MeshSpatial;
}



void UEditMeshPolygonsTool::UpdateTransformerMode(EMultiTransformerMode NewMode)
{
	MultiTransformer->SetMode(NewMode);
}



bool UEditMeshPolygonsTool::HitTest(const FRay& WorldRay, FHitResult& OutHit)
{
	FGroupTopologySelection Selection;
	return TopologyHitTest(WorldRay, OutHit, Selection);
}


bool UEditMeshPolygonsTool::TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit, FGroupTopologySelection& OutSelection)
{
	FTransform Transform = ComponentTarget->GetWorldTransform();
	FRay3d LocalRay(Transform.InverseTransformPosition(WorldRay.Origin),
		Transform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	UpdateTopoSelector();
	FVector3d LocalPosition, LocalNormal;
	if (TopoSelector.FindSelectedElement(LocalRay, OutSelection, LocalPosition, LocalNormal) == false)
	{
		return false;
	}

	if (OutSelection.SelectedCornerIDs.Num() > 0)
	{
		OutHit.FaceIndex = OutSelection.SelectedCornerIDs[0];
		OutHit.Distance = LocalRay.Project(LocalPosition);
		OutHit.ImpactPoint = Transform.TransformPosition((FVector)LocalRay.PointAt(OutHit.Distance));
	}
	else if (OutSelection.SelectedEdgeIDs.Num() > 0)
	{
		OutHit.FaceIndex = OutSelection.SelectedEdgeIDs[0];
		OutHit.Distance = LocalRay.Project(LocalPosition);
		OutHit.ImpactPoint = Transform.TransformPosition((FVector)LocalRay.PointAt(OutHit.Distance));
	}
	else
	{
		int HitTID = GetSpatial().FindNearestHitTriangle(LocalRay);
		if (HitTID != IndexConstants::InvalidID)
		{
			FTriangle3d Triangle;
			GetSpatial().GetMesh()->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
			FIntrRay3Triangle3d Query(LocalRay, Triangle);
			Query.Find();
			OutHit.FaceIndex = HitTID;
			OutHit.Distance = Query.RayParameter;
			OutHit.Normal = Transform.TransformVectorNoScale((FVector)GetSpatial().GetMesh()->GetTriNormal(HitTID));
			OutHit.ImpactPoint = Transform.TransformPosition((FVector)LocalRay.PointAt(Query.RayParameter));
		}
	}
	return true;
}




FInputRayHit UEditMeshPolygonsTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	if (TransformProps->TransformMode == EMultiTransformerMode::DefaultGizmo)
	{
		return FInputRayHit();
	}
	return UMeshSurfacePointTool::CanBeginClickDragSequence(PressPos);
}


void UEditMeshPolygonsTool::UpdateTopoSelector()
{
	bool bFaces = TransformProps->bSelectFaces;
	bool bEdges = TransformProps->bSelectEdges;
	bool bVertices = TransformProps->bSelectVertices;

	if (PersistentSelection.IsEmpty() == false)
	{
		bFaces = bFaces && PersistentSelection.SelectedGroupIDs.Num() > 0;
		bEdges = bEdges && PersistentSelection.SelectedEdgeIDs.Num() > 0;
		bVertices = bVertices && PersistentSelection.SelectedCornerIDs.Num() > 0;
	}
	
	TopoSelector.UpdateEnableFlags(bFaces, bEdges, bVertices);
}


FInputRayHit UEditMeshPolygonsTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	if (TransformProps->TransformMode != EMultiTransformerMode::DefaultGizmo)
	{
		return FInputRayHit();
	}

	FHitResult OutHit;
	if (HitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	// background capture, if nothing else is hit
	return FInputRayHit(TNumericLimits<float>::Max());
}

void UEditMeshPolygonsTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	FTransform Transform = ComponentTarget->GetWorldTransform();
	FRay3d LocalRay(Transform.InverseTransformPosition(ClickPos.WorldRay.Origin),
		Transform.InverseTransformVector(ClickPos.WorldRay.Direction));
	LocalRay.Direction.Normalize();

	UpdateTopoSelector();

	FVector3d LocalPosition, LocalNormal;
	FGroupTopologySelection Selection;
	if (TopoSelector.FindSelectedElement(LocalRay, Selection, LocalPosition, LocalNormal))
	{
		if (GetShiftToggle())
		{
			PersistentSelection.Toggle(Selection);
		}
		else
		{
			PersistentSelection = Selection;
		}
	}
	else
	{
		PersistentSelection.Clear();
	}

	// really just want show hide here....
	if (PersistentSelection.IsEmpty())
	{
		MultiTransformer->SetGizmoVisibility(false);
		return;
	}
	else
	{
		MultiTransformer->SetGizmoVisibility(true);
	}

	// update selection
	FFrame3d SelectionFrame = Topology.GetSelectionFrame(PersistentSelection);
	SelectionFrame.Transform(Transform);
	MultiTransformer->SetGizmoPositionFromWorldFrame(SelectionFrame);
}



void UEditMeshPolygonsTool::OnBeginDrag(const FRay& WorldRay)
{
	FTransform Transform = ComponentTarget->GetWorldTransform();
	FRay3d LocalRay(Transform.InverseTransformPosition(WorldRay.Origin),
		Transform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	HilightSelection.Clear();

	TopoSelector.UpdateEnableFlags(TransformProps->bSelectFaces, TransformProps->bSelectEdges, TransformProps->bSelectVertices);
	FGroupTopologySelection Selection;
	FVector3d LocalPosition, LocalNormal;
	bool bHit = TopoSelector.FindSelectedElement(LocalRay, Selection, LocalPosition, LocalNormal);

	if (bHit == false)
	{
		bInDrag = false;
		return;
	}

	HilightSelection = Selection;

	FVector WorldHitPos = Transform.TransformPosition((FVector)LocalPosition);
	FVector WorldHitNormal = Transform.TransformVector((FVector)LocalNormal);

	bInDrag = true;
	StartHitPosWorld = WorldHitPos;
	LastHitPosWorld = StartHitPosWorld;
	StartHitNormalWorld = WorldHitNormal;

	QuickAxisRotator.ClearAxisLock();
	UpdateActiveSurfaceFrame(HilightSelection);
	UpdateQuickTransformer();

	LastBrushPosLocal = Transform.InverseTransformPosition(LastHitPosWorld);
	StartBrushPosLocal = LastBrushPosLocal;

	UpdateDeformerFromSelection(Selection);
	
	BeginChange();
}



void UEditMeshPolygonsTool::UpdateActiveSurfaceFrame(FGroupTopologySelection& Selection)
{
	FTransform3d Transform(ComponentTarget->GetWorldTransform());

	// update surface frame
	ActiveSurfaceFrame.Origin = StartHitPosWorld;
	if (HilightSelection.SelectedCornerIDs.Num() == 1)
	{
		// just keeping existing axes...we don't have enough info to do something smarter
	}
	else
	{
		ActiveSurfaceFrame.AlignAxis(2, StartHitNormalWorld);
		if (HilightSelection.SelectedEdgeIDs.Num() == 1)
		{
			FVector3d Tangent;
			if (Topology.GetGroupEdgeTangent(HilightSelection.SelectedEdgeIDs[0], Tangent))
			{
				Tangent = Transform.TransformVector(Tangent);
				ActiveSurfaceFrame.ConstrainedAlignAxis(0, Tangent, ActiveSurfaceFrame.Z());
			}
		}
	}
}


FQuickTransformer* UEditMeshPolygonsTool::GetActiveQuickTransformer()
{
	//if (TransformProps->TransformMode == EQuickTransformerMode::AxisRotation)
	//{
	//	return &QuickAxisRotator;
	//}
	//else
	//{
	//	return &QuickAxisTranslater;
	//}
	if (TransformProps->TransformMode == EMultiTransformerMode::QuickAxisTranslation)
	{
		return &QuickAxisTranslater;
	}
	return nullptr;
}


void UEditMeshPolygonsTool::UpdateQuickTransformer()
{
	if (GetActiveQuickTransformer() == nullptr)
	{
		return;
	}

	bool bUseLocalAxes =
		(GetToolManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() == EToolContextCoordinateSystem::Local);
	if (bUseLocalAxes)
	{
		GetActiveQuickTransformer()->SetActiveWorldFrame(ActiveSurfaceFrame);
	}
	else
	{
		GetActiveQuickTransformer()->SetActiveFrameFromWorldAxes(StartHitPosWorld);
	}
}





void UEditMeshPolygonsTool::UpdateChangeFromROI(bool bFinal)
{
	if (ActiveVertexChange == nullptr)
	{
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	const TSet<int>& ModifiedVertices = LinearDeformer.GetModifiedVertices();
	ActiveVertexChange->SavePositions(Mesh, ModifiedVertices, !bFinal);
}


void UEditMeshPolygonsTool::OnUpdateDrag(const FRay& Ray)
{
	if (bInDrag)
	{
		bUpdatePending = true;
		UpdateRay = Ray;
	}
}

void UEditMeshPolygonsTool::OnEndDrag(const FRay& Ray)
{
	bInDrag = false;
	bUpdatePending = false;

	// update spatial
	bSpatialDirty = true;

	HilightSelection.Clear(); 
	TopoSelector.Invalidate(true, false);
	QuickAxisRotator.Reset();
	QuickAxisTranslater.Reset();

	// close change record
	EndChange();
}



void UEditMeshPolygonsTool::OnMultiTransformerTransformBegin()
{
	HilightSelection.Clear();

	UpdateDeformerFromSelection(PersistentSelection);

	InitialGizmoFrame = MultiTransformer->GetCurrentGizmoFrame();

	BeginChange();
}

void UEditMeshPolygonsTool::OnMultiTransformerTransformUpdate()
{
	if (MultiTransformer->InGizmoEdit())
	{
		ComputeUpdate_Gizmo();
	}
}

void UEditMeshPolygonsTool::OnMultiTransformerTransformEnd()
{
	bSpatialDirty = true;

	TopoSelector.Invalidate(true, false);

	// close change record
	EndChange();
}



bool UEditMeshPolygonsTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (ActiveVertexChange == nullptr && MultiTransformer->InGizmoEdit() == false )
	{
		FTransform3d Transform(ComponentTarget->GetWorldTransform());
		FRay3d LocalRay(Transform.InverseTransformPosition(DevicePos.WorldRay.Origin),
		Transform.InverseTransformVector(DevicePos.WorldRay.Direction));
		LocalRay.Direction.Normalize();

		HilightSelection.Clear();
		UpdateTopoSelector();
		FVector3d LocalPosition, LocalNormal;
		bool bHit = TopoSelector.FindSelectedElement(LocalRay, HilightSelection, LocalPosition, LocalNormal);

		if (bHit)
		{
			StartHitPosWorld = (FVector)Transform.TransformPosition(LocalPosition);
			StartHitNormalWorld = (FVector)Transform.TransformVector(LocalNormal);

			UpdateActiveSurfaceFrame(HilightSelection);
			UpdateQuickTransformer();
		}
	}
	return true;
}







void UEditMeshPolygonsTool::UpdateDeformerFromSelection(const FGroupTopologySelection& Selection)
{
	//Determine which of the following (corners, edges or faces) has been selected by counting the associated feature's IDs
	if (Selection.SelectedCornerIDs.Num() > 0)
	{
		//Add all the the Corner's adjacent poly-groups (NbrGroups) to the ongoing array of groups.
		LinearDeformer.SetActiveHandleCorners(Selection.SelectedCornerIDs);
	}
	else if (Selection.SelectedEdgeIDs.Num() > 0)
	{
		//Add all the the edge's adjacent poly-groups (NbrGroups) to the ongoing array of groups.
		LinearDeformer.SetActiveHandleEdges(Selection.SelectedEdgeIDs);
	}
	else if (Selection.SelectedGroupIDs.Num() > 0)
	{
		LinearDeformer.SetActiveHandleFaces(Selection.SelectedGroupIDs);
	}
}





void UEditMeshPolygonsTool::ComputeUpdate()
{

	if (bUpdatePending == true)
	{
		//if (TransformProps->TransformMode == EQuickTransformerMode::AxisRotation)
		//{
		//	ComputeUpdate_Rotate();
		//}
		//else
		//{
		//	ComputeUpdate_Translate();
		//}

		if (TransformProps->TransformMode == EMultiTransformerMode::QuickAxisTranslation)
		{
			ComputeUpdate_Translate();
		}

	}

}






void UEditMeshPolygonsTool::ComputeUpdate_Rotate()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FTransform3d Transform(ComponentTarget->GetWorldTransform());
	FVector NewHitPosWorld = LastHitPosWorld;

	FVector3d SnappedPoint;
	if (QuickAxisRotator.UpdateSnap(FRay3d(UpdateRay), SnappedPoint))
	{
		NewHitPosWorld = (FVector)SnappedPoint;
	}
	else
	{
		return;
	}

	// check if we are on back-facing part of rotation in which case we ignore...
	FVector3d SphereCenter = QuickAxisRotator.GetActiveWorldFrame().Origin;
	if (QuickAxisRotator.HaveActiveSnapRotation() && QuickAxisRotator.GetHaveLockedToAxis() == false)
	{
		FVector3d ToSnapPointVec = (SnappedPoint - SphereCenter);
		FVector3d ToEyeVec = (SnappedPoint - (FVector3d)CameraState.Position);
		if (ToSnapPointVec.Dot(ToEyeVec) > 0)
		{
			return;
		}
	}


	// if we haven't snapped to a rotation we can exit
	if (QuickAxisRotator.HaveActiveSnapRotation() == false)
	{
		QuickAxisRotator.ClearAxisLock();

		LinearDeformer.ClearSolution(Mesh);

		DynamicMeshComponent->FastNotifyPositionsUpdated();
		GetToolManager()->PostInvalidation();

		bUpdatePending = false;
		return;
	}

	// ok we have an axis...
	if (QuickAxisRotator.GetHaveLockedToAxis() == false)
	{
		QuickAxisRotator.SetAxisLock();
		RotationStartPointWorld = SnappedPoint;
		RotationStartFrame = QuickAxisRotator.GetActiveRotationFrame();
	}

	FVector2d RotateStartVec = RotationStartFrame.ToPlaneUV(RotationStartPointWorld, 2);
	RotateStartVec.Normalize();
	FVector2d RotateToVec = RotationStartFrame.ToPlaneUV(NewHitPosWorld, 2);
	RotateToVec.Normalize();
	double AngleRad = RotateStartVec.SignedAngleR(RotateToVec);
	FQuaterniond Rotation(
		Transform.InverseTransformVectorNoScale(RotationStartFrame.Z()), AngleRad, false);
	FVector3d LocalOrigin = Transform.InverseTransformPosition(RotationStartFrame.Origin);

	// Update Mesh the rotation,
	LinearDeformer.UpdateSolution(Mesh, [this, LocalOrigin, Rotation](FDynamicMesh3* TargetMesh, int VertIdx)
	{
		FVector3d V = TargetMesh->GetVertex(VertIdx);
		V -= LocalOrigin;
		V = Rotation * V;
		V += LocalOrigin;
		return V;
	});

	DynamicMeshComponent->FastNotifyPositionsUpdated();
	GetToolManager()->PostInvalidation();
	bUpdatePending = false;
}




void UEditMeshPolygonsTool::ComputeUpdate_Translate()
{
	TFunction<FVector3d(const FVector3d&)> PointConstraintFunc = nullptr;
	if (TransformProps->bSnapToWorldGrid 
		&& GetToolManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() == EToolContextCoordinateSystem::World)
	{
		PointConstraintFunc = [&](const FVector3d& Pos)
		{
			FVector3d GridSnapPos;
			return ToolSceneQueriesUtil::FindWorldGridSnapPoint(this, Pos, GridSnapPos) ? GridSnapPos : Pos;
		};
	}

	FTransform3d Transform(ComponentTarget->GetWorldTransform());
	FVector NewHitPosWorld = LastHitPosWorld;
	FVector3d SnappedPoint;
	if (QuickAxisTranslater.UpdateSnap(FRay3d(UpdateRay), SnappedPoint, PointConstraintFunc))
	{
		NewHitPosWorld = (FVector)SnappedPoint;
	}
	else
	{
		return;
	}

	FVector3d NewBrushPosLocal = Transform.InverseTransformPosition(NewHitPosWorld);
	FVector3d NewMoveDelta = NewBrushPosLocal - StartBrushPosLocal;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (LastMoveDelta.SquaredLength() > 0.)
	{
		if (NewMoveDelta.SquaredLength() > 0.)
		{
			LinearDeformer.UpdateSolution(Mesh, [this, NewMoveDelta](FDynamicMesh3* TargetMesh, int VertIdx)
			{
				return TargetMesh->GetVertex(VertIdx) + NewMoveDelta;
			});
		}
		else
		{
			// Reset mesh to initial positions.
			LinearDeformer.ClearSolution(Mesh);
		}
		DynamicMeshComponent->FastNotifyPositionsUpdated();
		GetToolManager()->PostInvalidation();
	}

	LastMoveDelta = NewMoveDelta;
	LastBrushPosLocal = (FVector)NewBrushPosLocal;

	bUpdatePending = false;
}





void UEditMeshPolygonsTool::ComputeUpdate_Gizmo()
{
	if (PersistentSelection.IsEmpty())
	{
		return;
	}

	FFrame3d CurFrame = MultiTransformer->GetCurrentGizmoFrame();
	FVector3d Translation = CurFrame.Origin - InitialGizmoFrame.Origin;
	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector3d LocalTranslation = (FVector3d)Transform.InverseTransformVector((FVector)Translation);


	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (LocalTranslation.SquaredLength() > 0.)
	{
		LinearDeformer.UpdateSolution(Mesh, [this, LocalTranslation](FDynamicMesh3* TargetMesh, int VertIdx)
		{
			return TargetMesh->GetVertex(VertIdx) + LocalTranslation;
		});
	}
	else
	{
		// Reset mesh to initial positions.
		LinearDeformer.ClearSolution(Mesh);
	}
	DynamicMeshComponent->FastNotifyPositionsUpdated();
	GetToolManager()->PostInvalidation();
}



void UEditMeshPolygonsTool::Tick(float DeltaTime)
{
	UMeshSurfacePointTool::Tick(DeltaTime);

	TransformerModeWatcher.CheckAndUpdate();
	MultiTransformer->Tick(DeltaTime);
}


void UEditMeshPolygonsTool::ComputePolygons(bool RecomputeTopology)
{
	switch (TransformProps->PolygonMode)
	{
	case EPolygonGroupMode::KeepInputPolygons:
		SetTriangleGroups(InitialTriangleGroups);
		break;
	case EPolygonGroupMode::RecomputePolygonsByAngleThreshold:
	{
		FFindPolygonsAlgorithm Polygons = FFindPolygonsAlgorithm(InitialMesh.Get());
		double DotTolerance = 1.0 - FMathd::Cos(TransformProps->PolygonGroupingAngleThreshold * FMathd::DegToRad);
		Polygons.FindPolygonsFromFaceNormals(DotTolerance);
		Polygons.FindPolygonEdges();
		SetTriangleGroups(*InitialMesh->GetTriangleGroupsBuffer());
	}
	break;
	case EPolygonGroupMode::PolygonsAreTriangles:
	{
		FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
		int GID = 0;
		for (int TID : Mesh->TriangleIndicesItr())
		{
			Mesh->SetTriangleGroup(TID, GID++);
		}
	}
	break;
	}

	if (RecomputeTopology)
	{
		PrecomputeTopology();
		TopoSelector.Invalidate(false, true);
		HilightSelection.Clear();
	}
}



void UEditMeshPolygonsTool::PrecomputeTopology()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	Topology = FGroupTopology(Mesh, true);

	LinearDeformer.Initialize(Mesh, &Topology);
}




void UEditMeshPolygonsTool::Render(IToolsContextRenderAPI* RenderAPI)
{

	ComputeUpdate();
		
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	DynamicMeshComponent->bExplicitShowWireframe = TransformProps->bShowWireframe;
	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();

	PolyEdgesRenderer.BeginFrame(RenderAPI, CameraState);
	PolyEdgesRenderer.SetTransform(ComponentTarget->GetWorldTransform());


	for (FGroupTopology::FGroupEdge& Edge : Topology.Edges)
	{
		FVector3d A, B;
		for (int eid : Edge.Span.Edges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PolyEdgesRenderer.DrawLine(A, B);
		}
	}

	PolyEdgesRenderer.EndFrame();


	HilightRenderer.BeginFrame(RenderAPI, CameraState);
	HilightRenderer.SetTransform(ComponentTarget->GetWorldTransform());

	TopoSelector.VisualAngleSnapThreshold = this->VisualAngleSnapThreshold;
	TopoSelector.DrawSelection(HilightSelection, &HilightRenderer, &CameraState);
	HilightRenderer.EndFrame();

	if (PersistentSelection.IsEmpty() == false)
	{
		SelectionRenderer.BeginFrame(RenderAPI, CameraState);
		SelectionRenderer.SetTransform(ComponentTarget->GetWorldTransform());
		SelectionRenderer.SetTransform(ComponentTarget->GetWorldTransform());
		TopoSelector.DrawSelection(PersistentSelection, &SelectionRenderer, &CameraState);
		SelectionRenderer.EndFrame();
	}

	if (GetActiveQuickTransformer() != nullptr)
	{
		GetActiveQuickTransformer()->UpdateCameraState(CameraState);
		if (bInDrag)
		{
			GetActiveQuickTransformer()->Render(RenderAPI);
		}
		else
		{
			GetActiveQuickTransformer()->PreviewRender(RenderAPI);
		}
	}
}



void UEditMeshPolygonsTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	// if anything has changed the polygon settings, recompute polygons
	if (Property && 
			(Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPolyEditTransformProperties, PolygonMode)
		  || Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPolyEditTransformProperties, PolygonGroupingAngleThreshold))
		)
	{
		ComputePolygons(true);
	}
}

void UEditMeshPolygonsTool::BackupTriangleGroups()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (Mesh->HasTriangleGroups())
	{
		InitialTriangleGroups = *Mesh->GetTriangleGroupsBuffer();
	}
	else
	{
		InitialTriangleGroups.SetNum(0);
	}
}

void UEditMeshPolygonsTool::SetTriangleGroups(const TDynamicVector<int>& Groups)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	for (int TID = 0, MaxID = Groups.Num(); TID < MaxID; TID++)
	{
		if (Mesh->IsTriangle(TID))
		{
			Mesh->SetTriangleGroup(TID, Groups[TID]);
		}
	}
}


//
// Change Tracking
//


void UEditMeshPolygonsTool::BeginChange()
{
	if (ActiveVertexChange == nullptr)
	{
		ActiveVertexChange = new FMeshVertexChangeBuilder();
		UpdateChangeFromROI(false);
	}
}


void UEditMeshPolygonsTool::EndChange()
{
	if (ActiveVertexChange != nullptr)
	{
		UpdateChangeFromROI(true);
		GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(ActiveVertexChange->Change), LOCTEXT("PolyMeshDeformationChange", "PolyMesh Edit"));
	}

	delete ActiveVertexChange;
	ActiveVertexChange = nullptr;
}



#undef LOCTEXT_NAMESPACE
