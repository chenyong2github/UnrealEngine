// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessAmbient.cpp: Post processing ambient implementation.
=============================================================================*/

#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "AmbientCubemapParameters.h"
#include "PipelineStateCache.h"

/*-----------------------------------------------------------------------------
FCubemapShaderParameters
-----------------------------------------------------------------------------*/

void FCubemapShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	AmbientCubemapColor.Bind(ParameterMap, TEXT("AmbientCubemapColor"));
	AmbientCubemapMipAdjust.Bind(ParameterMap, TEXT("AmbientCubemapMipAdjust"));
	AmbientCubemap.Bind(ParameterMap, TEXT("AmbientCubemap"));
	AmbientCubemapSampler.Bind(ParameterMap, TEXT("AmbientCubemapSampler"));
}

void FCubemapShaderParameters::SetParameters(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const
{
	SetParametersTemplate(RHICmdList, ShaderRHI, Entry);
}

void FCubemapShaderParameters::SetParameters(FRHICommandList& RHICmdList, FRHIComputeShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const
{
	SetParametersTemplate(RHICmdList, ShaderRHI, Entry);
}

template<typename TRHIShader>
void FCubemapShaderParameters::SetParametersTemplate(FRHICommandList& RHICmdList, TRHIShader* ShaderRHI, const FFinalPostProcessSettings::FCubemapEntry& Entry) const
{
	FAmbientCubemapParameters ShaderParameters;
	SetupAmbientCubemapParameters(Entry, &ShaderParameters);

	// floats to render the cubemap
	{
		SetShaderValue(RHICmdList, ShaderRHI, AmbientCubemapColor, ShaderParameters.AmbientCubemapColor);
		SetShaderValue(RHICmdList, ShaderRHI, AmbientCubemapMipAdjust, ShaderParameters.AmbientCubemapMipAdjust);
	}

	// cubemap texture
	{
		FTexture* InternalSpec = Entry.AmbientCubemap ? Entry.AmbientCubemap->Resource : GBlackTextureCube;

		SetTextureParameter(RHICmdList, ShaderRHI, AmbientCubemap, AmbientCubemapSampler, InternalSpec);
	}
}

FArchive& operator<<(FArchive& Ar, FCubemapShaderParameters& P)
{
	Ar << P.AmbientCubemapColor << P.AmbientCubemap << P.AmbientCubemapSampler << P.AmbientCubemapMipAdjust;

	return Ar;
}

void SetupAmbientCubemapParameters(const FFinalPostProcessSettings::FCubemapEntry& Entry, FAmbientCubemapParameters* OutParameters)
{
	// floats to render the cubemap
	{
		float MipCount = 0;

		if(Entry.AmbientCubemap)
		{
			int32 CubemapWidth = Entry.AmbientCubemap->GetSurfaceWidth();
			MipCount = FMath::Log2(CubemapWidth) + 1.0f;
		}

		OutParameters->AmbientCubemapColor = Entry.AmbientCubemapTintMulScaleValue;

		OutParameters->AmbientCubemapMipAdjust.X =  1.0f - GDiffuseConvolveMipLevel / MipCount;
		OutParameters->AmbientCubemapMipAdjust.Y = (MipCount - 1.0f) * OutParameters->AmbientCubemapMipAdjust.X;
		OutParameters->AmbientCubemapMipAdjust.Z = MipCount - GDiffuseConvolveMipLevel;
		OutParameters->AmbientCubemapMipAdjust.W = MipCount;
	}

	// cubemap texture
	{
		FTexture* AmbientCubemapTexture = Entry.AmbientCubemap ? Entry.AmbientCubemap->Resource : GBlackTextureCube;
		OutParameters->AmbientCubemap = AmbientCubemapTexture->TextureRHI;
		OutParameters->AmbientCubemapSampler = AmbientCubemapTexture->SamplerStateRHI;
	}
}
