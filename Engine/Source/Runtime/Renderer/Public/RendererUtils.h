// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RendererInterface.h"

class FRenderTargetWriteMask
{
public:
	template <uint32 NumRenderTargets>
	static void Decode(FRHICommandListImmediate& RHICmdList, TShaderMap<FGlobalShaderType>* ShaderMap, IPooledRenderTarget* InRenderTargets[NumRenderTargets], TRefCountPtr<IPooledRenderTarget>& OutRTWriteMask, uint32 RTWriteMaskFastVRamConfig, const TCHAR* RTWriteMaskDebugName);
};