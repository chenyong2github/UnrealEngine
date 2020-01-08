// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RendererInterface.h"
#include "RHICommandList.h"

struct FGpuDebugPrimitiveBuffers
{
	TRefCountPtr<IPooledRenderTarget> DebugPrimitiveCountTexture;
	TRefCountPtr<IPooledRenderTarget> DebugPrimitiveCountStagingTexture;

	TRefCountPtr<IPooledRenderTarget> DebugPrimitiveTexture;
	TRefCountPtr<IPooledRenderTarget> DebugPrimitiveStagingTexture;
};

FGpuDebugPrimitiveBuffers AllocateGpuDebugPrimitiveBuffers(FRHICommandListImmediate& RHICmdList);
void BindGpuDebugPrimitiveBuffers(FRHIRenderPassInfo& RPInfo, FGpuDebugPrimitiveBuffers& DebugPrimitiveBuffer, uint32 UAVIndex = 0);
void DrawGpuDebugPrimitives(FRHICommandListImmediate& Context, TArray<FViewInfo>& Views, FGpuDebugPrimitiveBuffers& DebugPrimitiveBuffer);