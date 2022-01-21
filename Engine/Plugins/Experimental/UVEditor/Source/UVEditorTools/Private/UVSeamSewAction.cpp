// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVSeamSewAction.h"

#include "Algo/Unique.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "Selection/UVEditorDynamicMeshSelection.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "PreviewMesh.h"
#include "ToolSetupUtil.h"
#include "UVToolContextObjects.h"
#include "UVEditorUXSettings.h"


#define LOCTEXT_NAMESPACE "UUVSeamSewAction"

using namespace UE::Geometry;


UUVSeamSewAction::UUVSeamSewAction()
{
	CurrentSelection = MakeShared<UE::Geometry::FUVEditorDynamicMeshSelection>();
}

void UUVSeamSewAction::SetWorld(UWorld* WorldIn)
{
	UUVToolAction::SetWorld(WorldIn);

	if (UnwrapPreviewGeometryActor)
	{
		UnwrapPreviewGeometryActor->Destroy();
	}

	// We need the world so we can create the geometry actor in the right place.
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	UnwrapPreviewGeometryActor = WorldIn->SpawnActor<APreviewGeometryActor>(FVector::ZeroVector, Rotation, SpawnInfo);

	// Attach the rendering component to the actor
	SewEdgePairingLineSet->Rename(nullptr, UnwrapPreviewGeometryActor); // Changes the "outer"
	UnwrapPreviewGeometryActor->SetRootComponent(SewEdgePairingLineSet);
	if (SewEdgePairingLineSet->IsRegistered())
	{
		SewEdgePairingLineSet->ReregisterComponent();
	}
	else
	{
		SewEdgePairingLineSet->RegisterComponent();
	}

}

void UUVSeamSewAction::Setup(UInteractiveTool* ParentToolIn)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSeamSewAction_Setup);
	
	UUVToolAction::Setup(ParentToolIn);

	SewEdgePairingLineSet = NewObject<ULineSetComponent>();
	SewEdgePairingLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));
}

void UUVSeamSewAction::Shutdown()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSeamSewAction_Shutdown);

	if (UnwrapPreviewGeometryActor)
	{
		UnwrapPreviewGeometryActor->Destroy();
		UnwrapPreviewGeometryActor = nullptr;
	}
}


void UUVSeamSewAction::SetSelection(int32 SelectionTargetIndexIn, const UE::Geometry::FUVEditorDynamicMeshSelection* NewSelection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSeamSewAction_SetSelection);
	
	SelectionTargetIndex = SelectionTargetIndexIn;
	if (NewSelection)
	{
		(*CurrentSelection) = *NewSelection;
	}
	else
	{
		(*CurrentSelection) = UE::Geometry::FUVEditorDynamicMeshSelection();
	}

	EdgeSewCandidates.Reset();

	if (SelectionTargetIndex != -1 && CurrentSelection && CurrentSelection->Type == FUVEditorDynamicMeshSelection::EType::Edge)
	{
		// TODO(Performance) This loop is very slow for large selections
		TRACE_CPUPROFILER_EVENT_SCOPE(FindSewEdgeOppositePairing_Loop);

		for (int32 Eid : CurrentSelection->SelectedIDs)
		{
			FIndex2i SewPairCandidate(Eid, FindSewEdgeOppositePairing(Eid));
			if (SewPairCandidate[1] != IndexConstants::InvalidID)
			{
				if (SewPairCandidate[0] > SewPairCandidate[1])
				{
					SewPairCandidate.Swap();
				}

				FEdgePair EdgePair;
				EdgePair.A = CurrentSelection->Mesh->GetEdgeV(SewPairCandidate[0]);
				EdgePair.B = CurrentSelection->Mesh->GetEdgeV(SewPairCandidate[1]);

				EdgeSewCandidates.Add(EdgePair);
			}
		}
	}

	UpdateVisualizations();
}

void UUVSeamSewAction::UpdateVisualizations()
{
	UpdateSewEdgePreviewLines();
}

void UUVSeamSewAction::UpdateSewEdgePreviewLines()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSeamSewAction_UpdateSewEdgePreviewLines);
	
	SewEdgePairingLineSet->Clear();
	if (CurrentSelection && !CurrentSelection->IsEmpty())
	{
		FTransform MeshTransform = Targets[SelectionTargetIndex]->UnwrapPreview->PreviewMesh->GetTransform();

		const FDynamicMesh3* UnwrapMesh = Targets[SelectionTargetIndex]->UnwrapPreview->PreviewMesh->GetMesh();
		for (FEdgePair SewPair : EdgeSewCandidates)
		{
			
			FVector3d Vert1, Vert2;
			Vert1 = UnwrapMesh->GetVertex(SewPair.A[0]);
			Vert2 = UnwrapMesh->GetVertex(SewPair.A[1]);
			SewEdgePairingLineSet->AddLine(
				MeshTransform.TransformPosition(Vert1),
				MeshTransform.TransformPosition(Vert2),
				FUVEditorUXSettings::SewSideLeftColor, FUVEditorUXSettings::SewLineHighlightThickness, FUVEditorUXSettings::SewLineDepthOffset);

			Vert1 = UnwrapMesh->GetVertex(SewPair.B[0]);
			Vert2 = UnwrapMesh->GetVertex(SewPair.B[1]);
			SewEdgePairingLineSet->AddLine(
				MeshTransform.TransformPosition(Vert1),
				MeshTransform.TransformPosition(Vert2),
				FUVEditorUXSettings::SewSideRightColor, FUVEditorUXSettings::SewLineHighlightThickness, FUVEditorUXSettings::SewLineDepthOffset);

		}
	}
}


bool UUVSeamSewAction::PreCheckAction()
{
	if (SelectionTargetIndex == -1 || !CurrentSelection || CurrentSelection->Mesh == nullptr)
	{
		ParentTool->GetToolManager()->DisplayMessage(
			LOCTEXT("SewErrorSelectionEmpty", "Cannot sew UVs. Mesh selection was empty."),
			EToolMessageLevel::UserWarning);
		return false;
	}

	if (CurrentSelection->Type != FUVEditorDynamicMeshSelection::EType::Edge)
	{
		ParentTool->GetToolManager()->DisplayMessage(
			LOCTEXT("SewErrorSelectionNotEdge", "Cannot sew UVs. Selection was not an edge."),
			EToolMessageLevel::UserWarning);
		return false;
	}

	if (EdgeSewCandidates.Num() == 0)
	{
		ParentTool->GetToolManager()->DisplayMessage(
			LOCTEXT("SewErrorSelectionNotBoundary", "Cannot sew UVs. No viable sew candidate edges selected."),
			EToolMessageLevel::UserWarning);
		return false;
	}

	return true;
}


int32 UUVSeamSewAction::FindSewEdgeOppositePairing(int32 UnwrapEid) const
{
	// Given an edge id on the unwrap mesh, determine it's opposite edge suitable for UV sewing elsewhere on the unwrap mesh

	if (!CurrentSelection->Mesh->IsBoundaryEdge(UnwrapEid))
	{
		return IndexConstants::InvalidID;
	}
	int32 UVLayerIndex = Targets[SelectionTargetIndex]->UVLayerIndex;
	FDynamicMeshUVOverlay* UVOverlay = Targets[SelectionTargetIndex]->AppliedCanonical.Get()->Attributes()->GetUVLayer(UVLayerIndex);
	UE::Geometry::FDynamicMesh3::FEdge UnwrapEdge = CurrentSelection->Mesh->GetEdge(UnwrapEid);
	checkSlow(UnwrapEdge.Tri[1] == -1); // As a boundary edge, the second triangle should always be invalid.
	int32 ParentVid_0 = UVOverlay->GetParentVertex(UnwrapEdge.Vert[0]);
	int32 ParentVid_1 = UVOverlay->GetParentVertex(UnwrapEdge.Vert[1]);

	int32 ParentEid = Targets[SelectionTargetIndex]->AppliedCanonical->FindEdgeFromTri(ParentVid_0, ParentVid_1, UnwrapEdge.Tri[0]);
	FIndex2i AppliedEdgeTids = Targets[SelectionTargetIndex]->AppliedCanonical->GetEdgeT(ParentEid);

	int32 OppositeTid = UnwrapEdge.Tri[0] == AppliedEdgeTids[0] ? AppliedEdgeTids[1] : AppliedEdgeTids[0];
	if (OppositeTid == -1) 
	{
		// This could happen if a boundary edge in the unwrap mesh is also a boundary edge in the applied, i.e. the applied mesh isn't closed.
		return IndexConstants::InvalidID;
	}
	if (!CurrentSelection->Mesh->IsTriangle(OppositeTid))
	{
		// Return invalid if the opposite triangle isn't set in the unwtap mesh, i.e. the overlay has incomplete UVs
		return IndexConstants::InvalidID;
	}

	FIndex3i UnwrapOppositeEids = CurrentSelection->Mesh->GetTriEdges(OppositeTid);
	for (int32 i = 0; i < 3; ++i)
	{
		FIndex2i UnwrapOppositeVids = CurrentSelection->Mesh->GetEdgeV(UnwrapOppositeEids[i]);
		if (!ensure(UVOverlay->IsElement(UnwrapOppositeVids[0]) && UVOverlay->IsElement(UnwrapOppositeVids[1]))) {			
			continue; // Skip in case any elements aren't properly set in the overlay.
			          //We probably shouldn't reach this if the triangle check above passed.
		}
		int32 OppositeParentVid_0 = UVOverlay->GetParentVertex(UnwrapOppositeVids[0]);
		int32 OppositeParentVid_1 = UVOverlay->GetParentVertex(UnwrapOppositeVids[1]);

		if ((OppositeParentVid_0 == ParentVid_0 && OppositeParentVid_1 == ParentVid_1) ||
			(OppositeParentVid_0 == ParentVid_1 && OppositeParentVid_1 == ParentVid_0)) {
			return UnwrapOppositeEids[i];
		}
	}

	// If we can't find anything... return an invalid result.
	return IndexConstants::InvalidID;

}


bool UUVSeamSewAction::ApplyAction(UUVToolEmitChangeAPI& EmitChangeAPI)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSeamSewAction_ApplyAction);

	UUVEditorToolMeshInput* Target = Targets[SelectionTargetIndex];
	FDynamicMesh3& MeshToSew = *(Target->UnwrapCanonical);

	TArray<int32> SelectedTids;
	TArray<FIndex2i> ResolvedEdgePairs;
	for (FEdgePair SewPair : EdgeSewCandidates)
	{
		ResolvedEdgePairs.Add(FIndex2i(MeshToSew.FindEdge(SewPair.A[0], SewPair.A[1]),
			                           MeshToSew.FindEdge(SewPair.B[0], SewPair.B[1])));

		for (int32 VertexIndex = 0; VertexIndex < 2; ++VertexIndex)
		{
			TArray<int32> Tids;
			MeshToSew.GetVtxTriangles(SewPair.A[VertexIndex], Tids);
			SelectedTids.Append(Tids);
		}			
		
		for (int32 VertexIndex = 0; VertexIndex < 2; ++VertexIndex)
		{
			TArray<int32> Tids;
			MeshToSew.GetVtxTriangles(SewPair.B[VertexIndex], Tids);
			SelectedTids.Append(Tids);
		}
	}
	SelectedTids.Sort();
	SelectedTids.SetNum(Algo::Unique(SelectedTids));

	FDynamicMeshChangeTracker ChangeTracker(Target->UnwrapCanonical.Get());
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(SelectedTids, true);

	EdgeSewCandidates.Sort();
	EdgeSewCandidates.SetNum(Algo::Unique(EdgeSewCandidates));

	TArray<FDynamicMesh3::FMergeEdgesInfo> AllMergeInfo;
	
	// Note that currently, we don't really need to gather up the kept verts and call them out for an
	// update, since no vert locations should have changed. But we'll do it anyway in case any of the
	// code changes in some way that moves the remaining verts (for instance, to some halfway point).
	TSet<int32> KeptVidsSet;

	for (FIndex2i EdgePair : ResolvedEdgePairs)
	{
		FDynamicMesh3::FMergeEdgesInfo MergeInfo;
		EMeshResult Result = MeshToSew.MergeEdges(EdgePair[0], EdgePair[1], MergeInfo, false);
		if (Result == EMeshResult::Ok)
		{
			for (int i = 0; i < 2; ++i)
			{
				KeptVidsSet.Add(MergeInfo.KeptVerts[i]);
			}
		}
		else
		{
			UE_LOG(LogGeometry, Warning, TEXT("Failed to sew edge pair %d / %d. Failed with code %d"), EdgePair[0], EdgePair[1], Result);
		}
	}
	checkSlow(MeshToSew.CheckValidity(FDynamicMesh3::FValidityOptions(true, true))); // Allow nonmanifold verts and reverse orientation

	TArray<int32> RemainingVids;
	for (int32 KeptVid : KeptVidsSet)
	{
		// We have to do this check because we may have performed multiple merge actions, so a "kept vert"
		// from one action may have ended up getting removed in a later one.
		if (MeshToSew.IsVertex(KeptVid))
		{
			RemainingVids.Add(KeptVid);
		}
	}

	// Our selection is no longer valid, and we should clear it now before the broadcasts from the upcoming
	// canonical updates ask us to rebuild our visualization.
	SetSelection(-1, nullptr);

	Target->UpdateUnwrapCanonicalOverlayFromPositions(&RemainingVids, &SelectedTids);
	Target->UpdateAllFromUnwrapCanonical(&RemainingVids, &SelectedTids, &SelectedTids);
	checkSlow(MeshToSew.IsSameAs(*Target->UnwrapPreview->PreviewMesh->GetMesh(), FDynamicMesh3::FSameAsOptions()));

	const FText TransactionName(LOCTEXT("SewCompleteTransactionName", "Sew Edges"));
	EmitChangeAPI.EmitToolIndependentUnwrapCanonicalChange(Target,
		ChangeTracker.EndChange(), TransactionName);

	return true;
}

#undef LOCTEXT_NAMESPACE