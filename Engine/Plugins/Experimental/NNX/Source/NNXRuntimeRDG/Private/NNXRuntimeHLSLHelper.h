// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXTypes.h"

namespace NNX
{
	void ComputeTensorStrides(const FMLTensorDesc& TensorDesc, uint32 OutStrides[8], uint32 TargetNumdimensionForBroadcast = -1);
	void ComputeTensorStridesForBroadcast(const FMLTensorDesc& TensorDesc, uint32 OutputNumdimension, uint32 OutStrides[8]);
	void FillTensorStrideShaderParameters(uint32 Strides[8], FUint32Vector4& OutShaderParam0, FUint32Vector4& OutShaderParam1);
	void FillTensorStrideShaderParameters(const FMLTensorDesc& TensorDesc, FUint32Vector4& OutShaderParam0, FUint32Vector4& OutShaderParam1);
	void FillTensorStrideForBroadcastShaderParameters(const FMLTensorDesc& TensorDesc, uint32 OutputNumdimension, FUint32Vector4& OutShaderParam0, FUint32Vector4& OutShaderParam1);
	FIntVector ComputeElementWiseThreadGroups(uint32 ElementCount, uint32 GroupSizeX);

} // NNX