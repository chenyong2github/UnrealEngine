// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Polygroups/PolygroupSet.h"


namespace UE
{
namespace Geometry
{


enum class ERecomputeUVsUnwrapType
{
	ExpMap = 0,
	ConformalFreeBoundary = 1
};



enum class ERecomputeUVsIslandMode
{
	PolyGroups = 0,
	UVIslands = 1
};


class MODELINGOPERATORS_API FRecomputeUVsOp : public FDynamicMeshOperator
{
public:
	virtual ~FRecomputeUVsOp() {}

	//
	// Inputs
	// 

	// source mesh
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> InputMesh;
	
	// source groups (optional)
	TSharedPtr<UE::Geometry::FPolygroupSet, ESPMode::ThreadSafe> InputGroups;

	// area scaling
	bool bNormalizeAreas = true;
	float AreaScaling = 1.0;

	// UV layer
	int32 UVLayer = 0;

	// Atlas Packing parameters
	bool bPackUVs = true;
	int32 PackingTextureResolution = 512;
	float PackingGutterWidth = 1.0f;

	ERecomputeUVsUnwrapType UnwrapType = ERecomputeUVsUnwrapType::ExpMap;
	ERecomputeUVsIslandMode IslandMode = ERecomputeUVsIslandMode::PolyGroups;

	// set ability on protected transform.
	void SetTransform(const FTransform3d& XForm)
	{
		ResultTransform = XForm;
	}

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;


protected:

	void NormalizeUVAreas(const FDynamicMesh3& Mesh, FDynamicMeshUVOverlay* Overlay, float GlobalScale = 1.0f);
};

} // end namespace UE::Geometry
} // end namespace UE
