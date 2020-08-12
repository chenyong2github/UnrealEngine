// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"


enum class EUVLayoutOpLayoutModes
{
	TransformOnly = 0,
	RepackToUnitRect = 1,
	StackInUnitRect = 2
};


class MODELINGOPERATORSEDITORONLY_API FUVLayoutOp : public FDynamicMeshOperator
{
public:
	virtual ~FUVLayoutOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3> OriginalMesh;

	EUVLayoutOpLayoutModes UVLayoutMode = EUVLayoutOpLayoutModes::RepackToUnitRect;

	int TextureResolution = 128;
	bool bAllowFlips = false;
	bool bAlwaysSplitBowties = true;
	float UVScaleFactor = 1.0;
	float GutterSize = 1.0;
	FVector2f UVTranslation = FVector2f::Zero();

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


