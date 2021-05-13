// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelSource.h"

#include "ShaderParameterMetadataBuilder.h"

void UComputeKernelSource::GetShaderParameters(class FShaderParametersMetadataBuilder& OutBuilder) const
{
	for (auto& Input : InputParams)
	{
		ensureAlways(Input.DimType == EShaderFundamentalDimensionType::Scalar);

		switch (Input.FundamentalType)
		{
		case EShaderFundamentalType::Bool:
			OutBuilder.AddParam<bool>(*Input.Name);
			break;

		case EShaderFundamentalType::Int:
			OutBuilder.AddParam<int32>(*Input.Name);
			break;

		case EShaderFundamentalType::Uint:
			OutBuilder.AddParam<uint32>(*Input.Name);
			break;

		case EShaderFundamentalType::Float:
			OutBuilder.AddParam<float>(*Input.Name);
			break;
		}
	}
}
