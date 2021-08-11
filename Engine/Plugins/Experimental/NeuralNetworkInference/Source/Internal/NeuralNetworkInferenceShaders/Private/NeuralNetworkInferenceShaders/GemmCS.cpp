// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceShaders/GemmCS.h"



/* FGemmCS public functions
 *****************************************************************************/

void FGemmCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), THREADGROUP_SIZE_X);
	OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), THREADGROUP_SIZE_Y);
}



const uint32 FGemmCS::THREADGROUP_SIZE_X = 128;
const uint32 FGemmCS::THREADGROUP_SIZE_Y = 1;



/* Shader implementation
 *****************************************************************************/

IMPLEMENT_SHADER_TYPE(, FGemmCS, TEXT("/Plugins/NeuralNetworkInference/Private/GemmOperator.usf"), TEXT("GemmCS"), SF_Compute) // Path defined in NeuralNetworkInferenceShadersModule.cpp
