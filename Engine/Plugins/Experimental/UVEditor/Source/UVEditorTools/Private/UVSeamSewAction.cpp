// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVSeamSewAction.h"

#include "Algo/Unique.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "Selection/DynamicMeshSelection.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "PreviewMesh.h"
#include "ToolSetupUtil.h"
#include "UVToolContextObjects.h"

#define LOCTEXT_NAMESPACE "UUVSeamSewAction"

using namespace UE::Geometry;


UUVSeamSewAction::UUVSeamSewAction()
{
	CurrentSelection = MakeShared<UE::Geometry::FDynamicMeshSelection>();
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


void UUVSeamSewAction::SetSelection(int32 SelectionTargetIndexIn, const UE::Geometry::FDynamicMeshSelection* NewSelection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSeamSewAction_SetSelection);
	
	SelectionTargetIndex = SelectionTargetIndexIn;
	if (NewSelection)
	{
		(*CurrentSelection) = *NewSelection;
	}
	else
	{
		(*CurrentSelection) = UE::Geometry::FDynamicMeshSelection();
	}

	EdgeSewCandidates.Reset();

	if (SelectionTargetIndex != -1 && CurrentSelection && CurrentSelection->Type == FDynamicMeshSelection::EType::Edge)
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
				FColor::Red, 5, 3);

			Vert1 = UnwrapMesh->GetVertex(SewPair.B[0]);
			Vert2 = UnwrapMesh->GetVertex(SewPair.B[1]);
			SewEdgePairingLineSet->AddLine(
				MeshTransform.TransformPosition(Vert1),
				MeshTransform.TransformPosition(Vert2),
				FColor::Green, 5, 3);

		}
	}
}


bool UUVSeamSewAction::PreCheckAction()
{
	if (SelectionTargetIndex == -1 || !CurrentSelection || CurrentSelection->Mesh == nullptr)
	{
		UE_LOG(LogGeometry, Warning, TEXT("Cannot sew UVs. Mesh selection was empty."));
		return false;
	}

	if (CurrentSelection->Type != FDynamicMeshSelection::EType::Edge)
	{
		UE_LOG(LogGeometry, Warning, TEXT("Cannot sew UVs. Selection was not an edge."));
		return false;
	}

	if (EdgeSewCandidates.Num() == 0)
	{
		UE_LOG(LogGeometry, Warning, TEXT("Cannot sew UVs. No viable sew candidate edges selected."));
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
	FIndex2i EdgeVids = CurrentSelection->Mesh->GetEdgeV(UnwrapEid);
	int32 ParentVid_0 = UVOverlay->GetParentVertex(EdgeVids[0]);
	int32 ParentVid_1 = UVOverlay->GetParentVertex(EdgeVids[1]);

	TArray<int32> Vid0Elements;
	TArray<int32> Vid1Elements;
	UVOverlay->GetVertexElements(ParentVid_0, Vid0Elements);
	UVOverlay->GetVertexElements(ParentVid_1, Vid1Elements);

	// Now that we know our potential elements, locate one single edge that is a viable pairing, or return InvalidID if there are none or more.
	int32 OppositeEid = IndexConstants::InvalidID;
	for (int32 Vid0 : Vid0Elements)
	{
		for (int32 Vid1 : Vid1Elements)
		{
			int32 EidCandidate = CurrentSelection->Mesh->FindEdge(Vid0, Vid1);
			if (EidCandidate == UnwrapEid || EidCandidate == IndexConstants::InvalidID)
			{
				continue;
			}
			if (OppositeEid == IndexConstants::InvalidID)
			{
				OppositeEid = EidCandidate;
				int32 ParentCandidateVid_0 = UVOverlay->GetParentVertex(Vid0);
				int32 ParentCandidateVid_1 = UVOverlay->GetParentVertex(Vid1);
				check(ParentCandidateVid_0 == ParentVid_0 && ParentCandidateVid_1 == ParentVid_1)
			}
			else
			{
				UE_LOG(LogGeometry, Warning, TEXT("Selected edge is unsuitable for sewing - there are non-manifold candidate edges."));
				return IndexConstants::InvalidID;
			}
		}
	}

	return OppositeEid;
}


bool UUVSeamSewAction::ApplyAction(UUVToolEmitChangeAPI& EmitChangeAPI)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSeamSewAction_ApplyAction);

	FDynamicMesh3& MeshToSew = *(Targets[SelectionTargetIndex]->UnwrapCanonical);

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

	FDynamicMeshChangeTracker ChangeTracker(Targets[SelectionTargetIndex]->UnwrapCanonical.Get());
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

	Targets[SelectionTargetIndex]->UpdateUnwrapCanonicalOverlayFromPositions(&RemainingVids, &SelectedTids);
	Targets[SelectionTargetIndex]->UpdateAllFromUnwrapCanonical(&RemainingVids, &SelectedTids, &SelectedTids);
	checkSlow(MeshToSew.IsSameAs(*Targets[SelectionTargetIndex]->UnwrapPreview->PreviewMesh->GetMesh(), FDynamicMesh3::FSameAsOptions()));

	const FText TransactionName(LOCTEXT("SewCompleteTransactionName", "Sew Edges"));
	EmitChangeAPI.EmitToolIndependentUnwrapCanonicalChange(Targets[SelectionTargetIndex],
		ChangeTracker.EndChange(), TransactionName);

	SetSelection(-1, nullptr);

	return true;
}

#undef LOCTEXT_NAMESPACE