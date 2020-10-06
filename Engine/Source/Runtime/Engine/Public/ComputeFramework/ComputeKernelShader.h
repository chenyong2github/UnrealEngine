// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Shader.h"
#include "ShaderParameterStruct.h"
#include "ComputeFramework/ComputeKernelShaderMap.h"

#include "GlobalShader.h"

class FComputeKernelShader : public FGlobalShader /*public FShader*/
{
public:
	DECLARE_SHADER_TYPE(FComputeKernelShader, Global);
	SHADER_USE_PARAMETER_STRUCT(FComputeKernelShader, FGlobalShader);
	//DECLARE_SHADER_TYPE(FComputeKernelShader, ComputeKernel);
	//SHADER_USE_PARAMETER_STRUCT(FComputeKernelShader, FShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()
};
