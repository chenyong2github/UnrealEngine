// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/PolygonSelectionMechanic.h"
#include "InteractiveToolManager.h"
#include "Util/ColorConstants.h"
#include "ToolSceneQueriesUtil.h"

#define LOCTEXT_NAMESPACE "UPolygonSelectionMechanic"

void UPolygonSelectionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	Properties = NewObject<UPolygonSelectionMechanicProperties>(this);
	if (bAddSelectionFilterPropertiesToParentTool)
	{
		AddToolPropertySource(Properties);
	}

	// set up visualizers
	PolyEdgesRenderer.LineColor = FLinearColor::Red;
	PolyEdgesRenderer.LineThickness = 2.0;
	HilightRenderer.LineColor = FLinearColor::Green;
	HilightRenderer.LineThickness = 4.0f;
	SelectionRenderer.LineColor = LinearColors::Gold3f();
	SelectionRenderer.LineThickness = 4.0f;
}


void UPolygonSelectionMechanic::Initialize(const USimpleDynamicMeshComponent* MeshComponentIn, const FGroupTopology* TopologyIn,
	TFunction<FDynamicMeshAABBTree3*()> GetSpatialSourceFunc,
	TFunction<bool()> GetAddToSelectionModifierStateFuncIn)
{
	this->MeshComponent = MeshComponentIn;
	this->Topology = TopologyIn;

	TargetTransform = FTransform3d(MeshComponent->GetComponentTransform());

	TopoSelector.Initialize(MeshComponent->GetMesh(), Topology);
	this->GetSpatialFunc = GetSpatialSourceFunc;
	TopoSelector.SetSpatialSource(GetSpatialFunc);
	TopoSelector.PointsWithinToleranceTest = [this](const FVector3d& Position1, const FVector3d& Position2) {
		return ToolSceneQueriesUtil::PointSnapQuery(this->CameraState,
			TargetTransform.TransformPosition(Position1), TargetTransform.TransformPosition(Position2));
	};

	GetAddToSelectionModifierStateFunc = GetAddToSelectionModifierStateFuncIn;
}



void UPolygonSelectionMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	const FDynamicMesh3* TargetMesh = MeshComponent->GetMesh();
	FTransform Transform = (FTransform)TargetTransform;

	PolyEdgesRenderer.BeginFrame(RenderAPI, CameraState);
	PolyEdgesRenderer.SetTransform(Transform);
	for (const FGroupTopology::FGroupEdge& Edge : Topology->Edges)
	{
		FVector3d A, B;
		for (int32 eid : Edge.Span.Edges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PolyEdgesRenderer.DrawLine(A, B);
		}
	}
	PolyEdgesRenderer.EndFrame();

	HilightRenderer.BeginFrame(RenderAPI, CameraState);
	HilightRenderer.SetTransform(Transform);
	TopoSelector.DrawSelection(HilightSelection, &HilightRenderer, &CameraState);
	HilightRenderer.EndFrame();

	if (PersistentSelection.IsEmpty() == false)
	{
		SelectionRenderer.BeginFrame(RenderAPI, CameraState);
		SelectionRenderer.SetTransform(Transform);
		TopoSelector.DrawSelection(PersistentSelection, &SelectionRenderer, &CameraState);
		SelectionRenderer.EndFrame();
	}
}





void UPolygonSelectionMechanic::ClearHighlight()
{
	HilightSelection.Clear();
}


void UPolygonSelectionMechanic::NotifyMeshChanged(bool bTopologyModified)
{
	ClearHighlight();
	TopoSelector.Invalidate(true, bTopologyModified);
	if (bTopologyModified)
	{
		PersistentSelection.Clear();
		SelectionTimestamp++;
		OnSelectionChanged.Broadcast();
	}
}


bool UPolygonSelectionMechanic::TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit)
{
	FGroupTopologySelection Selection;
	return TopologyHitTest(WorldRay, OutHit, Selection);
}

bool UPolygonSelectionMechanic::TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit, FGroupTopologySelection& OutSelection)
{
	FRay3d LocalRay(TargetTransform.InverseTransformPosition(WorldRay.Origin),
		TargetTransform.InverseTransformVector(WorldRay.Direction));
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
		OutHit.ImpactPoint = (FVector)TargetTransform.TransformPosition(LocalRay.PointAt(OutHit.Distance));
	}
	else if (OutSelection.SelectedEdgeIDs.Num() > 0)
	{
		OutHit.FaceIndex = OutSelection.SelectedEdgeIDs[0];
		OutHit.Distance = LocalRay.Project(LocalPosition);
		OutHit.ImpactPoint = (FVector)TargetTransform.TransformPosition(LocalRay.PointAt(OutHit.Distance));
	}
	else
	{
		FDynamicMeshAABBTree3* Spatial = GetSpatialFunc();
		int HitTID = Spatial->FindNearestHitTriangle(LocalRay);
		if (HitTID != IndexConstants::InvalidID)
		{
			FTriangle3d Triangle;
			Spatial->GetMesh()->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
			FIntrRay3Triangle3d Query(LocalRay, Triangle);
			if (Query.Find())
			{
				OutHit.FaceIndex = HitTID;
				OutHit.Distance = (float)Query.RayParameter;
				OutHit.Normal = (FVector)TargetTransform.TransformVectorNoScale(Spatial->GetMesh()->GetTriNormal(HitTID));
				OutHit.ImpactPoint = (FVector)TargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
			}
			else
			{
				return false;
			}
		}
	}
	return true;
}



void UPolygonSelectionMechanic::UpdateTopoSelector()
{
	bool bFaces = Properties->bSelectFaces;
	bool bEdges = Properties->bSelectEdges;
	bool bVertices = Properties->bSelectVertices;

	if (PersistentSelection.IsEmpty() == false && GetAddToSelectionModifierStateFunc() == true)
	{
		bFaces = bFaces && PersistentSelection.SelectedGroupIDs.Num() > 0;
		bEdges = bEdges && PersistentSelection.SelectedEdgeIDs.Num() > 0;
		bVertices = bVertices && PersistentSelection.SelectedCornerIDs.Num() > 0;
	}

	TopoSelector.UpdateEnableFlags(bFaces, bEdges, bVertices);
}




bool UPolygonSelectionMechanic::UpdateHighlight(const FRay& WorldRay)
{
	FRay3d LocalRay(TargetTransform.InverseTransformPosition(WorldRay.Origin),
		TargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	HilightSelection.Clear();
	UpdateTopoSelector();
	FVector3d LocalPosition, LocalNormal;
	bool bHit = TopoSelector.FindSelectedElement(LocalRay, HilightSelection, LocalPosition, LocalNormal);
	return bHit;
}




bool UPolygonSelectionMechanic::HasSelection() const
{
	return PersistentSelection.IsEmpty() == false;
}


bool UPolygonSelectionMechanic::UpdateSelection(const FRay& WorldRay, FVector3d& LocalHitPositionOut, FVector3d& LocalHitNormalOut)
{
	FRay3d LocalRay(TargetTransform.InverseTransformPosition(WorldRay.Origin),
		TargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	UpdateTopoSelector();

	bool bSelectionModified = false;
	FVector3d LocalPosition, LocalNormal;
	FGroupTopologySelection Selection;
	if (TopoSelector.FindSelectedElement(LocalRay, Selection, LocalPosition, LocalNormal))
	{
		LocalHitPositionOut = LocalPosition;
		LocalHitNormalOut = LocalNormal;
		if (GetAddToSelectionModifierStateFunc())
		{
			PersistentSelection.Toggle(Selection);
		}
		else
		{
			PersistentSelection = Selection;
		}
		bSelectionModified = true;
	}
	else
	{
		bSelectionModified = (PersistentSelection.IsEmpty() == false);
		PersistentSelection.Clear();
	}

	if (bSelectionModified)
	{
		SelectionTimestamp++;
		OnSelectionChanged.Broadcast();
	}

	return bSelectionModified;
}


void UPolygonSelectionMechanic::SetSelection(const FGroupTopologySelection& Selection)
{
	PersistentSelection = Selection;
	SelectionTimestamp++;
	OnSelectionChanged.Broadcast();
}


void UPolygonSelectionMechanic::ClearSelection()
{
	PersistentSelection.Clear();
	SelectionTimestamp++;
	OnSelectionChanged.Broadcast();
}



void UPolygonSelectionMechanic::BeginChange()
{
	check(ActiveChange.IsValid() == false);
	ActiveChange = MakeUnique<FPolygonSelectionMechanicSelectionChange>();
	ActiveChange->Before = PersistentSelection;
	ActiveChange->Timestamp = SelectionTimestamp;
}

TUniquePtr<FToolCommandChange> UPolygonSelectionMechanic::EndChange()
{
	check(ActiveChange.IsValid() == true);
	ActiveChange->After = PersistentSelection;
	if (SelectionTimestamp != ActiveChange->Timestamp)
	{
		return MoveTemp(ActiveChange);
	}
	ActiveChange = TUniquePtr<FPolygonSelectionMechanicSelectionChange>();
	return TUniquePtr<FToolCommandChange>();
}

bool UPolygonSelectionMechanic::EndChangeAndEmitIfModified()
{
	check(ActiveChange.IsValid() == true);
	ActiveChange->After = PersistentSelection;
	if (SelectionTimestamp != ActiveChange->Timestamp)
	{
		GetParentTool()->GetToolManager()->EmitObjectChange(this, MoveTemp(ActiveChange),
			LOCTEXT("SelectionChange", "Selection Change"));
		return true;
	}
	ActiveChange = TUniquePtr<FPolygonSelectionMechanicSelectionChange>();
	return false;
}


FFrame3d UPolygonSelectionMechanic::GetSelectionFrame(bool bWorld, FFrame3d* InitialLocalFrame) const
{
	FFrame3d UseFrame;
	if (PersistentSelection.IsEmpty() == false)
	{
		UseFrame = Topology->GetSelectionFrame(PersistentSelection, InitialLocalFrame);
	}

	if (bWorld)
	{
		UseFrame.Transform(TargetTransform);
	}

	return UseFrame;
}



void FPolygonSelectionMechanicSelectionChange::Apply(UObject* Object)
{
	UPolygonSelectionMechanic* Mechanic = Cast<UPolygonSelectionMechanic>(Object);
	if (Mechanic)
	{
		Mechanic->PersistentSelection = After;
		Mechanic->OnSelectionChanged.Broadcast();
	}
}
void FPolygonSelectionMechanicSelectionChange::Revert(UObject* Object)
{
	UPolygonSelectionMechanic* Mechanic = Cast<UPolygonSelectionMechanic>(Object);
	if (Mechanic)
	{
		Mechanic->PersistentSelection = Before;
		Mechanic->OnSelectionChanged.Broadcast();
	}
}
FString FPolygonSelectionMechanicSelectionChange::ToString() const
{
	return TEXT("FPolygonSelectionMechanicSelectionChange");
}



#undef LOCTEXT_NAMESPACE