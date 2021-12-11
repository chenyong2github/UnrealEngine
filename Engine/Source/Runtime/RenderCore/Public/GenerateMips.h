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
	AutoDetect,
	Compute,
	Raster
};

class RENDERCORE_API FGenerateMips
{
public:
	static bool WillFormatSupportCompute(EPixelFormat InPixelFormat);

	/** (ES3.1+) Generates mips for the requested RHI texture using the feature-level appropriate means (Compute, Raster, or Fixed-Function). */
	static void Execute(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef Texture,
		FGenerateMipsParams Params = {},
		EGenerateMipsPass Pass = EGenerateMipsPass::AutoDetect);

	/** (SM5+) Generates mips for the requested RDG texture using the requested compute / raster pass. */
	static void Execute(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef Texture,
		FRHISamplerState* Sampler,
		EGenerateMipsPass Pass = EGenerateMipsPass::AutoDetect);

	static void ExecuteCompute(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FRHISamplerState* Sampler);
	
	/** (SM5+) Generate mips for the requested RDG texture using the compute pass conditionally.
		if( uint(ConditionBuffer[Offset]) > 0)
			Execute(...)
	*/
	static void ExecuteCompute(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FRHISamplerState* Sampler,
								 FRDGBufferRef ConditionBuffer, uint32 Offset = 0);

	static void ExecuteRaster(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FRHISamplerState* Sampler);

	//////////////////////////////////////////////////////////////////////////
	UE_DEPRECATED(4.26, "Please use the FRDGBuilder version of this function instead.")
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, TSharedPtr<FGenerateMipsStruct>& GenerateMipsStruct, FGenerateMipsParams Params = {}, bool bAllowRenderBasedGeneration = false);

	UE_DEPRECATED(4.26, "Please use the FRDGBuilder version of this function instead.")
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FGenerateMipsParams Params = {}, bool bAllowRenderBasedGeneration = false);
	//////////////////////////////////////////////////////////////////////////
};