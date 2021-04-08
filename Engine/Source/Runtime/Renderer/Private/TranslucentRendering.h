// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HitProxies.h"
#include "ShaderParameters.h"
#include "GlobalShader.h"
#include "SceneRendering.h"
#include "VolumeRendering.h"
#include "ScreenPass.h"

struct FSeparateTranslucencyDimensions
{
	inline FScreenPassTextureViewport GetViewport(FIntRect ViewRect) const
	{
		return FScreenPassTextureViewport(Extent, GetScaledRect(ViewRect, Scale));
	}

	FScreenPassTextureViewport GetInstancedStereoViewport(const FViewInfo& View, float InstancedStereoWidth) const;

	// Extent of the separate translucency targets, if downsampled.
	FIntPoint Extent = FIntPoint::ZeroValue;

	// Amount the view rects should be scaled to match the new separate translucency extent.
	float Scale = 1.0f;

	// The number of MSAA samples to use when creating separate translucency textures.
	uint32 NumSamples = 1;
};

class FSeparateTranslucencyTextures
{
public:
	FSeparateTranslucencyTextures(FSeparateTranslucencyDimensions InDimensions)
		: Dimensions(InDimensions)
	{}

	bool IsColorValid() const
	{
		return ColorTexture.IsValid();
	}

	FRDGTextureMSAA GetColorForWrite(FRDGBuilder& GraphBuilder);
	FRDGTextureRef  GetColorForRead(FRDGBuilder& GraphBuilder) const;

	bool IsColorModulateValid() const
	{
		return ColorModulateTexture.IsValid();
	}

	FRDGTextureMSAA GetColorModulateForWrite(FRDGBuilder& GraphBuilder);
	FRDGTextureRef  GetColorModulateForRead(FRDGBuilder& GraphBuilder) const;

	FRDGTextureMSAA GetDepthForWrite(FRDGBuilder& GraphBuilder);
	FRDGTextureRef  GetDepthForRead(FRDGBuilder& GraphBuilder) const;

	FRDGTextureMSAA GetForWrite(FRDGBuilder& GraphBuilder, ETranslucencyPass::Type TranslucencyPass);

	const FSeparateTranslucencyDimensions& GetDimensions() const
	{
		return Dimensions;
	}

private:
	FSeparateTranslucencyDimensions Dimensions;
	FRDGTextureMSAA ColorTexture;
	FRDGTextureMSAA ColorModulateTexture;
	FRDGTextureMSAA DepthTexture;
};

DECLARE_GPU_DRAWCALL_STAT_EXTERN(Translucency);

/** Creates separate translucency textures. */
FSeparateTranslucencyTextures CreateSeparateTranslucencyTextures(FRDGBuilder& GraphBuilder, FSeparateTranslucencyDimensions Dimensions);

/** Converts the the translucency pass into the respective mesh pass. */
EMeshPass::Type TranslucencyPassToMeshPass(ETranslucencyPass::Type TranslucencyPass);

/** Returns the translucency views to render for the requested view. */
ETranslucencyView GetTranslucencyView(const FViewInfo& View);

/** Returns the union of all translucency views to render. */
ETranslucencyView GetTranslucencyViews(TArrayView<const FViewInfo> Views);

/** Call once per frame to update GPU timers for stats and dynamic resolution scaling. */
FSeparateTranslucencyDimensions UpdateTranslucencyTimers(FRHICommandListImmediate& RHICmdList, TArrayView<const FViewInfo> Views);

/** Returns whether the view family is requesting to render translucency. */
bool ShouldRenderTranslucency(const FSceneViewFamily& ViewFamily);