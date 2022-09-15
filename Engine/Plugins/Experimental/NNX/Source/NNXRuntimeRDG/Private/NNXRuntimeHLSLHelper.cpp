// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLHelper.h"

#include "RHI.h"

namespace NNX
{
	void ComputeTensorStrides(const FMLTensorDesc& TensorDesc, uint32 OutStrides[8], uint32 TargetNumdimensionForBroadcast)
	{
		if (TargetNumdimensionForBroadcast == -1)
		{
			TargetNumdimensionForBroadcast = TensorDesc.Dimension;
		}
		checkf(TargetNumdimensionForBroadcast >= TensorDesc.Dimension, TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), TensorDesc.Dimension, TargetNumdimensionForBroadcast);
		uint32 Offset = TargetNumdimensionForBroadcast - TensorDesc.Dimension;

		static_assert(FMLTensorDesc::MaxTensorDimension <= 8);
		for (int32 i = 7; i >= 0; --i)
		{
			if ((uint32)i >= TargetNumdimensionForBroadcast || (uint32)i < Offset)
			{
				OutStrides[i] = 0;
			}
			else if ((uint32)i == TargetNumdimensionForBroadcast - 1)
			{
				OutStrides[i] = 1;
			}
			else
			{
				OutStrides[i] = OutStrides[i + 1] * TensorDesc.Sizes[i + 1 - Offset];
			}
		}
	}

	void ComputeTensorStridesForBroadcast(const FMLTensorDesc& TensorDesc, uint32 OutputNumdimension, uint32 OutStrides[8])
	{
		checkf(OutputNumdimension >= TensorDesc.Dimension, TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), TensorDesc.Dimension, OutputNumdimension);
		ComputeTensorStrides(TensorDesc, OutStrides, OutputNumdimension);
		uint32 Offset = OutputNumdimension - TensorDesc.Dimension;
		for (uint32 i = Offset; i < OutputNumdimension; ++i)
		{
			// the stride for broadcast dimension is kept as 0
			if (TensorDesc.Sizes[i - Offset] == 1)
			{
				OutStrides[i] = 0;
			}
		}
	}

	void FillTensorStrideShaderParameters(uint32 Strides[8], FUint32Vector4& OutShaderParam0, FUint32Vector4& OutShaderParam1)
	{
		for (uint32 i = 0; i < 4; ++i)
		{
			OutShaderParam0[i] = Strides[i];
			OutShaderParam1[i] = Strides[i + 4];
		}
	}

	void FillTensorStrideShaderParameters(const FMLTensorDesc& TensorDesc, FUint32Vector4& OutShaderParam0, FUint32Vector4& OutShaderParam1)
	{
		uint32 Strides[8];
		ComputeTensorStrides(TensorDesc, Strides);
		FillTensorStrideShaderParameters(Strides, OutShaderParam0, OutShaderParam1);
	}

	void FillTensorStrideForBroadcastShaderParameters(const FMLTensorDesc& TensorDesc, uint32 OutputNumdimension, FUint32Vector4& OutShaderParam0, FUint32Vector4& OutShaderParam1)
	{
		uint32 Strides[8];
		ComputeTensorStridesForBroadcast(TensorDesc, OutputNumdimension, Strides);
		FillTensorStrideShaderParameters(Strides, OutShaderParam0, OutShaderParam1);
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