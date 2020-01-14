// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "GlobalShader.h"

struct FGenerateMipsStruct;

/** Parameters for generating mip maps */
struct FGenerateMipsParams
{
	ESamplerFilter Filter = SF_Bilinear;
	ESamplerAddressMode AddressU = AM_Clamp;
	ESamplerAddressMode AddressV = AM_Clamp;
	ESamplerAddressMode AddressW = AM_Clamp;
};

class RENDERCORE_API FGenerateMips
{
public:
	/**
	 * Public function for executing the generate mips compute shader 
	 * @param RHICmdList RHI command list to use to schedule generation of mips
	 * @param InTexture Texture to generate mips for
	 * @param InParams Parameters influencing mips generation (Default sampler is always bilinear clamp)
	 * @param ExternalMipsStructCache Shared pointer to provide a location to cache internal parameters and resource needed to generate mips (provide if generation is triggered repeatedly)
	 * @param bAllowRenderBasedGeneration Set to true if generation via classic rendering is allowable (some platforms do not support compute based generation, but some use cases may disallow this)
	 */
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture, TSharedPtr<FGenerateMipsStruct> & ExternalMipsStructCache, const FGenerateMipsParams& InParams = FGenerateMipsParams(), bool bAllowRenderBasedGeneration = false)
	{
		ExecuteInternal(RHICmdList, InTexture, InParams, &ExternalMipsStructCache, bAllowRenderBasedGeneration);
	}
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture, const FGenerateMipsParams& InParams = FGenerateMipsParams(), bool bAllowRenderBasedGeneration = false)
	{
		ExecuteInternal(RHICmdList, InTexture, InParams, nullptr, bAllowRenderBasedGeneration);
	}

	static void Execute(class FRDGBuilder* GraphBuilder, class FRDGTexture* InGraphTexture, FRHISamplerState* InSampler);

private:
	static void ExecuteInternal(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture, const FGenerateMipsParams& InParams, TSharedPtr<FGenerateMipsStruct> * ExternalMipsStructCache, bool bAllowRenderBasedGeneration);

	static void Compute(FRHICommandListImmediate& RHIImmCmdList, FRHITexture* InTexture, TSharedPtr<FGenerateMipsStruct> GenMipsStruct);
	static TSharedPtr<FGenerateMipsStruct> SetupTexture(FRHITexture* InTexture, const FGenerateMipsParams& InParams = FGenerateMipsParams());

	static void RenderMips(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture, const FGenerateMipsParams& InParams, TSharedPtr<FGenerateMipsStruct> * ExternalMipsStructCache);
	static void SetupRendering(FGenerateMipsStruct *GenMipsStruct, FRHITexture* InTexture, const FGenerateMipsParams& InParams);
};