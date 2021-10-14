// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorToolUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

using namespace UE::Geometry;

namespace UVEditorToolUtilLocals
{
	// Helper function to update the triangles in an unwrap mesh based on an overlay.
	template <typename TriIteratorType>
	void UpdateUnwrapTriangles(const FDynamicMeshUVOverlay& TriSource,
		const TriIteratorType& TriIterator, FDynamicMesh3& UnwrapMeshOut)
	{
		// Updating triangles is a little messy. To be able to handle arbitrary remeshing, we
		// have to delete all tris first before adding any, so that we don't fail in SetTriangle
		// due to (temporary) non-manifold edges. We have to do this without removing verts, which
		// means we later need to check for isolated verts. Finally, because we don't have a way
		// to avoid removing temporarily isolated UV elements, we need to reattach the UV elements
		// for verts that were temporarily left isolated.

		FDynamicMeshUVOverlay* UnwrapMeshUVOverlay = UnwrapMeshOut.Attributes()->PrimaryUV();

		// Remove tris and keep track of potentially isolated elements
		TSet<int32> PotentiallyIsolatedElements;
		for (int32 Tid : TriIterator)
		{
			// Shouldn't have to have this check, but it guards against TriIterator having duplicates.
			if (!UnwrapMeshOut.IsTriangle(Tid))
			{
				// We probably didn't mean to be looking at the same tids multiple times, so something probably
				// isn't quite right upstream.
				ensure(false);
				continue;
			}

			FIndex3i PrevTriangle = UnwrapMeshOut.GetTriangle(Tid);
			for (int i = 0; i < 3; ++i)
			{
				PotentiallyIsolatedElements.Add(PrevTriangle[i]);
			}
			UnwrapMeshOut.RemoveTriangle(Tid, false);
		}

		// Reinsert new tris
		for (int32 Tid : TriIterator)
		{
			// Shouldn't have to have this check, but it guards against TriIterator having duplicates.
			if (UnwrapMeshOut.IsTriangle(Tid))
			{
				continue;
			}

			FIndex3i NewTriangle = TriSource.GetTriangle(Tid);
			UnwrapMeshOut.InsertTriangle(Tid, NewTriangle);
		}

		// Deal with isolated and non-isolated verts
		for (int32 ElementID : PotentiallyIsolatedElements)
		{
			if (!UnwrapMeshOut.IsReferencedVertex(ElementID))
			{
				UnwrapMeshOut.RemoveVertex(ElementID);
			}
			else if (!UnwrapMeshUVOverlay->IsElement(ElementID))
			{
				// This is a referenced vert without a UV element (because it got removed
				// during temporary isolation), so reinstate the element.
				FVector2f ElementValue = TriSource.GetElement(ElementID);
				UnwrapMeshUVOverlay->InsertElement(ElementID, &ElementValue.X);
			}
		}

		// Update overlay tris now that we know that the elements exist.
		for (int32 Tid : TriIterator)
		{
			UnwrapMeshUVOverlay->SetTriangle(Tid, TriSource.GetTriangle(Tid));
		}
	}
}

void UVEditorToolUtil::GenerateUVUnwrapMesh(
	const FDynamicMeshUVOverlay& UVOverlay, FDynamicMesh3& UnwrapMeshOut,
	TFunctionRef<FVector3d(const FVector2f&)> UVToVertPosition)
{
	UnwrapMeshOut.Clear();

	// The unwrap mesh will have an overlay on top of it with the corresponding UVs,
	// in case we want to draw the texture on it, etc. However note that we can't
	// just do a Copy() call using the source overlay because the parent vertices will differ.
	UnwrapMeshOut.EnableAttributes(); // Makes one UV layer
	FDynamicMeshUVOverlay* UnwrapMeshUVOverlay = UnwrapMeshOut.Attributes()->PrimaryUV();

	// Create a vert for each uv overlay element
	UnwrapMeshOut.BeginUnsafeVerticesInsert();
	UnwrapMeshUVOverlay->BeginUnsafeElementsInsert();
	for (int32 ElementID : UVOverlay.ElementIndicesItr())
	{
		FVector2f UVElement = UVOverlay.GetElement(ElementID);
		UnwrapMeshOut.InsertVertex(ElementID, UVToVertPosition(UVElement), true);
		UnwrapMeshUVOverlay->InsertElement(ElementID, &UVElement.X);
	}
	UnwrapMeshOut.EndUnsafeVerticesInsert();
	UnwrapMeshUVOverlay->EndUnsafeElementsInsert();

	// Insert a tri connecting the same vids as elements in the overlay.
	const FDynamicMesh3* OverlayParentMesh = UVOverlay.GetParentMesh();
	UnwrapMeshOut.BeginUnsafeTrianglesInsert();
	for (int32 Tid : OverlayParentMesh->TriangleIndicesItr())
	{
		if (UVOverlay.IsSetTriangle(Tid))
		{
			FIndex3i UVTri = UVOverlay.GetTriangle(Tid);
			UnwrapMeshOut.InsertTriangle(Tid, UVTri, 0, true);
			UnwrapMeshUVOverlay->SetTriangle(Tid, UVTri);
		}
	}
	UnwrapMeshOut.EndUnsafeTrianglesInsert();
}

void UVEditorToolUtil::UpdateUVUnwrapMesh(const FDynamicMeshUVOverlay& UVOverlayIn, 
	FDynamicMesh3& UnwrapMeshOut, TFunctionRef<FVector3d(const FVector2f&)> UVToVertPosition,
	const TArray<int32>* ChangedElementIDs, const TArray<int32>* ChangedTids)
{
	using namespace UVEditorToolUtilLocals;

	// Note that we don't want to use GenerateUVUnwrapMesh even when doing a full update
	// because that clears the mesh and rebuilds it, and that resets the attributes pointer.
	// That would prevent us from using a dynamic mesh change tracker across an update, as
	// it would lose its attribute pointer.

	FDynamicMeshUVOverlay* UnwrapMeshUVOverlay = UnwrapMeshOut.Attributes()->PrimaryUV();

	auto UpdateVertPositions = [&UVOverlayIn, &UnwrapMeshOut, UVToVertPosition, UnwrapMeshUVOverlay](const auto& ElementIterator)
	{
		for (int32 ElementID : ElementIterator)
		{
			if (!ensure(UVOverlayIn.IsElement(ElementID)))
			{
				// [ELEMENT_NOT_IN_SOURCE]
				// If you ended up here, then you asked to update an element that wasn't in the source mesh.
				// Perhaps you gathered the changing elements pre-change, and that element was deleted. You 
				// shouldn't gather pre-change because you risk not including any added elements, and because 
				// deleted elements should be captured by changed tri connectivity.
				continue;
			}

			FVector2f ElementValue = UVOverlayIn.GetElement(ElementID);

			// Update the actual unwrap mesh
			if (UnwrapMeshOut.IsVertex(ElementID))
			{
				UnwrapMeshOut.SetVertex(ElementID, UVToVertPosition(ElementValue));
			}
			else
			{
				UnwrapMeshOut.InsertVertex(ElementID, UVToVertPosition(ElementValue));
			}

			// Update the unwrap overlay.
			if (UnwrapMeshUVOverlay->IsElement(ElementID))
			{
				UnwrapMeshUVOverlay->SetElement(ElementID, ElementValue);
			}
			else
			{
				UnwrapMeshUVOverlay->InsertElement(ElementID, &ElementValue.X);
			}
		}
	};

	if (ChangedElementIDs)
	{
		UpdateVertPositions(*ChangedElementIDs);
	}
	else
	{
		UpdateVertPositions(UVOverlayIn.ElementIndicesItr());
	}

	if (ChangedTids)
	{
		UpdateUnwrapTriangles(UVOverlayIn, *ChangedTids, UnwrapMeshOut);
	}
	else
	{
		UpdateUnwrapTriangles(UVOverlayIn, UVOverlayIn.GetParentMesh()->TriangleIndicesItr(), UnwrapMeshOut);
	}
}

void UVEditorToolUtil::UpdateUVUnwrapMesh(const FDynamicMesh3& SourceUnwrapMesh, FDynamicMesh3& DestUnwrapMesh,
	const TArray<int32>* ChangedVids, const TArray<int32>* ChangedConnectivityTids)
{
	using namespace UVEditorToolUtilLocals;

	if (!ChangedVids && !ChangedConnectivityTids)
	{
		DestUnwrapMesh.Copy(SourceUnwrapMesh, false, false, false, true); // Copy positions and UVs
		return;
	}

	const FDynamicMeshUVOverlay* SourceOverlay = SourceUnwrapMesh.Attributes()->PrimaryUV();
	FDynamicMeshUVOverlay* DestOverlay = DestUnwrapMesh.Attributes()->PrimaryUV();

	auto UpdateVerts = [&SourceUnwrapMesh, &DestUnwrapMesh, SourceOverlay, DestOverlay](const auto& VidIterator)
	{
		for (int32 Vid : VidIterator)
		{
			if (!ensure(SourceUnwrapMesh.IsVertex(Vid)))
			{
				// See comment labeled [ELEMENT_NOT_IN_SOURCE] above.
				continue;
			}

			if (DestUnwrapMesh.IsVertex(Vid))
			{
				DestUnwrapMesh.SetVertex(Vid, SourceUnwrapMesh.GetVertex(Vid));
				DestOverlay->SetElement(Vid, SourceOverlay->GetElement(Vid));
			}
			else
			{
				DestUnwrapMesh.InsertVertex(Vid, SourceUnwrapMesh.GetVertex(Vid));
				FVector2f ElementValue = SourceOverlay->GetElement(Vid);
				DestOverlay->InsertElement(Vid, &ElementValue.X);
			}
		}
	};
	if (ChangedVids)
	{
		UpdateVerts(*ChangedVids);
	}
	else
	{
		UpdateVerts(SourceUnwrapMesh.VertexIndicesItr());
	}

	if (ChangedConnectivityTids)
	{
		UpdateUnwrapTriangles(*SourceOverlay, *ChangedConnectivityTids, DestUnwrapMesh);
	}
	else
	{
		UpdateUnwrapTriangles(*SourceOverlay, SourceUnwrapMesh.TriangleIndicesItr(), DestUnwrapMesh);
	}
}

void UVEditorToolUtil::UpdateUVOverlayFromUnwrapMesh(
	const FDynamicMesh3& UnwrapMeshIn, FDynamicMeshUVOverlay& UVOverlayOut,
	TFunctionRef<FVector2f(const FVector3d&)> VertPositionToUV,
	const TArray<int32>* ChangedVids, const TArray<int32>* ChangedTids)
{
	if (!ensure(UVOverlayOut.GetParentMesh()->MaxTriangleID() == UnwrapMeshIn.MaxTriangleID()))
	{
		return;
	}

	auto UpdateElements = [&UnwrapMeshIn, &UVOverlayOut, VertPositionToUV](const auto& VidIterator)
	{
		for (int32 Vid : VidIterator)
		{
			if (!ensure(UnwrapMeshIn.IsVertex(Vid)))
			{
				// See comment labeled [ELEMENT_NOT_IN_SOURCE] above.
				continue;
			}

			FVector2f UV = VertPositionToUV(UnwrapMeshIn.GetVertex(Vid));

			if (UVOverlayOut.IsElement(Vid))
			{
				UVOverlayOut.SetElement(Vid, UV);
			}
			else
			{
				UVOverlayOut.InsertElement(Vid, &UV.X);
			}
		}
	};
	if (ChangedVids)
	{
		UpdateElements(*ChangedVids);
	}
	else
	{
		UpdateElements(UnwrapMeshIn.VertexIndicesItr());
	}

	auto UpdateTriangles = [&UnwrapMeshIn, &UVOverlayOut](const auto& TriIterator)
	{
		TSet<int32> PotentiallyFreedElements;
		for (int32 Tid : TriIterator)
		{
			FIndex3i NewTriangle = UnwrapMeshIn.GetTriangle(Tid);
			FIndex3i PrevTriangle = UVOverlayOut.GetTriangle(Tid);
			for (int i = 0; i < 3; ++i)
			{
				if (NewTriangle[i] != PrevTriangle[i])
				{
					PotentiallyFreedElements.Add(PrevTriangle[i]);
				}
			}
			UVOverlayOut.SetTriangle(Tid, NewTriangle, false);
		}

		UVOverlayOut.FreeUnusedElements(&PotentiallyFreedElements);
	};
	if (ChangedTids)
	{
		UpdateTriangles(*ChangedTids);
	}
	else
	{
		UpdateTriangles(UnwrapMeshIn.TriangleIndicesItr());
	}
}

void UVEditorToolUtil::UpdateOverlayFromOverlay(
	const FDynamicMeshUVOverlay& OverlayIn, FDynamicMeshUVOverlay& OverlayOut,
	bool bMeshesHaveSameTopology, const TArray<int32>* ChangedElements, 
	const TArray<int32>* ChangedConnectivityTids)
{
	if (!ChangedElements && !ChangedConnectivityTids && bMeshesHaveSameTopology)
	{
		OverlayOut.Copy(OverlayIn);
		return;
	}

	auto UpdateElements = [&OverlayIn, &OverlayOut](const auto& ElementIterator)
	{
		for (int32 ElementID : ElementIterator)
		{
			if (!ensure(OverlayIn.IsElement(ElementID)))
			{
				// See comment labeled [ELEMENT_NOT_IN_SOURCE] above.
				continue;
			}

			FVector2f ElementValue = OverlayIn.GetElement(ElementID);
			if (OverlayOut.IsElement(ElementID))
			{
				OverlayOut.SetElement(ElementID, ElementValue);
			}
			else
			{
				OverlayOut.InsertElement(ElementID, &ElementValue.X);
			}
		}
	};

	if (ChangedElements)
	{
		UpdateElements(*ChangedElements);
	}
	else
	{
		UpdateElements(OverlayIn.ElementIndicesItr());
	}

	auto UpdateTriangles = [&OverlayIn, &OverlayOut](const auto& TriIterator)
	{
		// To handle arbitrary remeshing in the UV overlay, not only do we need to
		// check for freed elements only after finishing the updates, but we may 
		// also need to forcefully change the parent pointer of elements (imagine
		// a mesh of two disconnected triangles whose element mappings changed)
		TSet<int32> PotentiallyFreedElements;
		for (int32 Tid : TriIterator)
		{
			FIndex3i OldElementTri = OverlayIn.GetTriangle(Tid);
			FIndex3i NewElementTri = OverlayIn.GetTriangle(Tid);
			FIndex3i ParentTriInOutput = OverlayOut.GetParentMesh()->GetTriangle(Tid);

			for (int i = 0; i < 3; ++i)
			{
				if (NewElementTri[i] != OldElementTri[i])
				{
					PotentiallyFreedElements.Add(OldElementTri[i]);
				}

				// Force the parent pointer, if needed
				if (OverlayOut.GetParentVertex(NewElementTri[i]) != ParentTriInOutput[i])
				{
					OverlayOut.SetParentVertex(NewElementTri[i], ParentTriInOutput[i]);
				}
			}

			for (int32 i = 0; i < 3; ++i)
			{
				if (OverlayOut.GetParentVertex(NewElementTri[i]) != ParentTriInOutput[i])
				{
					OverlayOut.SetParentVertex(NewElementTri[i], ParentTriInOutput[i]);
				}
			}

			OverlayOut.SetTriangle(Tid, NewElementTri, false);
		}

		OverlayOut.FreeUnusedElements(&PotentiallyFreedElements);
	};

	if (ChangedConnectivityTids)
	{
		UpdateTriangles(*ChangedConnectivityTids);
	}
	else
	{
		UpdateTriangles(OverlayIn.GetParentMesh()->TriangleIndicesItr());
	}
}
