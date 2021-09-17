// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/UVEditorToolMeshInput.h"

#include "Drawing/MeshElementsVisualizer.h" // for wireframe display
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h" // FDynamicMeshChange
#include "DynamicMesh/MeshIndexUtil.h"
#include "MeshOpPreviewHelpers.h"
#include "ModelingToolTargetUtil.h"
#include "TargetInterfaces/AssetBackedTarget.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "ToolSetupUtil.h"
#include "UVEditorToolUtil.h"

using namespace UE::Geometry;

namespace UVEditorToolMeshInputLocals
{
	void CopyMeshPositions(const UE::Geometry::FDynamicMesh3& MeshIn, UE::Geometry::FDynamicMesh3& MeshOut,
		const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids)
	{
		auto UpdateOrInsertVid = [&MeshIn, &MeshOut](int32 Vid) {
			if (MeshOut.IsVertex(Vid))
			{
				MeshOut.SetVertex(Vid, MeshIn.GetVertex(Vid));
			}
			else
			{
				MeshOut.InsertVertex(Vid, MeshIn.GetVertex(Vid));
			}
		};

		if (!ChangedVids && !ChangedConnectivityTids)
		{
			for (int32 Vid : MeshIn.VertexIndicesItr())
			{
				UpdateOrInsertVid(Vid);
			}
			for (int32 Tid : MeshIn.TriangleIndicesItr())
			{
				MeshOut.SetTriangle(Tid, MeshIn.GetTriangle(Tid));
			}

			return;
		}

		if (ChangedVids)
		{
			for (int32 Vid : *ChangedVids)
			{
				UpdateOrInsertVid(Vid);
			}
		}

		if (ChangedConnectivityTids)
		{
			for (int32 Tid : *ChangedConnectivityTids)
			{
				MeshOut.SetTriangle(Tid, MeshIn.GetTriangle(Tid));
			}
		}
	}

	/**
	 * Copy all or parts of a mesh overlay into another mesh overlay.
	 * 
	 * WARNING: except in narrow cases where we are able to revert to a simple Copy() call, the function
	 * forcibly sets the parent vertices of any elements touched by a SetTriangle() call made in the function.
	 * This is necessary, for example, if you have a two triangle mesh with two UV islands and you want to
	 * swap the assignment of the islands. However it means that the function could silently place the overlay 
	 * in an invalid state if, for instance, ChangedConnectivityTids are not complete (in the previous example,
	 * imagine only one of the triangles is included in ChangedConnectivityTids).
	 * Consider using a checkSlow to check the validity of the output overlay to make sure you did not miss something. 
	 *
	 * @param bMeshesHaveSameTopology If true, underlying meshes are topologically identical, so we can use a
	 *   simple copy when we are not constraining the copied elements/triangles. Can be set true when copying
	 *   between a canonical/preview version of the same mesh, but must be false when copying between an unwrap
	 *   mesh and an applied mesh.
	 */
	void CopyMeshOverlay(const UE::Geometry::FDynamicMeshUVOverlay& OverlayIn, UE::Geometry::FDynamicMeshUVOverlay& OverlayOut, 
		bool bMeshesHaveSameTopology, const TArray<int32>* ChangedElements, const TArray<int32>* ChangedConnectivityTids)
	{
		auto UpdateOrInsertElement = [&OverlayIn, &OverlayOut](int32 ElementID) {
			if (OverlayOut.IsElement(ElementID))
			{
				OverlayOut.SetElement(ElementID, OverlayIn.GetElement(ElementID));
			}
			else
			{
				FVector2f UV = OverlayIn.GetElement(ElementID);
				OverlayOut.InsertElement(ElementID, &UV.X);
			}
		};

		// @param PotentiallyFreedElementsOut If not null, any elements that used to be referenced by
		// the changed triangle are placed here so they can be checked for freeing later.
		auto ResetTriangleWithParenting = [&OverlayIn, &OverlayOut](int32 Tid, TSet<int32>* PotentiallyFreedElementsOut) {
			if (PotentiallyFreedElementsOut)
			{
				FIndex3i OldElementTri = OverlayIn.GetTriangle(Tid);
				for (int i = 0; i < 3; ++i)
				{
					PotentiallyFreedElementsOut->Add(OldElementTri[i]);
				}
			}

			// Force reset the parent pointers if necessary
			FIndex3i NewElementTri = OverlayIn.GetTriangle(Tid);
			FIndex3i ParentTriInOutput = OverlayOut.GetParentMesh()->GetTriangle(Tid);
			for (int32 i = 0; i < 3; ++i)
			{
				if (OverlayOut.GetParentVertex(NewElementTri[i]) != ParentTriInOutput[i])
				{
					OverlayOut.SetParentVertex(NewElementTri[i], ParentTriInOutput[i]);
				}
			}

			// Now set the triangle. Don't free elements since other new triangles might use them.
			OverlayOut.SetTriangle(Tid, NewElementTri, false);
		};

		if (!ChangedElements && !ChangedConnectivityTids)
		{
			if (bMeshesHaveSameTopology)
			{
				OverlayOut.Copy(OverlayIn);
			}
			else
			{
				for (int32 ElementID : OverlayIn.ElementIndicesItr())
				{
					UpdateOrInsertElement(ElementID);
				}
				for (int32 Tid : OverlayIn.GetParentMesh()->TriangleIndicesItr())
				{
					ResetTriangleWithParenting(Tid, nullptr);
				}
				OverlayOut.FreeUnusedElements();
			}
			return;
		}

		if (ChangedElements)
		{
			for (int32 ElementID : *ChangedElements)
			{
				UpdateOrInsertElement(ElementID);
			}
		}
		if (ChangedConnectivityTids)
		{
			TSet<int32> PotentiallyFreedElements;
			for (int32 Tid : *ChangedConnectivityTids)
			{
				ResetTriangleWithParenting(Tid, &PotentiallyFreedElements);
			}

			OverlayOut.FreeUnusedElements(&PotentiallyFreedElements);
		}
	}
}

bool UUVEditorToolMeshInput::IsValid() const
{
	return UnwrapCanonical
		&& UnwrapPreview && UnwrapPreview->IsValidLowLevel()
		&& AppliedCanonical
		&& AppliedPreview && AppliedPreview->IsValidLowLevel()
		&& SourceTarget && SourceTarget->IsValid()
		&& UVLayerIndex >= 0;
}

bool UUVEditorToolMeshInput::InitializeMeshes(UToolTarget* Target, 
	TSharedPtr<FDynamicMesh3> AppliedCanonicalIn, UMeshOpPreviewWithBackgroundCompute* AppliedPreviewIn,
	int32 AssetIDIn, int32 UVLayerIndexIn, UWorld* UnwrapWorld, UWorld* LivePreviewWorld,
	UMaterialInterface* WorkingMaterialIn,
	TFunction<FVector3d(const FVector2f&)> UVToVertPositionFuncIn,
	TFunction<FVector2f(const FVector3d&)> VertPositionToUVFuncIn)
{
	// TODO: The ModelingToolTargetUtil.h doesn't currently have all the proper functions we want
	// to access the tool target (for instance, to get a dynamic mesh without a mesh description).
	// We'll need to update this function once they exist.
	using namespace UE::ToolTarget;

	SourceTarget = Target;
	AssetID = AssetIDIn;
	UVLayerIndex = UVLayerIndexIn;
	UVToVertPosition = UVToVertPositionFuncIn;
	VertPositionToUV = VertPositionToUVFuncIn;

	// We are given the preview- i.e. the mesh with the uv layer applied.
	AppliedCanonical = AppliedCanonicalIn;

	if (!AppliedCanonical->HasAttributes()
		|| UVLayerIndex >= AppliedCanonical->Attributes()->NumUVLayers())
	{
		return false;
	}

	AppliedPreview = AppliedPreviewIn;

	// Set up the unwrapped mesh
	UnwrapCanonical = MakeShared<FDynamicMesh3>();
	UVEditorToolUtil::GenerateUVUnwrapMesh(
		*AppliedCanonical->Attributes()->GetUVLayer(UVLayerIndex),
		*UnwrapCanonical, UVToVertPosition);
	UnwrapCanonical->SetShapeChangeStampEnabled(true);

	// Set up the unwrap preview
	UnwrapPreview = NewObject<UMeshOpPreviewWithBackgroundCompute>();
	UnwrapPreview->Setup(UnwrapWorld);
	UnwrapPreview->PreviewMesh->UpdatePreview(UnwrapCanonical.Get());

	return true;
}


void UUVEditorToolMeshInput::Shutdown()
{
	if (WireframeDisplay)
	{
		WireframeDisplay->Disconnect();
		WireframeDisplay = nullptr;
	}

	UnwrapCanonical = nullptr;
	UnwrapPreview->Shutdown();
	UnwrapPreview = nullptr;
	AppliedCanonical = nullptr;
	// Can't shut down AppliedPreview because it is owned by mode
	AppliedPreview = nullptr;

	SourceTarget = nullptr;
}

void UUVEditorToolMeshInput::UpdateUnwrapPreviewOverlayFromPositions(const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	UnwrapPreview->PreviewMesh->DeferredEditMesh([this, ChangedVids, ChangedConnectivityTids](FDynamicMesh3& Mesh)
	{
		FDynamicMeshUVOverlay* DestOverlay = Mesh.Attributes()->PrimaryUV();
		UVEditorToolUtil::UpdateUVOverlayFromUnwrapMesh(Mesh, *DestOverlay, VertPositionToUV,
			ChangedVids, ChangedConnectivityTids);
	}, false);

	if (FastRenderUpdateTids) {
		UnwrapPreview->PreviewMesh->NotifyRegionDeferredEditCompleted(*FastRenderUpdateTids,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexUVs); // Assuming user changed positions and didn't notify yet
	} 
	else
	{
		UnwrapPreview->PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexUVs, true); // Assuming user changed positions and didn't notify yet
	}

	if (WireframeDisplay)
	{
		WireframeDisplay->NotifyMeshChanged();
	}
}

void UUVEditorToolMeshInput::UpdateUnwrapCanonicalOverlayFromPositions(const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids)
{
	FDynamicMeshUVOverlay* DestOverlay = UnwrapCanonical->Attributes()->PrimaryUV();
	UVEditorToolUtil::UpdateUVOverlayFromUnwrapMesh(*UnwrapCanonical, *DestOverlay, VertPositionToUV,
		ChangedVids, ChangedConnectivityTids);
}

void UUVEditorToolMeshInput::UpdateAppliedPreviewFromUnwrapPreview(const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	using namespace UVEditorToolMeshInputLocals;

	const FDynamicMesh3* SourceUnwrapMesh = UnwrapPreview->PreviewMesh->GetMesh();

	// We need to update the overlay in AppliedPreview. Assuming that the overlay in UnwrapPreview is updated, we can
	// just copy that overlay (using our function that doesn't copy element parent pointers)
	AppliedPreview->PreviewMesh->DeferredEditMesh([this, SourceUnwrapMesh, ChangedVids, ChangedConnectivityTids](FDynamicMesh3& Mesh)
	{
			const FDynamicMeshUVOverlay* SourceOverlay = SourceUnwrapMesh->Attributes()->PrimaryUV();
			FDynamicMeshUVOverlay* DestOverlay = Mesh.Attributes()->GetUVLayer(UVLayerIndex);

			CopyMeshOverlay(*SourceOverlay, *DestOverlay, false, ChangedVids, ChangedConnectivityTids);
	}, false);

	if (FastRenderUpdateTids) {
		AppliedPreview->PreviewMesh->NotifyRegionDeferredEditCompleted(*FastRenderUpdateTids,
			EMeshRenderAttributeFlags::VertexUVs); // Assuming user changed positions and didn't notify yet
	}
	else
	{
		AppliedPreview->PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate,
			EMeshRenderAttributeFlags::VertexUVs, false); // Assuming user changed positions and didn't notify yet
	}
}

void UUVEditorToolMeshInput::UpdateUnwrapPreviewFromAppliedPreview(const TArray<int32>* ChangedElementIDs, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	using namespace UVEditorToolMeshInputLocals;

	// Convert the AppliedPreview UV overlay to positions in UnwrapPreview
	const FDynamicMeshUVOverlay* SourceOverlay = AppliedPreview->PreviewMesh->GetMesh()->Attributes()->GetUVLayer(UVLayerIndex);
	UnwrapPreview->PreviewMesh->DeferredEditMesh([this, SourceOverlay, ChangedElementIDs, ChangedConnectivityTids](FDynamicMesh3& Mesh)
	{
		UVEditorToolUtil::UpdateUVUnwrapMesh(*SourceOverlay, Mesh, UVToVertPosition, ChangedElementIDs, ChangedConnectivityTids);

		// Also copy the actual overlay
		FDynamicMeshUVOverlay* DestOverlay = Mesh.Attributes()->PrimaryUV();
		CopyMeshOverlay(*SourceOverlay, *DestOverlay, false, ChangedElementIDs, ChangedConnectivityTids);
	}, false);

	if (FastRenderUpdateTids) {
		UnwrapPreview->PreviewMesh->NotifyRegionDeferredEditCompleted(*FastRenderUpdateTids,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexUVs); // Assuming user changed positions and didn't notify yet
	}
	else
	{
		UnwrapPreview->PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexUVs, true); // Assuming user changed positions and didn't notify yet
	}

	if (WireframeDisplay)
	{
		WireframeDisplay->NotifyMeshChanged();
	}
}

void UUVEditorToolMeshInput::UpdateCanonicalFromPreviews(const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids)
{
	using namespace UVEditorToolMeshInputLocals;

	// Update UnwrapCanonical from UnwrapPreview
	UpdateOtherUnwrap(*UnwrapPreview->PreviewMesh->GetMesh(), *UnwrapCanonical, ChangedVids, ChangedConnectivityTids);
	
	// Update the overlay in AppliedCanonical from overlay in AppliedPreview
	const FDynamicMeshUVOverlay* SourceOverlay = AppliedPreview->PreviewMesh->GetMesh()->Attributes()->GetUVLayer(UVLayerIndex);
	FDynamicMeshUVOverlay* DestOverlay = AppliedCanonical->Attributes()->GetUVLayer(UVLayerIndex);
	CopyMeshOverlay(*SourceOverlay, *DestOverlay, true, ChangedVids, ChangedConnectivityTids);
}

void UUVEditorToolMeshInput::UpdatePreviewsFromCanonical(const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	using namespace UVEditorToolMeshInputLocals;

	// Update UnwrapPreview from UnwrapCanonical
	UnwrapPreview->PreviewMesh->DeferredEditMesh([this, ChangedVids, ChangedConnectivityTids](FDynamicMesh3& Mesh)
	{
		UpdateOtherUnwrap(*UnwrapCanonical, Mesh, ChangedVids, ChangedConnectivityTids);
	}, false);
	if (FastRenderUpdateTids) {
		UnwrapPreview->PreviewMesh->NotifyRegionDeferredEditCompleted(*FastRenderUpdateTids,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexUVs);
	}
	else
	{
		UnwrapPreview->PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate,
			EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexUVs, true);
	}

	if (WireframeDisplay)
	{
		WireframeDisplay->NotifyMeshChanged();
	}

	// Update AppliedPreview from AppliedCanonical
	AppliedPreview->PreviewMesh->DeferredEditMesh([this, ChangedVids, ChangedConnectivityTids](FDynamicMesh3& Mesh)
	{
		const FDynamicMeshUVOverlay* SourceOverlay = AppliedCanonical->Attributes()->GetUVLayer(UVLayerIndex);
		FDynamicMeshUVOverlay* DestOverlay = Mesh.Attributes()->GetUVLayer(UVLayerIndex);
		CopyMeshOverlay(*SourceOverlay, *DestOverlay, true, ChangedVids, ChangedConnectivityTids);
	}, false);
	if (FastRenderUpdateTids) {
		AppliedPreview->PreviewMesh->NotifyRegionDeferredEditCompleted(*FastRenderUpdateTids,
			EMeshRenderAttributeFlags::VertexUVs);
	}
	else
	{
		AppliedPreview->PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate,
			EMeshRenderAttributeFlags::VertexUVs, true);
	}
}

void UUVEditorToolMeshInput::UpdateAllFromUnwrapPreview(const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	UpdateAppliedPreviewFromUnwrapPreview(ChangedVids, ChangedConnectivityTids, FastRenderUpdateTids);
	UpdateCanonicalFromPreviews(ChangedVids, ChangedConnectivityTids);
}

void UUVEditorToolMeshInput::UpdateAllFromUnwrapCanonical(
	const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	using namespace UVEditorToolMeshInputLocals;

	// Update AppliedCanonical
	FDynamicMeshUVOverlay* SourceOverlay = UnwrapCanonical->Attributes()->PrimaryUV();
	FDynamicMeshUVOverlay* DestOverlay = AppliedCanonical->Attributes()->GetUVLayer(UVLayerIndex);
	CopyMeshOverlay(*SourceOverlay, *DestOverlay, false, ChangedVids, ChangedConnectivityTids);

	UpdatePreviewsFromCanonical(ChangedVids, ChangedConnectivityTids, FastRenderUpdateTids);
}

void UUVEditorToolMeshInput::UpdateAllFromAppliedCanonical(
	const TArray<int32>* ChangedElementIDs, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	using namespace UVEditorToolMeshInputLocals;

	// Update UnwrapCanonical
	const FDynamicMeshUVOverlay* SourceOverlay = AppliedCanonical->Attributes()->GetUVLayer(UVLayerIndex);
	UVEditorToolUtil::UpdateUVUnwrapMesh(*SourceOverlay, *UnwrapCanonical, UVToVertPosition, ChangedElementIDs, ChangedConnectivityTids);

	UpdatePreviewsFromCanonical(ChangedElementIDs, ChangedConnectivityTids, FastRenderUpdateTids);
}

void UUVEditorToolMeshInput::UpdateAllFromAppliedPreview(
	const TArray<int32>* ChangedElementIDs, const TArray<int32>* ChangedConnectivityTids, const TArray<int32>* FastRenderUpdateTids)
{
	UpdateUnwrapPreviewFromAppliedPreview(ChangedElementIDs, ChangedConnectivityTids, FastRenderUpdateTids);
	UpdateCanonicalFromPreviews(ChangedElementIDs, ChangedConnectivityTids);
}

/**
 * Helper function. Uses the positions and UV overlay of one unwrap mesh to update another one.
 */
void UUVEditorToolMeshInput::UpdateOtherUnwrap(const FDynamicMesh3& SourceUnwrapMesh,
	FDynamicMesh3& DestUnwrapMesh, const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids)
{
	using namespace UVEditorToolMeshInputLocals;

	if (!ChangedVids && !ChangedConnectivityTids)
	{
		DestUnwrapMesh.Copy(SourceUnwrapMesh, false, false, false, true); // Copy positions and UVs
		return;
	}

	CopyMeshPositions(SourceUnwrapMesh, DestUnwrapMesh, ChangedVids, ChangedConnectivityTids);

	const FDynamicMeshUVOverlay* SourceOverlay = SourceUnwrapMesh.Attributes()->PrimaryUV();
	FDynamicMeshUVOverlay* DestOverlay = DestUnwrapMesh.Attributes()->PrimaryUV();
	CopyMeshOverlay(*SourceOverlay, *DestOverlay, true, ChangedVids, ChangedConnectivityTids);
}

void UUVEditorToolMeshInput::UpdateFromCanonicalUnwrapUsingMeshChange(const FDynamicMeshChange& UnwrapCanonicalMeshChange)
{
	// Note that we know that no triangles were created or destroyed since the UV editor
	// does not allow that (it would break the mesh mappings). Otherwise we would need to
	// combine original and final tris here.
	TArray<int32> ChangedTids;
	UnwrapCanonicalMeshChange.GetSavedTriangleList(ChangedTids, true);

	TArray<int32> ChangedVids;
	TriangleToVertexIDs(UnwrapCanonical.Get(), ChangedTids, ChangedVids);

	TSet<int32> RenderUpdateTidsSet;
	VertexToTriangleOneRing(UnwrapCanonical.Get(), ChangedVids, RenderUpdateTidsSet);
	TArray<int32> RenderUpdateTids = RenderUpdateTidsSet.Array();

	// Todo: Determine a better solution for supporting explicit changed Tids and Vids
	// 	     from MeshChanges. This current approach only works well if the topology is not
	// 	     changing between updates.
	//UpdateAllFromUnwrapCanonical(&ChangedVids, & ChangedTids, & RenderUpdateTids);
	UpdateAllFromUnwrapCanonical(nullptr, nullptr, nullptr);
}
