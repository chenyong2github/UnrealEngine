// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/MeshSelectionMechanic.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "InteractiveToolManager.h"
#include "Polyline3.h"
#include "Selections/MeshConnectedComponents.h"
#include "SceneManagement.h"
#include "Spatial/GeometrySet3.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UMeshSelectionMechanic"

void UMeshSelectionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	// TODO: Add shift/ctrl modifiers
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	ClickBehavior->Initialize(this);
	ParentTool->AddInputBehavior(ClickBehavior);

	LineSet = NewObject<ULineSetComponent>();
	LineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));
}

void UMeshSelectionMechanic::SetWorld(UWorld* World)
{
	// It may be unreasonable to worry about SetWorld being called more than once, but let's be safe anyway
	if (PreviewGeometryActor)
	{
		PreviewGeometryActor->Destroy();
	}

	// We need the world so we can create the geometry actor in the right place.
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	PreviewGeometryActor = World->SpawnActor<APreviewGeometryActor>(FVector::ZeroVector, Rotation, SpawnInfo);

	// Attach the rendering component to the actor
	LineSet->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PreviewGeometryActor->SetRootComponent(LineSet);
	if (LineSet->IsRegistered())
	{
		LineSet->ReregisterComponent();
	}
	else
	{
		LineSet->RegisterComponent();
	}
}

void UMeshSelectionMechanic::Shutdown()
{
	if (PreviewGeometryActor)
	{
		PreviewGeometryActor->Destroy();
		PreviewGeometryActor = nullptr;
	}
}

void UMeshSelectionMechanic::AddSpatial(TSharedPtr<FDynamicMeshAABBTree3> SpatialIn, const FTransform& TransformIn)
{
	MeshSpatials.Add(SpatialIn);
	MeshTransforms.Add(TransformIn);
}

const FDynamicMeshSelection& UMeshSelectionMechanic::GetCurrentSelection() const
{
	return CurrentSelection;
}

void UMeshSelectionMechanic::SetSelection(const FDynamicMeshSelection& Selection, bool bBroadcast, bool bEmitChange)
{
	CurrentSelection = Selection;

	if (MeshSpatials[CurrentSelectionIndex]->GetMesh() != Selection.Mesh)
	{
		for (int32 i = 0; i < MeshSpatials.Num(); ++i)
		{
			if (MeshSpatials[i]->GetMesh() == Selection.Mesh)
			{
				CurrentSelectionIndex = i;
				break;
			}
		}
	}

	UpdateCentroid();
	RebuildDrawnElements(FTransform(CurrentSelectionCentroid));

	if (bBroadcast)
	{
		OnSelectionChanged.Broadcast();
	}
	// TODO: Undo/redo
}

void UMeshSelectionMechanic::RebuildDrawnElements(const FTransform& StartTransform)
{
	LineSet->Clear();
	PreviewGeometryActor->SetActorTransform(StartTransform);

	// For us to end up with the StartTransform, we have to invert it, but only after applying
	// the mesh transform to begin with.
	auto TransformToApply = [&StartTransform, this](const FVector& VectorIn)
	{
		return StartTransform.InverseTransformPosition(
			MeshTransforms[CurrentSelectionIndex].TransformPosition(VectorIn));
	};

	if (CurrentSelection.Type == FDynamicMeshSelection::EType::Triangle)
	{
		for (int32 Tid : CurrentSelection.SelectedIDs)
		{
			FIndex3i Vids = CurrentSelection.Mesh->GetTriangle(Tid);
			FVector Points[3];
			for (int i = 0; i < 3; ++i)
			{
				Points[i] = TransformToApply(CurrentSelection.Mesh->GetVertex(Vids[i]));
			}
			for (int i = 0; i < 3; ++i)
			{
				int NextIndex = (i + 1) % 3;
				LineSet->AddLine(Points[i], Points[NextIndex],
					LineColor, LineThickness, DepthBias);
			}
		}
	}
	else if (CurrentSelection.Type == FDynamicMeshSelection::EType::Edge)
	{
		for (int32 Eid : CurrentSelection.SelectedIDs)
		{
			FIndex2i EdgeVids = CurrentSelection.Mesh->GetEdgeV(Eid);
			LineSet->AddLine(
				TransformToApply(CurrentSelection.Mesh->GetVertex(EdgeVids.A)),
				TransformToApply(CurrentSelection.Mesh->GetVertex(EdgeVids.B)),
					LineColor, LineThickness, DepthBias);
		}
	}
}

void UMeshSelectionMechanic::UpdateCentroid()
{
	CurrentSelectionCentroid = FVector3d(0);
	if (CurrentSelection.IsEmpty())
	{
		return;
	}

	if (CurrentSelection.Type == FDynamicMeshSelection::EType::Edge)
	{
		for (int32 Eid : CurrentSelection.SelectedIDs)
		{
			CurrentSelectionCentroid += CurrentSelection.Mesh->GetEdgePoint(Eid, 0.5);
		}
		CurrentSelectionCentroid /= CurrentSelection.SelectedIDs.Num();
	}
	else if (CurrentSelection.Type == FDynamicMeshSelection::EType::Triangle)
	{
		for (int32 Tid : CurrentSelection.SelectedIDs)
		{
			CurrentSelectionCentroid += CurrentSelection.Mesh->GetTriCentroid(Tid);
		}
		CurrentSelectionCentroid /= CurrentSelection.SelectedIDs.Num();
	}
}

FVector3d UMeshSelectionMechanic::GetCurrentSelectionCentroid()
{
	return CurrentSelectionCentroid;
}


void UMeshSelectionMechanic::SetDrawnElementsTransform(const FTransform& Transform)
{
	PreviewGeometryActor->SetActorTransform(Transform);
}

void UMeshSelectionMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	// TODO:We do this in other places, but should CameraState be cached somewhere else?
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (CurrentSelection.IsEmpty())
	{
		return;
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

	if (!InMultiSelectMode() ||
		(SelectionMode == EMeshSelectionMechanicMode::Component && CurrentSelection.Type != FDynamicMeshSelection::EType::Triangle) ||
		(SelectionMode == EMeshSelectionMechanicMode::Edge && CurrentSelection.Type != FDynamicMeshSelection::EType::Edge) ) 
	{ // If we're not enabling multiselect, or our desired mode doesn't match our current selection type, clear the existing selection
		CurrentSelection.SelectedIDs.Empty();
		CurrentSelection.Mesh = nullptr;
	}

	int32 HitTid = IndexConstants::InvalidID;
	double RayT = 0; 
	for (int32 i = 0; i < MeshSpatials.Num(); ++i)
	{
		//Short circuit the loop if we're in multiselect mode, to save us from testing things we aren't allowed to select
		if (InMultiSelectMode() && !CurrentSelection.SelectedIDs.IsEmpty() && i != CurrentSelectionIndex) 
		{
			continue;
		}

		// Minor note: Using FRay3d in this statement instead of FRay causes an internal compiler error in VS
		// in Development Editor builds, for no good reason.
		FRay LocalRay(
			MeshTransforms[i].InverseTransformPosition(ClickPos.WorldRay.Origin),
			MeshTransforms[i].InverseTransformVector(ClickPos.WorldRay.Direction));

		if (MeshSpatials[i]->FindNearestHitTriangle(LocalRay, RayT, HitTid))
		{
			// It should be okay to reassign this. If we're in multiselect, and it misses a hit, this just won't happen and selection won't be altered.
			CurrentSelection.Mesh = MeshSpatials[i]->GetMesh();
			CurrentSelectionIndex = i;
			break;
		}
	}

	if (HitTid != IndexConstants::InvalidID)
	{
		if (SelectionMode == EMeshSelectionMechanicMode::Component)
		{
			FMeshConnectedComponents MeshSelectedComponent(CurrentSelection.Mesh);
			TArray<int32> SeedTriangles;
			SeedTriangles.Add(HitTid);
			MeshSelectedComponent.FindTrianglesConnectedToSeeds(SeedTriangles);
			// Since we've already handled the situation of not holding triangles in a multiselect above, this should be safe.
			// But just to be sure, we'll toss in a check to validate everything.
			ensure((InMultiSelectMode() && CurrentSelection.Type == FDynamicMeshSelection::EType::Triangle) || CurrentSelection.SelectedIDs.IsEmpty());
			CurrentSelection.SelectedIDs.Append( TSet(MeshSelectedComponent.Components[0].Indices) );
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
				// Since we've already handled multiselect and not holding edges above, this should be safe.	
				// But just to be sure, we'll toss in a check to validate everything.
				ensure((InMultiSelectMode() && CurrentSelection.Type == FDynamicMeshSelection::EType::Edge) || CurrentSelection.SelectedIDs.IsEmpty());
				CurrentSelection.SelectedIDs.Add(Result.ID);
				CurrentSelection.Type = FDynamicMeshSelection::EType::Edge;
			}
		}
	}

	if (!(OriginalSelection == CurrentSelection))
	{
		UpdateCentroid();
		RebuildDrawnElements(FTransform(CurrentSelectionCentroid));
		OnSelectionChanged.Broadcast();
	}
}

void UMeshSelectionMechanic::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	switch (ModifierID)
	{
	case ShiftModifierID:
		bShiftToggle = bIsOn;
		break;
	// Add more modifiers here, if needed.
	}
}

#undef LOCTEXT_NAMESPACE
