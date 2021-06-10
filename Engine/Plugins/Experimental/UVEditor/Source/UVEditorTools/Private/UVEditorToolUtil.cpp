// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorToolUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"

using namespace UE::Geometry;

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
		UnwrapMeshUVOverlay->InsertElement(ElementID, (float*)UVElement);
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
	if (!ChangedElementIDs && !ChangedTids)
	{
		UVEditorToolUtil::GenerateUVUnwrapMesh(UVOverlayIn, UnwrapMeshOut, UVToVertPosition);
		return;
	}

	// Otherwise, we only update the parts that we're asked.
	FDynamicMeshUVOverlay* UnwrapMeshUVOverlay = UnwrapMeshOut.Attributes()->GetUVLayer(0);
	if (ChangedElementIDs)
	{
		for (int32 ElementID : *ChangedElementIDs)
		{
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

			// Update the unwrap overlay
			if (UnwrapMeshUVOverlay->IsElement(ElementID))
			{
				UnwrapMeshUVOverlay->SetElement(ElementID, ElementValue);
			}
			else
			{
				UnwrapMeshUVOverlay->InsertElement(ElementID, (float*)ElementValue);
			}
		}
	}

	if (ChangedTids)
	{
		for (int32 Tid : *ChangedTids)
		{
			FIndex3i Triangle = UVOverlayIn.GetTriangle(Tid);
			UnwrapMeshUVOverlay->SetTriangle(Tid, Triangle);
			UnwrapMeshOut.SetTriangle(Tid, Triangle);
		}
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

	auto UpdateUVElement = [&UnwrapMeshIn, &UVOverlayOut, VertPositionToUV](int32 Vid) {
		if (UVOverlayOut.IsElement(Vid))
		{
			UVOverlayOut.SetElement(Vid, VertPositionToUV(UnwrapMeshIn.GetVertex(Vid)));
		}
		else
		{
			UVOverlayOut.InsertElement(Vid, (float*)VertPositionToUV(UnwrapMeshIn.GetVertex(Vid)));
		}
	};

	// Update all vids and tids if not given changed items
	if (!ChangedVids && !ChangedTids)
	{
		for (int32 Vid : UnwrapMeshIn.VertexIndicesItr())
		{
			UpdateUVElement(Vid);
		}
		for (int32 Tid : UnwrapMeshIn.TriangleIndicesItr())
		{
			UVOverlayOut.SetTriangle(Tid, UnwrapMeshIn.GetTriangle(Tid));
		}

		return;
	}

	//Otherwise, only update what we're told about.
	if (ChangedVids)
	{
		for (int32 Vid : *ChangedVids)
		{
			UpdateUVElement(Vid);
		}
	}
	if (ChangedTids)
	{
		for (int32 Tid : *ChangedTids)
		{
			UVOverlayOut.SetTriangle(Tid, UnwrapMeshIn.GetTriangle(Tid));
		}
	}
}
