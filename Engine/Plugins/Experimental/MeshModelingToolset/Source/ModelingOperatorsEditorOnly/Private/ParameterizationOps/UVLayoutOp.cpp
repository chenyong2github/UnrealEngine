// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizationOps/UVLayoutOp.h"

#include "OverlappingCorners.h"
#include "LayoutUV.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"

#include "Selections/MeshConnectedComponents.h"

#include "Parameterization/MeshUVPacking.h"


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
	if (Progress && Progress->Cancelled())
	{
		return;
	}
	ResultMesh->Copy(*OriginalMesh, true, true, true, true);

	if (!ensureMsgf(ResultMesh->HasAttributes(), TEXT("Attributes not found on mesh? Conversion should always create them, so this operator should not need to do so.")))
	{
		ResultMesh->EnableAttributes();
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}


	int UVLayerInput = 0, UVLayerOutput = 0;
	FDynamicMeshUVOverlay* UVLayer = ResultMesh->Attributes()->GetUVLayer(UVLayerInput);
	

	bool bWillRepackIslands = (UVLayoutMode != EUVLayoutOpLayoutModes::TransformOnly);

	// split bowties so that we can process islands independently
	if (bWillRepackIslands || bAlwaysSplitBowties)
	{
		UVLayer->SplitBowties();
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	FDynamicMeshUVPacker Packer(UVLayer);
	Packer.TextureResolution = this->TextureResolution;
	Packer.GutterSize = this->GutterSize;
	Packer.bAllowFlips = this->bAllowFlips;

	if (UVLayoutMode == EUVLayoutOpLayoutModes::RepackToUnitRect)
	{
		if (Packer.StandardPack() == false)
		{
			// failed... what to do?
			return;
		}
	}
	else if (UVLayoutMode == EUVLayoutOpLayoutModes::StackInUnitRect)
	{
		if (Packer.StackPack() == false)
		{
			// failed... what to do?
			return;
		}
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	if (UVScaleFactor != 1.0 || UVTranslation != FVector2f::Zero() )
	{
		for (int ElementID : UVLayer->ElementIndicesItr())
		{
			FVector2f UV = UVLayer->GetElement(ElementID);
			UV = (UV * UVScaleFactor) + UVTranslation;
			UVLayer->SetElement(ElementID, UV);
		}
	}

}
