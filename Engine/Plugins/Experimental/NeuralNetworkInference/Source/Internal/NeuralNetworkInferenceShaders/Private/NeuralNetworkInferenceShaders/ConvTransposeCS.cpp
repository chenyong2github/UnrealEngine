// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceShaders/ConvTransposeCS.h"



/* FConvTransposeCS static members
 *****************************************************************************/

const uint32 FConvTransposeCS::THREADGROUP_SIZE_X(128);



/* FConvTransposeCS public functions
 *****************************************************************************/

void FConvTransposeCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), THREADGROUP_SIZE_X);
}



/* Shader implementation
 *****************************************************************************/

IMPLEMENT_SHADER_TYPE(, FConvTransposeCS, TEXT("/Plugins/NeuralNetworkInference/Private/ConvTransposeOperator.usf"), TEXT("XToXWithZerosCS"), SF_Compute) // Path defined in NeuralNetworkInferenceShadersModule.cpp
