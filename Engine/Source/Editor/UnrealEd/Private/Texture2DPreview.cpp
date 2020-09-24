// Copyright Epic Games, Inc. All Rights Reserved.

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
	DECLARE_SHADER_TYPE(FSimpleElementTexture2DPreviewPS, Global);
public:
	using FPermutationDomain = TShaderPermutationDomain<FTexture2DPreviewVirtualTexture, FTexture2DPreviewTexture2DArray>;

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

	void SetParameters(FRHICommandList& RHICmdList, const FTexture* TextureValue, const FMatrix& ColorWeightsValue, float GammaValue, float MipLevel, float LayerIndex, bool bIsNormalMap, bool bIsSingleVTPhysicalSpace, bool bIsVirtualTexture, bool bIsTextureArray)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		if (bIsVirtualTexture)
		{
			FVirtualTexture2DResource* VirtualTextureValue = (FVirtualTexture2DResource*)TextureValue;
			IAllocatedVirtualTexture* AllocatedVT = VirtualTextureValue->AcquireAllocatedVT();
	
			FRHIShaderResourceView* PhysicalView = AllocatedVT->GetPhysicalTextureSRV((uint32)LayerIndex, TextureValue->bSRGB);
			SetSRVParameter(RHICmdList, ShaderRHI, InTexture, PhysicalView);
			SetSamplerParameter(RHICmdList, ShaderRHI, InTextureSampler, VirtualTextureValue->SamplerStateRHI);

			SetTextureParameter(RHICmdList, ShaderRHI, InPageTableTexture0, AllocatedVT->GetPageTableTexture(0u));
			if (AllocatedVT->GetNumPageTableTextures() > 1u)
			{
				SetTextureParameter(RHICmdList, ShaderRHI, InPageTableTexture1, AllocatedVT->GetPageTableTexture(1u));
			}
			else
			{
				SetTextureParameter(RHICmdList, ShaderRHI, InPageTableTexture1, GBlackTexture->TextureRHI);
			}

			FUintVector4 PageTableUniform[2];
			FUintVector4 Uniform;

			AllocatedVT->GetPackedPageTableUniform(PageTableUniform);
			AllocatedVT->GetPackedUniform(&Uniform, (uint32)LayerIndex);

			SetShaderValueArray(RHICmdList, ShaderRHI, VTPackedPageTableUniform, PageTableUniform, UE_ARRAY_COUNT(PageTableUniform));
			SetShaderValue(RHICmdList, ShaderRHI, VTPackedUniform, Uniform);
		}
		else
		{
			SetTextureParameter(RHICmdList, ShaderRHI, InPageTableTexture0, GBlackTexture->TextureRHI);
			SetTextureParameter(RHICmdList, ShaderRHI, InPageTableTexture1, GBlackTexture->TextureRHI);
			SetTextureParameter(RHICmdList, ShaderRHI, InTexture, InTextureSampler, TextureValue);
		}
		
		SetShaderValue(RHICmdList, ShaderRHI,ColorWeights,ColorWeightsValue);
		FVector4 PackedParametersValue(GammaValue, MipLevel, bIsNormalMap ? 1.0 : -1.0f, bIsSingleVTPhysicalSpace ? 0 : LayerIndex);
		SetShaderValue(RHICmdList, ShaderRHI, PackedParameters, PackedParametersValue);

		// Store slice count for texture array
		if (bIsTextureArray)
		{
			const float NumSlicesData = TextureValue ? TextureValue->GetSizeZ() : 1;
			SetShaderValue(RHICmdList, ShaderRHI, NumSlices, NumSlicesData);
		}

		SetShaderValue(RHICmdList, ShaderRHI, TextureComponentReplicate, (TextureValue && TextureValue->bGreyScaleFormat) ? FLinearColor(1,0,0,0) : FLinearColor(0,0,0,0));
		SetShaderValue(RHICmdList, ShaderRHI, TextureComponentReplicateAlpha, (TextureValue && TextureValue->bGreyScaleFormat) ? FLinearColor(1,0,0,0) : FLinearColor(0,0,0,1));
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture);			// if VT, this used as the physical texture
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler);	// if VT, this is the physical sampler
	LAYOUT_FIELD(FShaderResourceParameter, InPageTableTexture0);
	LAYOUT_FIELD(FShaderResourceParameter, InPageTableTexture1);
	LAYOUT_FIELD(FShaderParameter, VTPackedPageTableUniform);
	LAYOUT_FIELD(FShaderParameter, VTPackedUniform);
	LAYOUT_FIELD(FShaderParameter, TextureComponentReplicate);
	LAYOUT_FIELD(FShaderParameter, TextureComponentReplicateAlpha);
	LAYOUT_FIELD(FShaderParameter, ColorWeights);
	LAYOUT_FIELD(FShaderParameter, PackedParameters);
	LAYOUT_FIELD(FShaderParameter, NumSlices);
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
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	if (bIsSingleChannelFormat)
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	}

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, EApplyRendertargetOption::ForceApply);

	VertexShader->SetParameters(RHICmdList, InTransform);
	PixelShader->SetParameters(RHICmdList, Texture, ColorWeights, InGamma, MipLevel, LayerIndex, bIsNormalMap, bIsSingleVTPhysicalSpace, bIsVirtualTexture, bIsTextureArray);
}
