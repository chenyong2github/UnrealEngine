// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizationOps/UVLayoutOp.h"

#include "OverlappingCorners.h"
#include "LayoutUV.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"

#include "Selections/MeshConnectedComponents.h"


void FUVLayoutOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

// very task-specific lightweight view of a FDynamicMesh3; ONLY for compact meshes with attributes
// not intended for use outside of this narrow context
class FCompactDynamicMeshWithAttributesLayoutView : public FLayoutUV::IMeshView
{
	FDynamicMesh3* Mesh;
	int UVLayerInput, UVLayerOutput;

public:
	FCompactDynamicMeshWithAttributesLayoutView(FDynamicMesh3* Mesh, int UVLayerIn=0, int UVLayerOut=0) : Mesh(Mesh), UVLayerInput(UVLayerIn), UVLayerOutput(UVLayerOut)
	{
		check(Mesh->HasAttributes());
		check(Mesh->IsCompact());
		check(Mesh->Attributes()->NumUVLayers() > UVLayerIn);
		check(Mesh->Attributes()->NumUVLayers() > UVLayerOut);
	}

	uint32 GetNumIndices() const override
	{
		return Mesh->TriangleCount() * 3;
	}
	FVector GetPosition(uint32 Index) const override
	{
		return FVector(Mesh->GetVertex(Mesh->GetTriangle(Index / 3)[Index % 3]));
	}
	FVector GetNormal(uint32 Index) const override
	{
		const FDynamicMeshNormalOverlay* NormalOverlay = Mesh->Attributes()->PrimaryNormals();
		FIndex3i NormalEl = NormalOverlay->GetTriangle(Index / 3);
		FVector3f Normal;
		NormalOverlay->GetElement(NormalEl[Index % 3], Normal);
		return FVector(Normal);
	}
	FVector2D GetInputTexcoord(uint32 Index) const override
	{
		const FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerInput);
		FIndex3i UVEl = UVOverlay->GetTriangle(Index / 3);
		FVector2f UV;
		UVOverlay->GetElement(UVEl[Index % 3], UV);
		return FVector2D(UV);
	}

	/**
	 * This function is kind of nonsense for our use case as we cannot initialize a UV overlay from a single number
	 * (unless we make every triangle disconnected in a fully raw wedge thing, which we never ever want to do)
	 * So we assume the calling code will only call this w/ NumIndices matching the input layer & the intent of making the UV layers the same.
	 */
	void InitOutputTexcoords(uint32 Num) override
	{
		if (UVLayerInput != UVLayerOutput)
		{
			Mesh->Attributes()->GetUVLayer(UVLayerOutput)->Copy(*Mesh->Attributes()->GetUVLayer(UVLayerInput));
		}
		check(Num == Mesh->TriangleCount() * 3);
	}

	void SetOutputTexcoord(uint32 Index, const FVector2D& Value) override
	{
		FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerOutput);
		FIndex3i UVEl = UVOverlay->GetTriangle(Index / 3);
		UVOverlay->SetElement(UVEl[Index % 3], FVector2f(Value));
	}
};

/**
 * Create an overlapping corner map to identify wedge indices that share the same UV element index for a given UV layer
 */
FOverlappingCorners OverlappingCornersFromUVs(const FDynamicMesh3* Mesh, int UVLayerIndex)
{
	// track all wedge indices that map to the same UV element
	const FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerIndex);
	FOverlappingCorners Overlaps;
	Overlaps.Init(Mesh->TriangleCount()*3);
	for (int ElementID : UVOverlay->ElementIndicesItr())
	{
		int VertID = UVOverlay->GetParentVertex(ElementID);
		int LastWedgeIdx = -1;
		for (int TriID : Mesh->VtxTrianglesItr(VertID))
		{
			FIndex3i ElTri = UVOverlay->GetTriangle(TriID);
			for (int SubIdx = 0; SubIdx < 3; SubIdx++)
			{
				if (ElTri[SubIdx] == ElementID)
				{
					int WedgeIdx = TriID * 3 + SubIdx;
					if (LastWedgeIdx != -1)
					{
						Overlaps.Add(LastWedgeIdx, WedgeIdx);
					}
					LastWedgeIdx = WedgeIdx;
				}
			}
		}
	}
	Overlaps.FinishAdding();
	return Overlaps;
}


void FUVLayoutOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}
	bool bDiscardAttributes = false;
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);

	if (!ensureMsgf(ResultMesh->HasAttributes(), TEXT("Attributes not found on mesh? Conversion should always create them, so this operator should not need to do so.")))
	{
		ResultMesh->EnableAttributes();
	}

	if (Progress->Cancelled())
	{
		return;
	}


	int UVLayerInput = 0, UVLayerOutput = 0;
	ResultMesh->Attributes()->GetUVLayer(UVLayerInput)->SplitBowties();

	if (Progress->Cancelled())
	{
		return;
	}

	if (!bSeparateUVIslands)
	{
		// The FLayoutUV class doesn't let us access the charts so this code path just finds them directly

		FDynamicMeshUVOverlay* UVLayer = ResultMesh->Attributes()->GetUVLayer(UVLayerOutput);
		FMeshConnectedComponents UVComponents(ResultMesh.Get());
		UVComponents.FindConnectedTriangles([&UVLayer](int32 Triangle0, int32 Triangle1) {
			return UVLayer->AreTrianglesConnected(Triangle0, Triangle1);
		});
		TArray<FAxisAlignedBox2f> ComponentBounds; ComponentBounds.SetNum(UVComponents.Num());
		TArray<int32> ElToComponent; ElToComponent.SetNum(UVLayer->ElementCount());
		for (int32 ComponentIdx = 0; ComponentIdx < UVComponents.Num(); ComponentIdx++)
		{
			FMeshConnectedComponents::FComponent& Component = UVComponents.GetComponent(ComponentIdx);
			FAxisAlignedBox2f& BoundsUV = ComponentBounds[ComponentIdx];
			BoundsUV = FAxisAlignedBox2f::Empty();

			for (int TID : Component.Indices)
			{
				if (UVLayer->IsSetTriangle(TID))
				{
					FIndex3i TriEls = UVLayer->GetTriangle(TID);
					for (int SubIdx = 0; SubIdx < 3; SubIdx++)
					{
						int ElID = TriEls[SubIdx];
						BoundsUV.Contain(UVLayer->GetElement(ElID));
						ElToComponent[ElID] = ComponentIdx;
					}
				}
			}
		}
		float MaxDim = 0;
		for (FAxisAlignedBox2f& BoundsUV : ComponentBounds)
		{
			MaxDim = FMath::Max(MaxDim, BoundsUV.MaxDim());
		}
		float Scale = 1;
		if (MaxDim >= FLT_MIN)
		{
			Scale = 1.0 / MaxDim;
		}

		Scale *= UVScaleFactor; // apply global scale factor
		for (int ElID : UVLayer->ElementIndicesItr())
		{
			UVLayer->SetElement(ElID, (UVLayer->GetElement(ElID) - ComponentBounds[ElToComponent[ElID]].Min) * Scale);
		}
	}
	else
	{
		// use FLayoutUV to do the layout

		FCompactDynamicMeshWithAttributesLayoutView MeshView(ResultMesh.Get(), UVLayerInput, UVLayerOutput);
		FLayoutUV LayoutUV(MeshView);
		FOverlappingCorners Overlaps = OverlappingCornersFromUVs(ResultMesh.Get(), UVLayerInput);
		if (Progress->Cancelled())
		{
			return;
		}

		LayoutUV.FindCharts(Overlaps);
		if (Progress->Cancelled())
		{
			return;
		}

		LayoutUV.FindBestPacking(TextureResolution);
		if (Progress->Cancelled())
		{
			return;
		}

		LayoutUV.CommitPackedUVs();

		// Add global scaling as a postprocess
		if (UVScaleFactor != 1.0)
		{
			FDynamicMeshUVOverlay* UVLayer = ResultMesh->Attributes()->GetUVLayer(UVLayerOutput);
			for (int ElID : UVLayer->ElementIndicesItr())
			{
				UVLayer->SetElement(ElID, UVLayer->GetElement(ElID) * UVScaleFactor);
			}
		}
	}
}
