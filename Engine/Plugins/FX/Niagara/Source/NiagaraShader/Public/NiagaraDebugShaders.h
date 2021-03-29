// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"

struct FScreenPassRenderTarget;
namespace NiagaraDebugShaders
{
	NIAGARASHADER_API void ClearUAV(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* UAV, FUintVector4 ClearValues, uint32 UIntsToSet);

	NIAGARASHADER_API void DrawDebugLines(
		class FRDGBuilder& GraphBuilder, const class FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth,
		const uint32 LineInstanceCount, const FShaderResourceViewRHIRef& LineBuffer
	);

	NIAGARASHADER_API void DrawDebugLines(
		class FRDGBuilder& GraphBuilder, const class FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth,
		const FVertexBufferRHIRef& ArgsBuffer, const FShaderResourceViewRHIRef& LineBuffer
	);

	NIAGARASHADER_API void VisualizeTexture(
		class FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassRenderTarget& Output,
		const FIntPoint& Location, const int32& DisplayHeight,
		const FIntVector4& AttributesToVisualize, FRHITexture* Texture, const FIntVector4& NumTextureAttributes, uint32 TickCounter,
		const FVector2D& PreviewDisplayRange = FVector2D::ZeroVector
	);
}
