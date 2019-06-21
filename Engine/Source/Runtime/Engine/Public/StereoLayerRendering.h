// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ScreenRendering.h: Screen rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

/**
 * A vertex shader for rendering a transformed textured element.
 */
class FStereoLayerVS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FStereoLayerVS,Global,ENGINE_API);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	FStereoLayerVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		InQuadAdjust.Bind(Initializer.ParameterMap, TEXT("InQuadAdjust"));
		InUVAdjust.Bind(Initializer.ParameterMap, TEXT("InUVAdjust"));
		InViewProjection.Bind(Initializer.ParameterMap, TEXT("InViewProjection"));
		InWorld.Bind(Initializer.ParameterMap, TEXT("InWorld"));
	}
	FStereoLayerVS() {}

	void SetParameters(FRHICommandList& RHICmdList, FVector2D QuadSize, FBox2D UVRect, const FMatrix& ViewProjection, const FMatrix& World)
	{
		FRHIVertexShader* VS = GetVertexShader();

		if (InQuadAdjust.IsBound())
		{
			SetShaderValue(RHICmdList, VS, InQuadAdjust, QuadSize);
		}

		if (InUVAdjust.IsBound())
		{
			FVector4 UVAdjust;
			UVAdjust.X = UVRect.Min.X;
			UVAdjust.Y = UVRect.Min.Y;
			UVAdjust.Z = UVRect.Max.X - UVRect.Min.X;
			UVAdjust.W = UVRect.Max.Y - UVRect.Min.Y;
			SetShaderValue(RHICmdList, VS, InUVAdjust, UVAdjust);
		}

		if (InViewProjection.IsBound())
		{
			SetShaderValue(RHICmdList, VS, InViewProjection, ViewProjection);
		}

		if (InWorld.IsBound())
		{
			SetShaderValue(RHICmdList, VS, InWorld, World);
		}
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InQuadAdjust;
		Ar << InUVAdjust;
		Ar << InViewProjection;
		Ar << InWorld;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter InQuadAdjust;
	FShaderParameter InUVAdjust;
	FShaderParameter InViewProjection;
	FShaderParameter InWorld;
};

class FStereoLayerPS_Base : public FGlobalShader
{
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	void SetParameters(FRHICommandList& RHICmdList, FRHISamplerState* SamplerStateRHI, FRHITexture* TextureRHI)
	{
		FRHIPixelShader* PS = GetPixelShader();

		SetTextureParameter(RHICmdList, PS, InTexture, InTextureSampler, SamplerStateRHI, TextureRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InTexture;
		Ar << InTextureSampler;
		return bShaderHasOutdatedParameters;
	}

protected:
	FShaderResourceParameter InTexture;
	FShaderResourceParameter InTextureSampler;

	FStereoLayerPS_Base(const ShaderMetaType::CompiledShaderInitializerType& Initializer, const TCHAR* TextureParamName) :
		FGlobalShader(Initializer) 
	{
		InTexture.Bind(Initializer.ParameterMap, TextureParamName, SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
	}
	FStereoLayerPS_Base() {}

};

/**
 * A pixel shader for rendering a transformed textured element.
 */
class FStereoLayerPS : public FStereoLayerPS_Base
{
	DECLARE_EXPORTED_SHADER_TYPE(FStereoLayerPS,Global,ENGINE_API);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	FStereoLayerPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FStereoLayerPS_Base(Initializer, TEXT("InTexture"))	{}
	FStereoLayerPS() {}
};

/**
 * A pixel shader for rendering a transformed external texture element.
 */
class FStereoLayerPS_External : public FStereoLayerPS_Base
{
	DECLARE_EXPORTED_SHADER_TYPE(FStereoLayerPS_External, Global, ENGINE_API);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	FStereoLayerPS_External(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FStereoLayerPS_Base(Initializer, TEXT("InExternalTexture")) {}
	FStereoLayerPS_External() {}
};
