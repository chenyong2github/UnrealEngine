// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateMaterialShader.h"
#include "Materials/Material.h"
#include "ShaderParameterUtils.h"


FSlateMaterialShaderVS::FSlateMaterialShaderVS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
	: FMaterialShader(Initializer)
{
	ViewProjection.Bind(Initializer.ParameterMap, TEXT("ViewProjection"));
	SwitchVerticalAxisMultiplier.Bind( Initializer.ParameterMap, TEXT("SwitchVerticalAxisMultiplier"));
}


void FSlateMaterialShaderVS::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	// Set defines based on what this shader will be used for
	OutEnvironment.SetDefine( TEXT("USE_MATERIALS"), 1 );
	OutEnvironment.SetDefine( TEXT("NUM_CUSTOMIZED_UVS"), Parameters.Material->GetNumCustomizedUVs() );
	OutEnvironment.SetDefine( TEXT("HAS_SCREEN_POSITION"), Parameters.Material->HasVertexPositionOffsetConnected() );

	FMaterialShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
}

bool FSlateMaterialShaderVS::ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
{
	return Parameters.Material->GetMaterialDomain() == MD_UI;
}

void FSlateMaterialShaderVS::SetViewProjection(FRHICommandList& RHICmdList, const FMatrix& InViewProjection )
{
	SetShaderValue(RHICmdList, GetVertexShader(), ViewProjection, InViewProjection );
}

void FSlateMaterialShaderVS::SetMaterialShaderParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material)
{
	FRHIVertexShader* ShaderRHI = GetVertexShader();

	FMaterialShader::SetParameters<FRHIVertexShader>(RHICmdList, ShaderRHI, MaterialRenderProxy, *Material, View, View.ViewUniformBuffer, ESceneTextureSetupMode::None);
}

void FSlateMaterialShaderVS::SetVerticalAxisMultiplier(FRHICommandList& RHICmdList, float InMultiplier )
{
	SetShaderValue(RHICmdList, GetVertexShader(), SwitchVerticalAxisMultiplier, InMultiplier );
}


bool FSlateMaterialShaderVS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);

	Ar << ViewProjection;
	Ar << SwitchVerticalAxisMultiplier;

	return bShaderHasOutdatedParameters;
}


bool FSlateMaterialShaderPS::ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
{
	return Parameters.Material->GetMaterialDomain() == MD_UI;
}


void FSlateMaterialShaderPS::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	// Set defines based on what this shader will be used for
	OutEnvironment.SetDefine( TEXT("USE_MATERIALS"), 1 );
	OutEnvironment.SetDefine( TEXT("NUM_CUSTOMIZED_UVS"), Parameters.Material->GetNumCustomizedUVs() );

	FMaterialShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
}

FSlateMaterialShaderPS::FSlateMaterialShaderPS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
	: FMaterialShader(Initializer)
{
	ShaderParams.Bind(Initializer.ParameterMap, TEXT("ShaderParams"));
	GammaAndAlphaValues.Bind(Initializer.ParameterMap, TEXT("GammaAndAlphaValues"));
	DrawFlags.Bind(Initializer.ParameterMap, TEXT("DrawFlags"));
	AdditionalTextureParameter.Bind(Initializer.ParameterMap, TEXT("ElementTexture"));
	TextureParameterSampler.Bind(Initializer.ParameterMap, TEXT("ElementTextureSampler"));
}

void FSlateMaterialShaderPS::SetBlendState(FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FMaterial* Material)
{
	EBlendMode BlendMode = Material->GetBlendMode();

	switch (BlendMode)
	{
	default:
	case BLEND_Opaque:
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		break;
	case BLEND_Masked:
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		break;
	case BLEND_Translucent:
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
		break;
	case BLEND_Additive:
		// Add to the existing scene color
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
		break;
	case BLEND_Modulate:
		// Modulate with the existing scene color
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_Zero, BF_SourceColor>::GetRHI();
		break;
	case BLEND_AlphaComposite:
		// Blend with existing scene color. New color is already pre-multiplied by alpha.
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		break;
	case BLEND_AlphaHoldout:
		// Blend by holding out the matte shape of the source alpha
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
		break;

	};
}

void FSlateMaterialShaderPS::SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material, const FVector4& InShaderParams)
{
	FRHIPixelShader* ShaderRHI = GetPixelShader();

	SetShaderValue( RHICmdList, ShaderRHI, ShaderParams, InShaderParams );

	const ESceneTextureSetupMode SceneTextures = ESceneTextureSetupMode::SceneDepth | ESceneTextureSetupMode::SSAO | ESceneTextureSetupMode::CustomDepth; // TODO - SSAO valid here?
	FMaterialShader::SetParameters<FRHIPixelShader>(RHICmdList, ShaderRHI, MaterialRenderProxy, *Material, View, View.ViewUniformBuffer, SceneTextures);
}

void FSlateMaterialShaderPS::SetAdditionalTexture( FRHICommandList& RHICmdList, FRHITexture* InTexture, const FSamplerStateRHIRef SamplerState )
{
	SetTextureParameter(RHICmdList, GetPixelShader(), AdditionalTextureParameter, TextureParameterSampler, SamplerState, InTexture );
}

void FSlateMaterialShaderPS::SetDisplayGammaAndContrast(FRHICommandList& RHICmdList, float InDisplayGamma, float InContrast)
{
	FVector4 InGammaValues(2.2f / InDisplayGamma, 1.0f / InDisplayGamma, 0.0f, InContrast);

	SetShaderValue(RHICmdList, GetPixelShader(), GammaAndAlphaValues, InGammaValues);
}

void FSlateMaterialShaderPS::SetDrawFlags(FRHICommandList& RHICmdList, bool bDrawDisabledEffect)
{
	FVector4 InDrawFlags(bDrawDisabledEffect ? 1 : 0, 0, 0, 0);

	SetShaderValue(RHICmdList, GetPixelShader(), DrawFlags, InDrawFlags);
}

bool FSlateMaterialShaderPS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
	Ar << GammaAndAlphaValues;
	Ar << DrawFlags;
	Ar << ShaderParams;
	Ar << TextureParameterSampler;
	Ar << AdditionalTextureParameter;
	return bShaderHasOutdatedParameters;
}


#define IMPLEMENT_SLATE_VERTEXMATERIALSHADER_TYPE(bUseInstancing) \
	typedef TSlateMaterialShaderVS<bUseInstancing> TSlateMaterialShaderVS##bUseInstancing; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TSlateMaterialShaderVS##bUseInstancing, TEXT("/Engine/Private/SlateVertexShader.usf"), TEXT("Main"), SF_Vertex);

/** Instancing vertex shader */
IMPLEMENT_SLATE_VERTEXMATERIALSHADER_TYPE(true);
/** Non instancing vertex shader */
IMPLEMENT_SLATE_VERTEXMATERIALSHADER_TYPE(false);

#define IMPLEMENT_SLATE_MATERIALSHADER_TYPE(ShaderType) \
	typedef TSlateMaterialShaderPS<ESlateShader::ShaderType> TSlateMaterialShaderPS##ShaderType; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, TSlateMaterialShaderPS##ShaderType, TEXT("/Engine/Private/SlateElementPixelShader.usf"), TEXT("Main"), SF_Pixel);

IMPLEMENT_SLATE_MATERIALSHADER_TYPE(Custom)
IMPLEMENT_SLATE_MATERIALSHADER_TYPE(Default);
IMPLEMENT_SLATE_MATERIALSHADER_TYPE(Border);
IMPLEMENT_SLATE_MATERIALSHADER_TYPE(GrayscaleFont);
IMPLEMENT_SLATE_MATERIALSHADER_TYPE(ColorFont);
