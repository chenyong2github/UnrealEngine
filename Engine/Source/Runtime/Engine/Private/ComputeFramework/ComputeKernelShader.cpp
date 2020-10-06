// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelShader.h"
#include "ComputeFramework/ComputeKernelShaderType.h"

//IMPLEMENT_SHADER_TYPE(, FComputeKernelShader, TEXT("/Engine/Private/ComputeKernel.usf"), TEXT("Main"), SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FComputeKernelShader, "/Engine/Private/ComputeKernel.usf", "Main", SF_Compute);
