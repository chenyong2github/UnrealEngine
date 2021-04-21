// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/MeshSelectionMechanic.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "InteractiveToolManager.h"
#include "Polyline3.h"
#include "Selections/MeshConnectedComponents.h"
#include "SceneManagement.h"
#include "Spatial/GeometrySet3.h"
#include "ToolSceneQueriesUtil.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UMeshSelectionMechanic"

void UMeshSelectionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	// TODO: Add shift/ctrl modifiers
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	ParentTool->AddInputBehavior(ClickBehavior);
}

void UMeshSelectionMechanic::Shutdown()
{
	// TODO: Will need to destroy preview actors here once using those for rendering
}

void UMeshSelectionMechanic::AddSpatial(TSharedPtr<FDynamicMeshAABBTree3> SpatialIn)
{
	MeshSpatials.Add(SpatialIn);
}

const FDynamicMeshSelection& UMeshSelectionMechanic::GetCurrentSelection() const
{
	return CurrentSelection;
}

void UMeshSelectionMechanic::SetSelection(const FDynamicMeshSelection& Selection, bool bBroadcast, bool bEmitChange)
{
	CurrentSelection = Selection;
	if (bBroadcast)
	{
		OnSelectionChanged.Broadcast();
	}
	// TODO: Undo/redo
}

void UMeshSelectionMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	// TODO:We do this in other places, but should CameraState be cached somewhere else?
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (CurrentSelection.IsEmpty())
	{
		return;
	}

	// TODO: We should do all the selection with line/triangle components instead of PDI
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	float PDIScale = RenderAPI->GetCameraState().GetPDIScalingFactor();
	float LineThickness = 3.0 * PDIScale;
	float DepthBias = 3.0f;

	if (CurrentSelection.Type == FDynamicMeshSelection::EType::Triangle)
	{
		for (int32 Tid : CurrentSelection.SelectedIDs)
		{
			for (int i = 0; i < 3; ++i)
			{
				int NextIndex = (i + 1) % 3;
				PDI->DrawLine((FVector)CurrentSelection.Mesh->GetTriVertex(Tid, i),
					(FVector)CurrentSelection.Mesh->GetTriVertex(Tid, NextIndex), FLinearColor::Yellow,
					SDPG_Foreground, LineThickness, DepthBias, true);
			}
		}
	}
	else if (CurrentSelection.Type == FDynamicMeshSelection::EType::Edge)
	{
		for (int32 Eid : CurrentSelection.SelectedIDs)
		{
			FIndex2i EdgeVids = CurrentSelection.Mesh->GetEdgeV(Eid);
			PDI->DrawLine((FVector)CurrentSelection.Mesh->GetVertex(EdgeVids.A),
				(FVector)CurrentSelection.Mesh->GetVertex(EdgeVids.B), FLinearColor::Yellow,
				SDPG_Foreground, LineThickness, DepthBias, true);
		}
	}
}

FInputRayHit UMeshSelectionMechanic::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	// We grab any clicks we are offered because even if we miss the mesh, we want to be able to clear selection.
	FInputRayHit Hit; 
	Hit.bHit = true;
	return Hit;
}

void UMeshSelectionMechanic::OnClicked(const FInputDeviceRay& ClickPos)
{
	FDynamicMeshSelection OriginalSelection = CurrentSelection;

	CurrentSelection.SelectedIDs.Empty();
	CurrentSelection.Mesh = nullptr;

	int32 HitTid = IndexConstants::InvalidID;
	double RayT = 0; 
	for (TSharedPtr<FDynamicMeshAABBTree3> Spatial : MeshSpatials)
	{
		if (Spatial->FindNearestHitTriangle(ClickPos.WorldRay, RayT, HitTid))
		{
			CurrentSelection.Mesh = Spatial->GetMesh();
			break;
		}
	}

	if (HitTid != IndexConstants::InvalidID)
	{
		if (SelectionMode == EMeshSelectionMechanicMode::Component)
		{
			CurrentSelection.SelectedIDs.Add(HitTid);
			FMeshConnectedComponents MeshSelectedComponent(CurrentSelection.Mesh);
			MeshSelectedComponent.FindTrianglesConnectedToSeeds(CurrentSelection.SelectedIDs.Array());
			CurrentSelection.SelectedIDs = TSet(MeshSelectedComponent.Components[0].Indices);
			CurrentSelection.Type = FDynamicMeshSelection::EType::Triangle;
		}
		// TODO: We'll need the ability to hit occluded triangles to see if there is a better edge
		// to snap to.
		else if (SelectionMode == EMeshSelectionMechanicMode::Edge)
		{
			// Try to snap to one of the edges.
			FIndex3i Eids = CurrentSelection.Mesh->GetTriEdges(HitTid);

			FGeometrySet3 GeometrySet;
			for (int i = 0; i < 3; ++i)
			{
				FIndex2i Vids = CurrentSelection.Mesh->GetEdgeV(Eids[i]);
				FPolyline3d Polyline(CurrentSelection.Mesh->GetVertex(Vids.A), CurrentSelection.Mesh->GetVertex(Vids.B));
				GeometrySet.AddCurve(Eids[i], Polyline);
			}
			FGeometrySet3::FNearest Result;
			if (GeometrySet.FindNearestCurveToRay(ClickPos.WorldRay, Result, 
				[this](const FVector3d& Position1, const FVector3d& Position2) {
					return ToolSceneQueriesUtil::PointSnapQuery(CameraState,
						Position1, Position2,
						ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD()); }))
			{
				CurrentSelection.SelectedIDs.Add(Result.ID);
				CurrentSelection.Type = FDynamicMeshSelection::EType::Edge;
			}
		}
	}

	if (!(OriginalSelection == CurrentSelection))
	{
		OnSelectionChanged.Broadcast();
	}
}

#undef LOCTEXT_NAMESPACE
