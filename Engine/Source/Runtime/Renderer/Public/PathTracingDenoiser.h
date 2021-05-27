// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FRHITexture2D;
class FRHICommandListImmediate;

using PathTracingDenoiserFunction = void(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ColorTex, FRHITexture2D* AlbedoTex, FRHITexture2D* NormalTex, FRHITexture2D* OutputTex);

extern RENDERER_API PathTracingDenoiserFunction* GPathTracingDenoiserFunc;
