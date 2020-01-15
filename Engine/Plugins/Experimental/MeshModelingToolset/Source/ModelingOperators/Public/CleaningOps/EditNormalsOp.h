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
	UseExistingTopology,
	FaceNormalThreshold,
	FaceGroupID
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


