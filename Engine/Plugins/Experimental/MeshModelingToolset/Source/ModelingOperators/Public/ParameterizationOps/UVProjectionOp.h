// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "OrientedBoxTypes.h"

#include "UVProjectionOp.generated.h"

UENUM()
enum class EUVProjectionMethod : uint8
{
	/** Calculate UVs by assigning triangles to faces of a Box, and then apply per-box-face planar projection */
	Box,
	/** Calculate UVs by assigning triangles to Cylinder (radial projection) or Endcaps (planar projection) */
	Cylinder,
	/** Calculate UVs by projecting to a 3D plane */
	Plane,
	/** Calculate UVs by Exponential Map Projection centered at nearest surface point to input 3D plane */
	ExpMap
};

namespace UE
{
namespace Geometry
{

class MODELINGOPERATORS_API FUVProjectionOp : public FDynamicMeshOperator
{
public:
	virtual ~FUVProjectionOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	TSharedPtr<TArray<int32>, ESPMode::ThreadSafe> TriangleROI;
	int32 UseUVLayer = 0;

	// transform from Mesh into space of Projection Geometry
	FTransform3d MeshToProjectionSpace;

	EUVProjectionMethod ProjectionMethod = EUVProjectionMethod::Box;
	FOrientedBox3d ProjectionBox;
	float CylinderSplitAngle = 45.0f;

	// ExpMap parameters for modifying normals
	double BlendWeight = 0.0;
	int32 SmoothingRounds = 0;
	double SmoothingAlpha = 0.5;

	float CylinderProjectToTopOrBottomAngleThreshold = 45.0f;

	// position to use as UV origin
	FVector2f UVOrigin = FVector2f(0.5f,0.5f);
	// rotation applied to computed UVs
	float UVRotationAngleDeg = 0.0;
	// scale applied to computed UVs
	FVector2f UVScale = FVector2f::One();
	// translation applied to computed UVs
	FVector2f UVTranslate = FVector2f::Zero();


	//
	// FDynamicMeshOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override;


	virtual void CalculateResult_Box(FProgressCancel* Progress);
	virtual void CalculateResult_Plane(FProgressCancel* Progress);
	virtual void CalculateResult_ExpMap(FProgressCancel* Progress);
	virtual void CalculateResult_Cylinder(FProgressCancel* Progress);
};

} // end namespace UE::Geometry
} // end namespace UE

