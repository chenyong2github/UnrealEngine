// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLHelper.h"

#include "RHI.h"

namespace NNX
{
	void FillTensorStrideShaderParameters(const FTensor& TensorDesc, TStaticArray<FUintVector4, NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS, 16U>& OutShaderParam, int32 Idx, int32 TargetNumdimensionForBroadcast)
	{
		if (TargetNumdimensionForBroadcast == -1)
		{
			TargetNumdimensionForBroadcast = TensorDesc.GetShape().Rank();
		}
		checkf(TargetNumdimensionForBroadcast >= TensorDesc.GetShape().Rank(), TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), TensorDesc.GetShape().Rank(), TargetNumdimensionForBroadcast);
		int32 Offset = TargetNumdimensionForBroadcast - TensorDesc.GetShape().Rank();

		static_assert(FTensorShape::MaxRank <= NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS);
		for (int32 i = 7; i >= 0; --i)
		{
			if (i >= TargetNumdimensionForBroadcast || i < Offset)
			{
				OutShaderParam[i][Idx] = 0;
			}
			else if (i == TargetNumdimensionForBroadcast - 1)
			{
				OutShaderParam[i][Idx] = 1;
			}
			else
			{
				OutShaderParam[i][Idx] = OutShaderParam[i + 1][Idx] * TensorDesc.GetShape().Data[i + 1 - Offset];
			}
		}
	}

	void FillTensorStrideForBroadcastShaderParameters(const FTensor& TensorDesc, int32 OutputNumdimension, TStaticArray<FUintVector4, NXRT_TENSORSTRIDEINFO_MAX_NUM_DIMENSIONS, 16U>& OutShaderParam, int32 Idx)
	{
		checkf(OutputNumdimension >= TensorDesc.GetShape().Rank(), TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), TensorDesc.GetShape().Rank(), OutputNumdimension);
		FillTensorStrideShaderParameters(TensorDesc, OutShaderParam, Idx, OutputNumdimension);
		int32 Offset = OutputNumdimension - TensorDesc.GetShape().Rank();
		for (int32 i = Offset; i < OutputNumdimension; ++i)
		{
			// the stride for broadcast dimension is kept as 0
			if (TensorDesc.GetShape().Data[i - Offset] == 1)
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