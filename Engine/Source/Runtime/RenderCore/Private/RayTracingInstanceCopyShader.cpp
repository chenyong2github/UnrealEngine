// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingInstanceCopyShader.h"
#include "ShaderParameterUtils.h"
#include "RendererInterface.h"

IMPLEMENT_SHADER_TYPE3(RayTracingInstanceCopyCS, SF_Compute);

bool RayTracingInstanceCopyCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return (ShouldCompileRayTracingShadersForProject(Parameters.Platform) && RHISupportsComputeShaders(Parameters.Platform) && !(Parameters.Platform == EShaderPlatform::SP_METAL || Parameters.Platform == EShaderPlatform::SP_METAL_TVOS || IsMobilePlatform(Parameters.Platform)));
}