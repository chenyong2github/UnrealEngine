// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/UVEditorToolMeshInput.h"

#include "Drawing/MeshElementsVisualizer.h" // for wireframe display
#include "DynamicMesh/DynamicMesh3.h"
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
			for (int32 Tid : *ChangedConnectivityTids                                         )
			{
				MeshOut.SetTriangle(Tid, MeshIn.GetTriangle(Tid));
			}
		}
	}

	// Aside from potentially  doing partial updates based on ChangedElements and ChangedConnectivityTids, this
	// function also differs from a simple overlay copy in that it does not copy the parent pointer arrays.
	// Also it assumes the existence of all the Tids.
	void CopyMeshOverlay(const UE::Geometry::FDynamicMeshUVOverlay& OverlayIn, UE::Geometry::FDynamicMeshUVOverlay& OverlayOut,
		const TArray<int32>* ChangedElements, const TArray<int32>* ChangedConnectivityTids)
	{
		auto UpdateOrInsertElement = [&OverlayIn, &OverlayOut](int32 ElementID) {
			if (OverlayOut.IsElement(ElementID))
			{
				OverlayOut.SetElement(ElementID, OverlayIn.GetElement(ElementID));
			}
			else
			{
				OverlayOut.InsertElement(ElementID, (float*)OverlayIn.GetElement(ElementID));
			}
		};

		if (!ChangedElements && !ChangedConnectivityTids)
		{
			for (int32 ElementID : OverlayIn.ElementIndicesItr())
			{
				UpdateOrInsertElement(ElementID);
			}
			for (int32 Tid : OverlayIn.GetParentMesh()->TriangleIndicesItr())
			{
				OverlayOut.SetTriangle(Tid, OverlayIn.GetTriangle(Tid));
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
			for (int32 Tid : *ChangedConnectivityTids)
			{
				OverlayOut.SetTriangle(Tid, OverlayIn.GetTriangle(Tid));
			}
		}
	}
}

bool UUVEditorToolMeshInput::IsValid() const
{
	return UnwrapCanonical
		&& UnwrapPreview && UnwrapPreview->IsValidLowLevel()
		&& AppliedCanonical
		&& AppliedPreview && AppliedPreview->IsValidLowLevel()
		&& OriginalAsset && OriginalAsset->IsValidLowLevel()
		&& UVLayerIndex >= 0;
}

bool UUVEditorToolMeshInput::InitializeMeshes(
	UToolTarget* Target, int32 UVLayerIndexIn,
	UWorld* UnwrapWorld, UWorld* LivePreviewWorld,
	UMaterialInterface* WorkingMaterialIn,
	TFunction<FVector3d(const FVector2f&)> UVToVertPositionFuncIn,
	TFunction<FVector2f(const FVector3d&)> VertPositionToUVFuncIn)
{
	// TODO: The ModelingToolTargetUtil.h doesn't currently have all the proper functions we want
	// to access the tool target (for instance, to get a dynamic mesh without a mesh description).
	// We'll need to update this function once they exist.
	using namespace UE::ToolTarget;

	checkSlow(Cast<IAssetBackedTarget>(Target) 
		&& Cast<IDynamicMeshProvider>(Target)
		&& Cast<IMaterialProvider>(Target));

	OriginalAsset = Cast<IAssetBackedTarget>(Target)->GetSourceData();
	UVLayerIndex = UVLayerIndexIn;
	UVToVertPosition = UVToVertPositionFuncIn;
	VertPositionToUV = VertPositionToUVFuncIn;

	// We are given the preview- i.e. the mesh with the uv layer applied.
	AppliedCanonical = Cast<IDynamicMeshProvider>(Target)->GetDynamicMesh();

	if (!AppliedCanonical->HasAttributes()
		|| UVLayerIndex >= AppliedCanonical->Attributes()->NumUVLayers())
	{
		return false;
	}

	// Create a preview version of the 3d mesh
	AppliedPreview = NewObject<UMeshOpPreviewWithBackgroundCompute>();
	AppliedPreview->Setup(LivePreviewWorld);
	AppliedPreview->PreviewMesh->UpdatePreview(AppliedCanonical.Get());

	FComponentMaterialSet MaterialSet;
	Cast<IMaterialProvider>(Target)->GetMaterialSet(MaterialSet);
	AppliedPreview->ConfigureMaterials(MaterialSet.Materials, WorkingMaterialIn);

	// Set up the unwrapped mesh
	UnwrapCanonical = MakeShared<FDynamicMesh3>();
	UVEditorToolUtil::GenerateUVUnwrapMesh(
		*AppliedCanonical->Attributes()->GetUVLayer(UVLayerIndex),
		*UnwrapCanonical, UVToVertPosition);

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

	UnwrapPreview->Cancel();
	AppliedPreview->Cancel();

	UnwrapCanonical = nullptr;
	UnwrapPreview = nullptr;
	AppliedCanonical = nullptr;
	AppliedPreview = nullptr;

	OriginalAsset = nullptr;
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
			FDynamicMeshUVOverlay* DestOverlay = Mesh.Attributes()->PrimaryUV();

			CopyMeshOverlay(*SourceOverlay, *DestOverlay, ChangedVids, ChangedConnectivityTids);
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
	const FDynamicMeshUVOverlay* SourceOverlay = AppliedPreview->PreviewMesh->GetMesh()->Attributes()->PrimaryUV();
	UnwrapPreview->PreviewMesh->DeferredEditMesh([this, SourceOverlay, ChangedElementIDs, ChangedConnectivityTids](FDynamicMesh3& Mesh)
	{
		UVEditorToolUtil::UpdateUVUnwrapMesh(*SourceOverlay, Mesh, UVToVertPosition, ChangedElementIDs, ChangedConnectivityTids);

		// Also copy the actual overlay
		FDynamicMeshUVOverlay* DestOverlay = Mesh.Attributes()->PrimaryUV();
		CopyMeshOverlay(*SourceOverlay, *DestOverlay, ChangedElementIDs, ChangedConnectivityTids);
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
	CopyMeshOverlay(*SourceOverlay, *DestOverlay, ChangedVids, ChangedConnectivityTids);
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
		CopyMeshOverlay(*SourceOverlay, *DestOverlay, ChangedVids, ChangedConnectivityTids);
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
	CopyMeshOverlay(*SourceOverlay, *DestOverlay, ChangedVids, ChangedConnectivityTids);

	UpdatePreviewsFromCanonical(ChangedVids, ChangedConnectivityTids, FastRenderUpdateTids);
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
	CopyMeshOverlay(*SourceOverlay, *DestOverlay, ChangedVids, ChangedConnectivityTids);
}
