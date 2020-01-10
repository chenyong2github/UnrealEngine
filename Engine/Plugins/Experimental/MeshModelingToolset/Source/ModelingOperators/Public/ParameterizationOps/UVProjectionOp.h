// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "UVProjectionOp.generated.h"


UENUM()
enum class EUVProjectionMethod : uint8
{
	Cube,
	Cylinder,
	Plane
};



class MODELINGOPERATORS_API FUVProjectionOp : public FDynamicMeshOperator
{
public:
	virtual ~FUVProjectionOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3> OriginalMesh;

	EUVProjectionMethod ProjectionMethod;
	float CylinderProjectToTopOrBottomAngleThreshold;
	FTransform ProjectionTransform;
	FVector2D UVScale, UVOffset;
	bool bWorldSpaceUVScale;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


