// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLHelper.h"

#include "RHI.h"

namespace NNX
{
	void FillTensorStrideShaderParameters(const FMLTensorDesc& TensorDesc, TStaticArray<FUintVector4, NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS, 16U>& OutShaderParam, uint32 Idx, uint32 TargetNumdimensionForBroadcast)
	{
		if (TargetNumdimensionForBroadcast == -1)
		{
			TargetNumdimensionForBroadcast = TensorDesc.Dimension;
		}
		checkf(TargetNumdimensionForBroadcast >= TensorDesc.Dimension, TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), TensorDesc.Dimension, TargetNumdimensionForBroadcast);
		uint32 Offset = TargetNumdimensionForBroadcast - TensorDesc.Dimension;

		static_assert(FMLTensorDesc::MaxTensorDimension <= NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS);
		for (int32 i = 7; i >= 0; --i)
		{
			if ((uint32)i >= TargetNumdimensionForBroadcast || (uint32)i < Offset)
			{
				OutShaderParam[i][Idx] = 0;
			}
			else if ((uint32)i == TargetNumdimensionForBroadcast - 1)
			{
				OutShaderParam[i][Idx] = 1;
			}
			else
			{
				OutShaderParam[i][Idx] = OutShaderParam[i + 1][Idx] * TensorDesc.Sizes[i + 1 - Offset];
			}
		}
	}

	void FillTensorStrideForBroadcastShaderParameters(const FMLTensorDesc& TensorDesc, uint32 OutputNumdimension, TStaticArray<FUintVector4, NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS, 16U>& OutShaderParam, uint32 Idx)
	{
		checkf(OutputNumdimension >= TensorDesc.Dimension, TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), TensorDesc.Dimension, OutputNumdimension);
		FillTensorStrideShaderParameters(TensorDesc, OutShaderParam, Idx, OutputNumdimension);
		uint32 Offset = OutputNumdimension - TensorDesc.Dimension;
		for (uint32 i = Offset; i < OutputNumdimension; ++i)
		{
			// the stride for broadcast dimension is kept as 0
			if (TensorDesc.Sizes[i - Offset] == 1)
			{
				OutShaderParam[i][Idx] = 0;
			}
		}
	}

	FIntVector ComputeElementWiseThreadGroups(uint32 ElementCount, uint32 GroupSizeX)
	{
		FIntVector ThreadGroupCount;
		ThreadGroupCount.X = FMath::DivideAndRoundUp(ElementCount, GroupSizeX);
		ThreadGroupCount.Y = 1;
		ThreadGroupCount.Z = 1;
		if (ThreadGroupCount.X > GRHIMaxDispatchThreadGroupsPerDimension.X)
		{
			ThreadGroupCount.Y = FMath::DivideAndRoundUp(ThreadGroupCount.X, GRHIMaxDispatchThreadGroupsPerDimension.X);
			ThreadGroupCount.X = FMath::DivideAndRoundUp(ThreadGroupCount.X, ThreadGroupCount.Y);
			ensure(ThreadGroupCount.Y <= GRHIMaxDispatchThreadGroupsPerDimension.Y);
		}
		return ThreadGroupCount;
	}

} // NNX