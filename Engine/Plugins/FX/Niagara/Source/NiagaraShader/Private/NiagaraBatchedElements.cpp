// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraBatchedElements.h"
#include "GlobalShader.h"
#include "SimpleElementShaders.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterMacros.h"

//////////////////////////////////////////////////////////////////////////

class FNiagaraSimpleElement2DArrayAttribute : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraSimpleElement2DArrayAttribute);
	SHADER_USE_PARAMETER_STRUCT(FNiagaraSimpleElement2DArrayAttribute, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NIAGARA_2DARRAY_ATTRIBUTE_PS"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector4, InAttributeSlices)
		SHADER_PARAMETER(FMatrix44f, InColorWeights)
		SHADER_PARAMETER(float, InGamma)
		SHADER_PARAMETER_TEXTURE(Texture2D, InTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InTextureSampler)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraSimpleElement2DArrayAttribute, "/Plugin/FX/Niagara/Private/NiagaraBatchedElements.usf", "MainPS", SF_Pixel);

void FBatchedElementNiagara2DArrayAttribute::BindShaders(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, ERHIFeatureLevel::Type InFeatureLevel, const FMatrix& InTransform, const float InGamma, const FMatrix& ColorWeights, const FTexture* Texture)
{
	FRHITexture* RHITexture = nullptr;
	FRHISamplerState* RHISamplerState = nullptr;
	if (GetTextureAndSampler != nullptr)
	{
		GetTextureAndSampler(RHITexture, RHISamplerState);
	}
	else if ( Texture != nullptr )
	{
		RHITexture = Texture->TextureRHI;
		RHISamplerState = Texture->SamplerStateRHI;
	}

	TShaderMapRef<FSimpleElementVS> VertexShader(GetGlobalShaderMap(InFeatureLevel));
	TShaderMapRef<FNiagaraSimpleElement2DArrayAttribute> PixelShader(GetGlobalShaderMap(InFeatureLevel));

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GSimpleElementVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

	VertexShader->SetParameters(RHICmdList, InTransform);

	FNiagaraSimpleElement2DArrayAttribute::FParameters PassParameters;
	PassParameters.InAttributeSlices = AttributeSlices;
	PassParameters.InColorWeights = FMatrix44f(ColorWeights);
	PassParameters.InGamma = InGamma;
	PassParameters.InTexture = RHITexture ? RHITexture : GWhiteTexture->TextureRHI.GetReference();
	PassParameters.InTextureSampler = RHISamplerState ? RHISamplerState : TStaticSamplerState<SF_Bilinear>::GetRHI();
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters);
}

//////////////////////////////////////////////////////////////////////////

class FNiagaraSimpleElementPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraSimpleElementPS);
	SHADER_USE_PARAMETER_STRUCT(FNiagaraSimpleElementPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NIAGARA_SIMPLE_PS"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraSimpleElementPS, "/Plugin/FX/Niagara/Private/NiagaraBatchedElements.usf", "MainPS", SF_Pixel);

void FBatchedElementNiagaraInvertColorChannel::BindShaders(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, ERHIFeatureLevel::Type InFeatureLevel, const FMatrix& InTransform, const float InGamma, const FMatrix& ColorWeights, const FTexture* Texture)
{
	TShaderMapRef<FSimpleElementVS> VertexShader(GetGlobalShaderMap(InFeatureLevel));
	TShaderMapRef<FNiagaraSimpleElementPS> PixelShader(GetGlobalShaderMap(InFeatureLevel));

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GSimpleElementVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_ALPHA, BO_Subtract, BF_One, BF_DestColor, BO_Subtract, BF_One, BF_DestAlpha>::GetRHI();

	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

	VertexShader->SetParameters(RHICmdList, InTransform);

	FNiagaraSimpleElementPS::FParameters PassParameters;
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters);
}
