// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MultiGPU.h"

class FRHITexture;
class FRHICommandListImmediate;

using PathTracingDenoiserFunction = void(FRHICommandListImmediate& RHICmdList, FRHITexture* ColorTex, FRHITexture* AlbedoTex, FRHITexture* NormalTex, FRHITexture* OutputTex, FRHIGPUMask GPUMask);

extern RENDERER_API PathTracingDenoiserFunction* GPathTracingDenoiserFunc;
