// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"

struct FScreenPassRenderTarget;
namespace NiagaraDebugShaders
{
	NIAGARASHADER_API void VisualizeTexture(
		class FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassRenderTarget& Output,
		const FIntPoint& Location, const int32& DisplayHeight,
		const FIntVector4& AttributesToVisualize, FRHITexture* Texture, const FIntPoint& NumTextureAttributes, uint32 TickCounter
	);
}
