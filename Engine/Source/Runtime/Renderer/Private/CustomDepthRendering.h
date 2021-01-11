// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"

enum class ECustomDepthPassLocation : uint32
{
	// Renders custom depth before the base pass. Can be more efficient with AsyncCompute and enables use with DBuffer decals.
	BeforeBasePass,

	// Renders after the base pass.
	AfterBasePass
};

// Returns the location in the frame where custom depth is rendered.
extern RENDERER_API ECustomDepthPassLocation GetCustomDepthPassLocation();

enum class ECustomDepthMode : uint32
{
	// Custom depth is disabled.
	Disabled,

	// Custom depth is enabled.
	Enabled,

	// Custom depth is enabled and uses stencil.
	EnabledWithStencil,
};

// The custom depth mode currently configured.
extern RENDERER_API ECustomDepthMode GetCustomDepthMode();

// Whether custom depth passes should write stencil.
extern RENDERER_API bool IsCustomDepthPassWritingStencil(ERHIFeatureLevel::Type InFeatureLevel);

// Returns the requested downsample factor for custom depth textures.
extern RENDERER_API uint32 GetCustomDepthDownsampleFactor(ERHIFeatureLevel::Type InFeatureLevel);

inline bool IsCustomDepthPassEnabled()
{
	return GetCustomDepthMode() != ECustomDepthMode::Disabled;
}

struct RENDERER_API FCustomDepthTextures
{
	static FCustomDepthTextures Create(FRDGBuilder& GraphBuilder, FIntPoint Extent, ERHIFeatureLevel::Type FeatureLevel, uint32 DownsampleFactor);

	bool IsValid() const
	{
		return Depth != nullptr;
	}

	FRDGTextureRef Depth{};
	FRDGTextureSRVRef Stencil{};

	FRDGTextureRef MobileDepth{};
	FRDGTextureRef MobileStencil{};

	// Actions to use when initially rendering to custom depth / stencil.
	ERenderTargetLoadAction DepthAction = ERenderTargetLoadAction::EClear;
	ERenderTargetLoadAction StencilAction = ERenderTargetLoadAction::EClear;

	uint32 DownsampleFactor = 1;
};