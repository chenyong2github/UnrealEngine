// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FRHITexture;
class FRHICommandListImmediate;

using PathTracingDenoiserFunction = void(FRHICommandListImmediate& RHICmdList, FRHITexture* ColorTex, FRHITexture* AlbedoTex, FRHITexture* NormalTex, FRHITexture* OutputTex, FRHIGPUMask GPUMask);

extern RENDERER_API PathTracingDenoiserFunction* GPathTracingDenoiserFunc;
