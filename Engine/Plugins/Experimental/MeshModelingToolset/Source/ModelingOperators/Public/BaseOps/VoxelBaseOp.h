// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"



class MODELINGOPERATORS_API FVoxelBaseOp : public FDynamicMeshOperator
{
public:
	virtual ~FVoxelBaseOp() {}

	// inputs
	double MinComponentVolume = 0.0, MinComponentArea = 0.0;

	bool bAutoSimplify = true;
	double SimplifyMaxErrorFactor = 1.0;

	int OutputVoxelCount = 1024;
	int InputVoxelCount = 1024;

	bool bRemoveInternalSurfaces = false;

	virtual void PostProcessResult(FProgressCancel* Progress, double MeshCellSize);
};


