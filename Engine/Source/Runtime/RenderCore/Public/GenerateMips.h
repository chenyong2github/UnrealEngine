// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"

struct FGenerateMipsStruct;

struct FGenerateMipsParams
{
	ESamplerFilter Filter = SF_Bilinear;
	ESamplerAddressMode AddressU = AM_Clamp;
	ESamplerAddressMode AddressV = AM_Clamp;
	ESamplerAddressMode AddressW = AM_Clamp;
};

enum class EGenerateMipsPass
{
	Compute,
	Raster
};

class RENDERCORE_API FGenerateMips
{
public:
	/** (ES3.1+) Generates mips for the requested RHI texture using the feature-level appropriate means (Compute, Raster, or Fixed-Function). */
	static void Execute(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef Texture,
		FGenerateMipsParams Params = {},
		EGenerateMipsPass Pass = EGenerateMipsPass::Compute);

	/** (SM5+) Generates mips for the requested RDG texture using the requested compute / raster pass. */
	static void Execute(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef Texture,
		FRHISamplerState* Sampler,
		EGenerateMipsPass Pass = EGenerateMipsPass::Compute)
	{
		if (Pass == EGenerateMipsPass::Compute)
		{
			ExecuteCompute(GraphBuilder, Texture, Sampler);
		}
		else
		{
			ExecuteRaster(GraphBuilder, Texture, Sampler);
		}
	}

	static void ExecuteCompute(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FRHISamplerState* Sampler);
	static void ExecuteRaster(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FRHISamplerState* Sampler);

	//////////////////////////////////////////////////////////////////////////
	UE_DEPRECATED(4.26, "Please use the FRDGBuilder version of this function instead.")
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, TSharedPtr<FGenerateMipsStruct>& GenerateMipsStruct, FGenerateMipsParams Params = {}, bool bAllowRenderBasedGeneration = false);

	UE_DEPRECATED(4.26, "Please use the FRDGBuilder version of this function instead.")
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FGenerateMipsParams Params = {}, bool bAllowRenderBasedGeneration = false);
	//////////////////////////////////////////////////////////////////////////
};