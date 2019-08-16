// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	Texture2DPreview.h: Implementation for previewing 2D Textures and normal maps.
==============================================================================*/

#include "Texture2DPreview.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "SimpleElementShaders.h"
#include "ShaderParameterUtils.h"
#include "PipelineStateCache.h"
#include "TextureResource.h"
#include "RenderCore.h"
#include "VirtualTexturing.h"
#include "Engine/Texture2DArray.h"
/*------------------------------------------------------------------------------
	Batched element shaders for previewing 2d textures.
------------------------------------------------------------------------------*/
/**
 * Simple pixel shader for previewing 2d textures at a specified mip level
 */
namespace
{
	class FTexture2DPreviewVirtualTexture : SHADER_PERMUTATION_BOOL("SAMPLE_VIRTUAL_TEXTURE");
	class FTexture2DPreviewTexture2DArray : SHADER_PERMUTATION_BOOL("TEXTURE_ARRAY");
}


class FSimpleElementTexture2DPreviewPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSimpleElementTexture2DPreviewPS);

	using FPermutationDomain = TShaderPermutationDomain<FTexture2DPreviewVirtualTexture, FTexture2DPreviewTexture2DArray>;

public:

	FSimpleElementTexture2DPreviewPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap,TEXT("InTexture"), SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap,TEXT("InTextureSampler"));
		InPageTableTexture0.Bind(Initializer.ParameterMap, TEXT("InPageTableTexture0"));
		InPageTableTexture1.Bind(Initializer.ParameterMap, TEXT("InPageTableTexture1"));
		VTPackedPageTableUniform.Bind(Initializer.ParameterMap, TEXT("VTPackedPageTableUniform"));
		VTPackedUniform.Bind(Initializer.ParameterMap, TEXT("VTPackedUniform"));
		TextureComponentReplicate.Bind(Initializer.ParameterMap,TEXT("TextureComponentReplicate"));
		TextureComponentReplicateAlpha.Bind(Initializer.ParameterMap,TEXT("TextureComponentReplicateAlpha"));
		ColorWeights.Bind(Initializer.ParameterMap,TEXT("ColorWeights"));
		PackedParameters.Bind(Initializer.ParameterMap,TEXT("PackedParams"));
		NumSlices.Bind(Initializer.ParameterMap, TEXT("NumSlices"));
	}
	FSimpleElementTexture2DPreviewPS() {}

	/** Should the shader be cached? Always. */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) == false)
		{
			return false;
		}
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform);
	}

	void SetParameters(FRHICommandList& RHICmdList, const FTexture* TextureValue, const FMatrix& ColorWeightsValue, float GammaValue, float MipLevel, float LayerIndex, bool bIsNormalMap, bool bIsVirtualTexture, bool bIsTextureArray)
	{
		if (bIsVirtualTexture)
		{
			FVirtualTexture2DResource* VirtualTextureValue = (FVirtualTexture2DResource*)TextureValue;
			IAllocatedVirtualTexture* AllocatedVT = VirtualTextureValue->AcquireAllocatedVT();
	
			FRHIShaderResourceView* PhysicalView = AllocatedVT->GetPhysicalTextureView((uint32)LayerIndex, TextureValue->bSRGB);
			SetSRVParameter(RHICmdList, GetPixelShader(), InTexture, PhysicalView);
			SetSamplerParameter(RHICmdList, GetPixelShader(), InTextureSampler, VirtualTextureValue->SamplerStateRHI);

			SetTextureParameter(RHICmdList, GetPixelShader(), InPageTableTexture0, AllocatedVT->GetPageTableTexture(0u));
			if (AllocatedVT->GetNumPageTableTextures() > 1u)
			{
				SetTextureParameter(RHICmdList, GetPixelShader(), InPageTableTexture1, AllocatedVT->GetPageTableTexture(1u));
			}
			else
			{
				SetTextureParameter(RHICmdList, GetPixelShader(), InPageTableTexture1, GBlackTexture->TextureRHI);
			}

			FUintVector4 PageTableUniform[2];
			FUintVector4 Uniform;

			AllocatedVT->GetPackedPageTableUniform(PageTableUniform, false);
			AllocatedVT->GetPackedUniform(&Uniform, (uint32)LayerIndex);

			SetShaderValueArray(RHICmdList, GetPixelShader(), VTPackedPageTableUniform, PageTableUniform, ARRAY_COUNT(PageTableUniform));
			SetShaderValue(RHICmdList, GetPixelShader(), VTPackedUniform, Uniform);
		}
		else
		{
			SetTextureParameter(RHICmdList, GetPixelShader(), InPageTableTexture0, GBlackTexture->TextureRHI);
			SetTextureParameter(RHICmdList, GetPixelShader(), InPageTableTexture1, GBlackTexture->TextureRHI);
			SetTextureParameter(RHICmdList, GetPixelShader(), InTexture, InTextureSampler, TextureValue);
		}
		
		SetShaderValue(RHICmdList, GetPixelShader(),ColorWeights,ColorWeightsValue);
		FVector4 PackedParametersValue(GammaValue, MipLevel, bIsNormalMap ? 1.0 : -1.0f, LayerIndex);
		SetShaderValue(RHICmdList, GetPixelShader(), PackedParameters, PackedParametersValue);

		// Store slice count for texture array
		if (bIsTextureArray)
		{
			const FTexture2DArrayResource* TextureValue2DArray = (FTexture2DArrayResource*)(TextureValue);
			float NumSlicesData = TextureValue2DArray ? float(TextureValue2DArray->GetNumSlices()) : 1;
			SetShaderValue(RHICmdList, GetPixelShader(), NumSlices, NumSlicesData);
		}

		SetShaderValue(RHICmdList, GetPixelShader(),TextureComponentReplicate,TextureValue->bGreyScaleFormat ? FLinearColor(1,0,0,0) : FLinearColor(0,0,0,0));
		SetShaderValue(RHICmdList, GetPixelShader(),TextureComponentReplicateAlpha,TextureValue->bGreyScaleFormat ? FLinearColor(1,0,0,0) : FLinearColor(0,0,0,1));
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InTexture;
		Ar << InTextureSampler;
		Ar << InPageTableTexture0;
		Ar << InPageTableTexture1;
		Ar << VTPackedPageTableUniform;
		Ar << VTPackedUniform;
		Ar << TextureComponentReplicate;
		Ar << TextureComponentReplicateAlpha;
		Ar << ColorWeights;
		Ar << NumSlices;
		Ar << PackedParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter InTexture;			// if VT, this used as the physical texture
	FShaderResourceParameter InTextureSampler;	// if VT, this is the physical sampler
	FShaderResourceParameter InPageTableTexture0;
	FShaderResourceParameter InPageTableTexture1;
	FShaderParameter VTPackedPageTableUniform;
	FShaderParameter VTPackedUniform;
	FShaderParameter TextureComponentReplicate;
	FShaderParameter TextureComponentReplicateAlpha;
	FShaderParameter ColorWeights; 
	FShaderParameter PackedParameters;
	FShaderParameter NumSlices;
};

IMPLEMENT_GLOBAL_SHADER(FSimpleElementTexture2DPreviewPS, "/Engine/Private/SimpleElementTexture2DPreviewPixelShader.usf", "Main", SF_Pixel);

/** Binds vertex and pixel shaders for this element */
void FBatchedElementTexture2DPreviewParameters::BindShaders(
	FRHICommandList& RHICmdList,
	FGraphicsPipelineStateInitializer& GraphicsPSOInit,
	ERHIFeatureLevel::Type InFeatureLevel,
	const FMatrix& InTransform,
	const float InGamma,
	const FMatrix& ColorWeights,
	const FTexture* Texture)
{
	TShaderMapRef<FSimpleElementVS> VertexShader(GetGlobalShaderMap(InFeatureLevel));

	FSimpleElementTexture2DPreviewPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FTexture2DPreviewVirtualTexture>(bIsVirtualTexture);
	PermutationVector.Set<FTexture2DPreviewTexture2DArray>(bIsTextureArray);
	TShaderMapRef<FSimpleElementTexture2DPreviewPS> PixelShader(GetGlobalShaderMap(InFeatureLevel), PermutationVector);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GSimpleElementVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	if (bIsSingleChannelFormat)
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	}

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, EApplyRendertargetOption::ForceApply);

	VertexShader->SetParameters(RHICmdList, InTransform);
	PixelShader->SetParameters(RHICmdList, Texture, ColorWeights, InGamma, MipLevel, LayerIndex, bIsNormalMap, bIsVirtualTexture, bIsTextureArray);
}
