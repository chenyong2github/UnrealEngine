// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXTypes.h"
#include "Containers/StaticArray.h"
#include "NNXShaderParameters.h"

namespace NNX
{
	void FillTensorStrideShaderParameters(const FMLTensorDesc& TensorDesc, TStaticArray<FUintVector4, NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS, 16U>& OutShaderParam, int32 Idx, int32 TargetNumdimensionForBroadcast = -1);
	void FillTensorStrideForBroadcastShaderParameters(const FMLTensorDesc& TensorDesc, int32 OutputNumdimension, TStaticArray<FUintVector4, NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS, 16U>& OutShaderParam, int32 Idx);
	FIntVector ComputeElementWiseThreadGroups(uint32 ElementCount, uint32 GroupSizeX);
} // NNX