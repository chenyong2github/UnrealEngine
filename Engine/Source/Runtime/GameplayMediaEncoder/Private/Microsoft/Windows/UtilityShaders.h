// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UtilityShaders.h: Screen rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

#include "GameplayMediaEncoderCommon.h"

/**
* A pixel shader for rendering a textured screen element that converts color channels from RGBA to BGRA.
*/
class FScreenSwizzlePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FScreenSwizzlePS, Global);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	FScreenSwizzlePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"), SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
	}
	FScreenSwizzlePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FTexture* Texture)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), InTexture, InTextureSampler, Texture);
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHISamplerState* SamplerStateRHI, FRHITexture* TextureRHI)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), InTexture, InTextureSampler, SamplerStateRHI, TextureRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InTexture;
		Ar << InTextureSampler;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter InTexture;
	FShaderResourceParameter InTextureSampler;
};

