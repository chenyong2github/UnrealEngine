// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "EditNormalsOp.generated.h"


UENUM()
enum class ENormalCalculationMethod : uint8
{
	AreaWeighted,
	AngleWeighted,
	AreaAngleWeighting
};


UENUM()
enum class ESplitNormalMethod : uint8
{
	/** Keep the existing split-normals structure on the mesh */
	UseExistingTopology,
	/** Recompute split-normals by grouping faces around each vertex based on an angle threshold */
	FaceNormalThreshold,
	/** Recompute split-normals by grouping faces around each vertex that share a face/polygroup */
	FaceGroupID,
	/** Set each triangle-vertex to have the face normal of that triangle's plane */
	PerTriangle,
	/** Set each vertex to have a fully shared normal, ie no split normals  */
	PerVertex
};


class MODELINGOPERATORS_API FEditNormalsOp : public FDynamicMeshOperator
{
public:
	virtual ~FEditNormalsOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3> OriginalMesh;

	bool bFixInconsistentNormals;
	bool bInvertNormals;
	bool bRecomputeNormals;
	ENormalCalculationMethod NormalCalculationMethod;
	ESplitNormalMethod SplitNormalMethod;
	bool bAllowSharpVertices;
	float NormalSplitThreshold;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


