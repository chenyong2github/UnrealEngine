// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/UVToolSelectionHighlightMechanic.h"

#include "Actions/UVSeamSewAction.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PointSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Drawing/TriangleSetComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "MeshOpPreviewHelpers.h" // for the preview meshes
#include "Selection/UVToolSelection.h"
#include "ToolSetupUtil.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "UVEditorUXSettings.h"

#define LOCTEXT_NAMESPACE "UUVToolSelectionHighlightMechanic"

using namespace UE::Geometry;

namespace UVToolSelectionHighlightMechanicLocals
{
}

void UUVToolSelectionHighlightMechanic::Initialize(UWorld* UnwrapWorld, UWorld* LivePreviewWorld)
{
	// Initialize shouldn't be called more than once...
	if (!ensure(!UnwrapGeometryActor))
	{
		UnwrapGeometryActor->Destroy();
	}
	if (!ensure(!LivePreviewGeometryActor))
	{
		LivePreviewGeometryActor->Destroy();
	}

	// Owns most of the unwrap geometry except for the unselected paired edges, since we don't
	// want those to move if we change the actor transform via SetUnwrapHighlightTransform
	UnwrapGeometryActor = UnwrapWorld->SpawnActor<APreviewGeometryActor>(
		FVector::ZeroVector, FRotator(0, 0, 0), FActorSpawnParameters());

	UnwrapTriangleSet = NewObject<UTriangleSetComponent>(UnwrapGeometryActor);
	// We are setting the TranslucencySortPriority here to handle the UV editor's use case in 2D
	// where multiple translucent layers are drawn on top of each other but still need depth sorting.
	UnwrapTriangleSet->TranslucencySortPriority = FUVEditorUXSettings::SelectionTriangleDepthBias;
	TriangleSetMaterial = ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(GetParentTool()->GetToolManager(),
		FUVEditorUXSettings::SelectionTriangleFillColor,
		FUVEditorUXSettings::SelectionTriangleDepthBias,
		FUVEditorUXSettings::SelectionTriangleOpacity);
	UnwrapGeometryActor->SetRootComponent(UnwrapTriangleSet.Get());
	UnwrapTriangleSet->RegisterComponent();

	UnwrapLineSet = NewObject<ULineSetComponent>(UnwrapGeometryActor);
	UnwrapLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetParentTool()->GetToolManager(), true));
	UnwrapLineSet->AttachToComponent(UnwrapTriangleSet.Get(), FAttachmentTransformRules::KeepWorldTransform);
	UnwrapLineSet->RegisterComponent();

	UnwrapPairedEdgeLineSet = NewObject<ULineSetComponent>(UnwrapGeometryActor);
	UnwrapPairedEdgeLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetParentTool()->GetToolManager(), true));
	UnwrapPairedEdgeLineSet->AttachToComponent(UnwrapTriangleSet.Get(), FAttachmentTransformRules::KeepWorldTransform);
	UnwrapPairedEdgeLineSet->RegisterComponent();

	SewEdgePairingLineSet = NewObject<ULineSetComponent>(UnwrapGeometryActor);
	SewEdgePairingLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));
	SewEdgePairingLineSet->AttachToComponent(UnwrapTriangleSet.Get(), FAttachmentTransformRules::KeepWorldTransform);
	SewEdgePairingLineSet->RegisterComponent();
	SewEdgePairingLineSet->SetVisibility(bPairedEdgeHighlightsEnabled);

	// The unselected paired edges get their own, stationary, actor.
	UnwrapStationaryGeometryActor = UnwrapWorld->SpawnActor<APreviewGeometryActor>(
		FVector::ZeroVector, FRotator(0, 0, 0), FActorSpawnParameters());
	SewEdgeUnselectedPairingLineSet = NewObject<ULineSetComponent>(UnwrapStationaryGeometryActor);
	SewEdgeUnselectedPairingLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));
	UnwrapStationaryGeometryActor->SetRootComponent(SewEdgeUnselectedPairingLineSet.Get());
	SewEdgeUnselectedPairingLineSet->RegisterComponent();
	SewEdgeUnselectedPairingLineSet->SetVisibility(bPairedEdgeHighlightsEnabled);

	UnwrapPointSet = NewObject<UPointSetComponent>(UnwrapGeometryActor);
	UnwrapPointSet->SetPointMaterial(ToolSetupUtil::GetDefaultPointComponentMaterial(GetParentTool()->GetToolManager(), true));
	UnwrapPointSet->AttachToComponent(UnwrapTriangleSet.Get(), FAttachmentTransformRules::KeepWorldTransform);
	UnwrapPointSet->RegisterComponent();

	// Owns the highlights in the live preview.
	LivePreviewGeometryActor = LivePreviewWorld->SpawnActor<APreviewGeometryActor>(
		FVector::ZeroVector, FRotator(0, 0, 0), FActorSpawnParameters());

	LivePreviewLineSet = NewObject<ULineSetComponent>(LivePreviewGeometryActor);
	LivePreviewLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));
	LivePreviewGeometryActor->SetRootComponent(LivePreviewLineSet.Get());
	LivePreviewLineSet->RegisterComponent();

	LivePreviewPointSet = NewObject<UPointSetComponent>(LivePreviewGeometryActor);
	LivePreviewPointSet->SetPointMaterial(ToolSetupUtil::GetDefaultPointComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));
	LivePreviewPointSet->AttachToComponent(LivePreviewLineSet.Get(), FAttachmentTransformRules::KeepWorldTransform);
	LivePreviewPointSet->RegisterComponent();
}

void UUVToolSelectionHighlightMechanic::Shutdown()
{
	if (UnwrapGeometryActor)
	{
		UnwrapGeometryActor->Destroy();
		UnwrapGeometryActor = nullptr;
	}
	if (UnwrapStationaryGeometryActor)
	{
		UnwrapStationaryGeometryActor->Destroy();
		UnwrapStationaryGeometryActor = nullptr;
	}
	if (LivePreviewGeometryActor)
	{
		LivePreviewGeometryActor->Destroy();
		LivePreviewGeometryActor = nullptr;
	}

	TriangleSetMaterial = nullptr;
}

void UUVToolSelectionHighlightMechanic::SetIsVisible(bool bUnwrapHighlightVisible, bool bLivePreviewHighlightVisible)
{
	if (UnwrapGeometryActor)
	{
		UnwrapGeometryActor->SetActorHiddenInGame(!bUnwrapHighlightVisible);
	}
	if (UnwrapStationaryGeometryActor)
	{
		UnwrapStationaryGeometryActor->SetActorHiddenInGame(!bUnwrapHighlightVisible);
	}
	if (LivePreviewGeometryActor)
	{
		LivePreviewGeometryActor->SetActorHiddenInGame(!bLivePreviewHighlightVisible);
	}
}

void UUVToolSelectionHighlightMechanic::RebuildUnwrapHighlight(
	const TArray<FUVToolSelection>& Selections, const FTransform& StartTransform, 
	bool bUsePreviews)
{
	if (!ensure(UnwrapGeometryActor))
	{
		return;
	}

	UnwrapTriangleSet->Clear();
	UnwrapLineSet->Clear();
	UnwrapPointSet->Clear();
	SewEdgePairingLineSet->Clear();
	SewEdgeUnselectedPairingLineSet->Clear();
	StaticPairedEdgeVidsPerMesh.Reset();

	UnwrapGeometryActor->SetActorTransform(StartTransform);

	for (const FUVToolSelection& Selection : Selections)
	{
		if (!ensure(Selection.Target.IsValid() && Selection.Target->IsValid()))
		{
			return;
		}

		const FDynamicMesh3& Mesh = bUsePreviews ? *Selection.Target->UnwrapPreview->PreviewMesh->GetMesh()
			: *Selection.Target->UnwrapCanonical;

		if (Selection.Type == FUVToolSelection::EType::Triangle)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UUVToolSelectionHighlightMechanic::AppendUnwrapHighlight_Triangle);

			UnwrapTriangleSet->ReserveTriangles(Selection.SelectedIDs.Num());
			UnwrapLineSet->ReserveLines(Selection.SelectedIDs.Num() * 3);
			for (int32 Tid : Selection.SelectedIDs)
			{
				if (!ensure(Mesh.IsTriangle(Tid)))
				{
					continue;
				}

				FIndex3i Vids = Mesh.GetTriangle(Tid);
				FVector Points[3];
				for (int i = 0; i < 3; ++i)
				{
					Points[i] = StartTransform.InverseTransformPosition(Mesh.GetVertex(Vids[i]));
				}
				UnwrapTriangleSet->AddTriangle(Points[0], Points[1], Points[2], FVector(0, 0, 1), FUVEditorUXSettings::SelectionTriangleFillColor, TriangleSetMaterial);
				for (int i = 0; i < 3; ++i)
				{
					int NextIndex = (i + 1) % 3;
					UnwrapLineSet->AddLine(Points[i], Points[NextIndex],
						FUVEditorUXSettings::SelectionTriangleWireframeColor,
						FUVEditorUXSettings::SelectionLineThickness,
						FUVEditorUXSettings::SelectionWireframeDepthBias);
				}
			}
		}
		else if (Selection.Type == FUVToolSelection::EType::Edge)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_RebuildDrawnElements_Edge);

			StaticPairedEdgeVidsPerMesh.Emplace();
			StaticPairedEdgeVidsPerMesh.Last().Key = Selection.Target;

			const FDynamicMesh3& AppliedMesh = bUsePreviews ? *Selection.Target->AppliedPreview->PreviewMesh->GetMesh()
				: *Selection.Target->AppliedCanonical;

			UnwrapLineSet->ReserveLines(Selection.SelectedIDs.Num());
			for (int32 Eid : Selection.SelectedIDs)
			{
				if (!ensure(Mesh.IsEdge(Eid)))
				{
					continue;
				}

				FIndex2i EdgeVids = Mesh.GetEdgeV(Eid);
				UnwrapLineSet->AddLine(
					StartTransform.InverseTransformPosition(Mesh.GetVertex(EdgeVids.A)),
					StartTransform.InverseTransformPosition(Mesh.GetVertex(EdgeVids.B)),
					FUVEditorUXSettings::SelectionTriangleWireframeColor,
					FUVEditorUXSettings::SelectionLineThickness,
					FUVEditorUXSettings::SelectionWireframeDepthBias);

				if (bPairedEdgeHighlightsEnabled)
				{
					bool bWouldPreferReverse = false;
					int32 PairedEid = UUVSeamSewAction::FindSewEdgeOppositePairing(
						Mesh, AppliedMesh, Selection.Target->UVLayerIndex, Eid, bWouldPreferReverse);

					bool bPairedEdgeIsSelected = Selection.SelectedIDs.Contains(PairedEid);

					if (PairedEid == IndexConstants::InvalidID
						// When both sides are selected, merge order depends on adjacent tid values, so
						// deal with the pair starting with the other edge.
						|| (bPairedEdgeIsSelected && bWouldPreferReverse))
					{
						continue;
					}

					FIndex2i Vids1 = Mesh.GetEdgeV(Eid);
					SewEdgePairingLineSet->AddLine(
						StartTransform.InverseTransformPosition(Mesh.GetVertex(Vids1.A)),
						StartTransform.InverseTransformPosition(Mesh.GetVertex(Vids1.B)),
						FUVEditorUXSettings::SewSideLeftColor, FUVEditorUXSettings::SewLineHighlightThickness, FUVEditorUXSettings::SewLineDepthOffset);

					// The paired edge may need to go into a separate line set if it is not selected so that it does
					// not get affected by transformations of the selected highlights in SetUnwrapHighlightTransform
					FIndex2i Vids2 = Mesh.GetEdgeV(PairedEid);
					if (bPairedEdgeIsSelected)
					{
						SewEdgePairingLineSet->AddLine(
							StartTransform.InverseTransformPosition(Mesh.GetVertex(Vids2.A)),
							StartTransform.InverseTransformPosition(Mesh.GetVertex(Vids2.B)),
							FUVEditorUXSettings::SewSideRightColor, FUVEditorUXSettings::SewLineHighlightThickness, 
							FUVEditorUXSettings::SewLineDepthOffset);
					}
					else
					{
						StaticPairedEdgeVidsPerMesh.Last().Value.Add(TPair<int32, int32>(Vids2.A, Vids2.B));
						SewEdgeUnselectedPairingLineSet->AddLine(
							Mesh.GetVertex(Vids2.A),
							Mesh.GetVertex(Vids2.B),
							FUVEditorUXSettings::SewSideRightColor, FUVEditorUXSettings::SewLineHighlightThickness,
							FUVEditorUXSettings::SewLineDepthOffset);
					}
				}//end if visualizing paired edges
			}//end for each edge
		}
		else if (Selection.Type == FUVToolSelection::EType::Vertex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_RebuildDrawnElements_Vertex);

			UnwrapPointSet->ReservePoints(Selection.SelectedIDs.Num());
			for (int32 Vid : Selection.SelectedIDs)
			{
				if (!ensure(Mesh.IsVertex(Vid)))
				{
					continue;
				}

				FRenderablePoint PointToRender(StartTransform.InverseTransformPosition(Mesh.GetVertex(Vid)),
					FUVEditorUXSettings::SelectionTriangleWireframeColor,
					FUVEditorUXSettings::SelectionPointThickness,
					FUVEditorUXSettings::SelectionWireframeDepthBias);
				UnwrapPointSet->AddPoint(PointToRender);
			}
		}
	}
}

void UUVToolSelectionHighlightMechanic::SetUnwrapHighlightTransform(const FTransform& Transform, 
	bool bRebuildStaticPairedEdges, bool bUsePreviews)
{
	if (ensure(UnwrapGeometryActor))
	{
		UnwrapGeometryActor->SetActorTransform(Transform);
	}
	if (bPairedEdgeHighlightsEnabled && bRebuildStaticPairedEdges)
	{
		SewEdgeUnselectedPairingLineSet->Clear();
		for (const TPair<TWeakObjectPtr<UUVEditorToolMeshInput>, 
			TArray<TPair<int32, int32>>>& MeshVidPairs : StaticPairedEdgeVidsPerMesh)
		{
			TWeakObjectPtr<UUVEditorToolMeshInput> Target = MeshVidPairs.Key;
			if (!ensure(Target.IsValid()))
			{
				continue;
			}

			const FDynamicMesh3& Mesh = bUsePreviews ? *Target->UnwrapPreview->PreviewMesh->GetMesh()
				: *Target->UnwrapCanonical;

			for (const TPair<int32, int32>& VidPair : MeshVidPairs.Value)
			{
				if (!ensure(Mesh.IsVertex(VidPair.Key) && Mesh.IsVertex(VidPair.Value)))
				{
					continue;
				}
				SewEdgeUnselectedPairingLineSet->AddLine(
					Mesh.GetVertex(VidPair.Key),
					Mesh.GetVertex(VidPair.Value),
					FUVEditorUXSettings::SewSideRightColor, FUVEditorUXSettings::SewLineHighlightThickness,
					FUVEditorUXSettings::SewLineDepthOffset);
			}
		}
	}
}

FTransform UUVToolSelectionHighlightMechanic::GetUnwrapHighlightTransform()
{
	if (ensure(UnwrapGeometryActor))
	{
		return UnwrapGeometryActor->GetActorTransform();
	}
	return FTransform::Identity;
}

void UUVToolSelectionHighlightMechanic::RebuildAppliedHighlightFromUnwrapSelection(
	const TArray<FUVToolSelection>& UnwrapSelections, bool bUsePreviews)
{
	if (!ensure(LivePreviewGeometryActor))
	{
		return;
	}

	LivePreviewLineSet->Clear();
	LivePreviewPointSet->Clear();

	for (const FUVToolSelection& Selection : UnwrapSelections)
	{
		if (!ensure(Selection.Target.IsValid() && Selection.Target->IsValid()))
		{
			return;
		}

		UUVEditorToolMeshInput* Target = Selection.Target.Get();

		const FDynamicMesh3& AppliedMesh = bUsePreviews ? *Target->AppliedPreview->PreviewMesh->GetMesh()
			: *Target->AppliedCanonical;
		const FDynamicMesh3& UnwrapMesh = bUsePreviews ? *Target->UnwrapPreview->PreviewMesh->GetMesh()
			: *Target->UnwrapCanonical;

		FTransform MeshTransform = Target->AppliedPreview->PreviewMesh->GetTransform();

		auto AppendEdgeLine = [this, &AppliedMesh, &MeshTransform](int32 AppliedEid)
		{
			FVector3d Vert1, Vert2;
			AppliedMesh.GetEdgeV(AppliedEid, Vert1, Vert2);

			LivePreviewLineSet->AddLine(
				MeshTransform.TransformPosition(Vert1),
				MeshTransform.TransformPosition(Vert2),
				FUVEditorUXSettings::SelectionTriangleWireframeColor,
				FUVEditorUXSettings::LivePreviewHighlightThickness,
				FUVEditorUXSettings::LivePreviewHighlightDepthOffset);
		};

		if (Selection.Type == FUVToolSelection::EType::Triangle)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Triangle);

			for (int32 Tid : Selection.SelectedIDs)
			{
				if (!ensure(AppliedMesh.IsTriangle(Tid)))
				{
					continue;
				}

				// Gather the boundary edges for the live preview
				FIndex3i TriEids = AppliedMesh.GetTriEdges(Tid);
				for (int i = 0; i < 3; ++i)
				{
					FIndex2i EdgeTids = AppliedMesh.GetEdgeT(TriEids[i]);
					for (int j = 0; j < 2; ++j)
					{
						if (EdgeTids[j] != Tid && !Selection.SelectedIDs.Contains(EdgeTids[j]))
						{
							AppendEdgeLine(TriEids[i]);
							break;
						}
					}
				}//end for tri edges
			}//end for selection tids
		}
		else if (Selection.Type == FUVToolSelection::EType::Edge)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Edge);

			for (int32 UnwrapEid : Selection.SelectedIDs)
			{
				if (!ensure(UnwrapMesh.IsEdge(UnwrapEid)))
				{
					continue;
				}

				FDynamicMesh3::FEdge Edge = UnwrapMesh.GetEdge(UnwrapEid);

				int32 AppliedEid = AppliedMesh.FindEdgeFromTri(
					Target->UnwrapVidToAppliedVid(Edge.Vert.A),
					Target->UnwrapVidToAppliedVid(Edge.Vert.B),
					Edge.Tri.A);

				AppendEdgeLine(AppliedEid);
			}
		}
		else if (Selection.Type == FUVToolSelection::EType::Vertex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Vertex);

			for (int32 UnwrapVid : Selection.SelectedIDs)
			{
				FVector3d Position = AppliedMesh.GetVertex(
					Target->UnwrapVidToAppliedVid(UnwrapVid));

				LivePreviewPointSet->AddPoint(Position,
					FUVEditorUXSettings::SelectionTriangleWireframeColor,
					FUVEditorUXSettings::LivePreviewHighlightPointSize,
					FUVEditorUXSettings::LivePreviewHighlightDepthOffset);
			}
		}
	}//end for selection
}

void UUVToolSelectionHighlightMechanic::AppendAppliedHighlight(const TArray<FUVToolSelection>& AppliedSelections, bool bUsePreviews)
{
	if (!ensure(LivePreviewGeometryActor))
	{
		return;
	}

	for (const FUVToolSelection& Selection : AppliedSelections)
	{
		if (!ensure(Selection.Target.IsValid() && Selection.Target->IsValid()))
		{
			return;
		}

		UUVEditorToolMeshInput* Target = Selection.Target.Get();

		const FDynamicMesh3& AppliedMesh = bUsePreviews ? *Target->AppliedPreview->PreviewMesh->GetMesh()
			: *Target->AppliedCanonical;

		FTransform MeshTransform = Target->AppliedPreview->PreviewMesh->GetTransform();

		auto AppendEdgeLine = [this, &AppliedMesh, &MeshTransform](int32 AppliedEid)
		{
			FVector3d Vert1, Vert2;
			AppliedMesh.GetEdgeV(AppliedEid, Vert1, Vert2);

			LivePreviewLineSet->AddLine(
				MeshTransform.TransformPosition(Vert1),
				MeshTransform.TransformPosition(Vert2),
				FUVEditorUXSettings::SelectionTriangleWireframeColor,
				FUVEditorUXSettings::LivePreviewHighlightThickness,
				FUVEditorUXSettings::LivePreviewHighlightDepthOffset);
		};

		if (Selection.Type == FUVToolSelection::EType::Triangle)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Triangle);

			for (int32 Tid : Selection.SelectedIDs)
			{
				if (!ensure(AppliedMesh.IsTriangle(Tid)))
				{
					continue;
				}

				// Gather the boundary edges for the live preview
				FIndex3i TriEids = AppliedMesh.GetTriEdges(Tid);
				for (int i = 0; i < 3; ++i)
				{
					FIndex2i EdgeTids = AppliedMesh.GetEdgeT(TriEids[i]);
					for (int j = 0; j < 2; ++j)
					{
						if (EdgeTids[j] != Tid && !Selection.SelectedIDs.Contains(EdgeTids[j]))
						{
							AppendEdgeLine(TriEids[i]);
							break;
						}
					}
				}//end for tri edges
			}//end for selection tids
		}
		else if (Selection.Type == FUVToolSelection::EType::Edge)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Edge);

			for (int32 Eid : Selection.SelectedIDs)
			{
				if (!ensure(AppliedMesh.IsEdge(Eid)))
				{
					continue;
				}

				AppendEdgeLine(Eid);
			}
		}
		else if (Selection.Type == FUVToolSelection::EType::Vertex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Vertex);

			for (int32 Vid : Selection.SelectedIDs)
			{
				FVector3d Position = AppliedMesh.GetVertex(Vid);

				LivePreviewPointSet->AddPoint(Position,
					FUVEditorUXSettings::SelectionTriangleWireframeColor,
					FUVEditorUXSettings::LivePreviewHighlightPointSize,
					FUVEditorUXSettings::LivePreviewHighlightDepthOffset);
			}
		}
	}//end for selection
}


void UUVToolSelectionHighlightMechanic::SetEnablePairedEdgeHighlights(bool bEnable)
{
	bPairedEdgeHighlightsEnabled = bEnable;
	SewEdgePairingLineSet->SetVisibility(bEnable);
	SewEdgeUnselectedPairingLineSet->SetVisibility(bEnable);
}

#undef LOCTEXT_NAMESPACE