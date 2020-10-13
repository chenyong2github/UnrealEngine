// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"

namespace NiagaraDebugShaders
{
	NIAGARASHADER_API void VisualizeTexture(
		FRHICommandList& RHICmdList,
		const FIntPoint& Location, const int32& DisplayHeight, const FIntPoint& RenderTargetSize,
		const FIntVector4& AttributesToVisualize, FRHITexture* Texture, const FIntPoint& NumTextureAttributes, uint32 TickCounter
	);
}
