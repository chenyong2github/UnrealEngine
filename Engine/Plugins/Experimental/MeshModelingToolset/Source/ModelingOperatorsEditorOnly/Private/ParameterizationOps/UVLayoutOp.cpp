// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizationOps/UVLayoutOp.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"

#include "Selections/MeshConnectedComponents.h"

#include "Parameterization/MeshUVPacking.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FUVLayoutOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
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


	int UVLayerInput = UVLayerIndex, UVLayerOutput = UVLayerIndex;
	FDynamicMeshUVOverlay* UseUVLayer = ResultMesh->Attributes()->GetUVLayer(UVLayerInput);
	

	bool bWillRepackIslands = (UVLayoutMode != EUVLayoutOpLayoutModes::TransformOnly);

	// split bowties so that we can process islands independently
	if (bWillRepackIslands || bAlwaysSplitBowties)
	{
		UseUVLayer->SplitBowties();
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	FDynamicMeshUVPacker Packer(UseUVLayer);
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
		for (int ElementID : UseUVLayer->ElementIndicesItr())
		{
			FVector2f UV = UseUVLayer->GetElement(ElementID);
			UV = (UV * UVScaleFactor) + UVTranslation;
			UseUVLayer->SetElement(ElementID, UV);
		}
	}

}
