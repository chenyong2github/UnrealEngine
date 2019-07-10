// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	//Public function for executing the generate mips compute shader 
	//Default sampler is always bilinear clamp
	static void Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* InTexture,
		const FGenerateMipsParams& InParams = FGenerateMipsParams());

private:
	static void Compute(FRHICommandListImmediate& RHIImmCmdList, FRHITexture* InTexture);
	static FGenerateMipsStruct* SetupTexture(FRHITexture* InTexture,
		const FGenerateMipsParams& InParams = FGenerateMipsParams());
};