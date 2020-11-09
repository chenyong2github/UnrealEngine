// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMapProjection.cpp
=============================================================================*/

#include "VirtualShadowMapProjection.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "LightSceneInfo.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "ShadowRendering.h"
#include "DeferredShadingRenderer.h"
#include "PixelShaderUtils.h"
#include "ShadowRendering.h"
#include "SceneRendering.h"
#include "VirtualShadowMapClipmap.h"

static TAutoConsoleVariable<float> CVarContactShadowLength(
	TEXT( "r.Shadow.v.ContactShadowLength" ),
	0.04f,
	TEXT( "Length of the screen space contact shadow trace (smart shadow bias) before the virtual shadow map lookup." ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVirtualShadowMapDebugProjection(
	TEXT( "r.Shadow.v.DebugVisualizeProjection" ),
	0,
	TEXT( "Projection pass debug output visualization for use with 'vis VirtSmDebugProj'." ),
	ECVF_RenderThreadSafe
);

BEGIN_SHADER_PARAMETER_STRUCT(FProjectionParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, ProjectionParameters)
	SHADER_PARAMETER_STRUCT(FLightShaderParameters, Light)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER(int32, VirtualShadowMapId)
	SHADER_PARAMETER(float, ContactShadowLength)
	SHADER_PARAMETER(int32, DebugOutputType)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FVirtualShadowMapProjectionPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualShadowMapProjectionPS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualShadowMapProjectionPS, FGlobalShader);
	
	class FOutputTypeDim : SHADER_PERMUTATION_ENUM_CLASS("OUTPUT_TYPE", EVirtualShadowMapProjectionOutputType);
	using FPermutationDomain = TShaderPermutationDomain<FOutputTypeDim>;

	using FParameters = FProjectionParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualShadowMapProjectionPS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapProjection.usf", "VirtualShadowMapProjectionPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FVirtualShadowMapProjectionCompositeParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputSignal)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// Composite denoised shadow projection mask onto the light's shadow mask
// Basically just a copy shader with a special blend mode
class FVirtualShadowMapProjectionCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualShadowMapProjectionCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualShadowMapProjectionCompositePS, FGlobalShader);
		
	using FParameters = FVirtualShadowMapProjectionCompositeParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Required right now due to where the shader function lives, but not actually used
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualShadowMapProjectionCompositePS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapProjection.usf", "VirtualShadowMapCompositePS", SF_Pixel);

static void AddPass_RenderVirtualShadowMapProjection(
	const FLightSceneProxy* LightProxy,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	FVirtualShadowMapArray& VirtualShadowMapArray,
	int VirtualShadowMapId,
	const FIntRect ScissorRect,
	EVirtualShadowMapProjectionOutputType OutputType,
	const FRenderTargetBinding& RenderTargetBinding,
	FRHIBlendState* BlendState)
{
	check(ScissorRect.Area() > 0);
		
	FGlobalShaderMap* ShaderMap = View.ShaderMap;

	FProjectionParameters* PassParameters = GraphBuilder.AllocParameters<FProjectionParameters>();
	VirtualShadowMapArray.SetProjectionParameters(GraphBuilder, PassParameters->ProjectionParameters);
	PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::GBuffers | ESceneTextureSetupMode::SceneDepth);
	PassParameters->View = View.ViewUniformBuffer;
		
	FLightShaderParameters LightParameters;
	LightProxy->GetLightShaderParameters(LightParameters);
	PassParameters->Light = LightParameters;
	PassParameters->VirtualShadowMapId = VirtualShadowMapId;
	PassParameters->DebugOutputType = CVarVirtualShadowMapDebugProjection.GetValueOnRenderThread();
	PassParameters->ContactShadowLength = CVarContactShadowLength.GetValueOnRenderThread();

	PassParameters->RenderTargets[0] = RenderTargetBinding;

	{
		FVirtualShadowMapProjectionPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVirtualShadowMapProjectionPS::FOutputTypeDim>(OutputType);
		auto PixelShader = ShaderMap->GetShader<FVirtualShadowMapProjectionPS>(PermutationVector);
		ValidateShaderParameters(PixelShader, *PassParameters);

		// NOTE: We use SV_Position in the shader, so we don't need separate scissor/view rect
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
			ShaderMap,
			(OutputType == EVirtualShadowMapProjectionOutputType::Debug) ? RDG_EVENT_NAME("Debug Projection") : RDG_EVENT_NAME("Projection"),
			PixelShader,
			PassParameters,
			ScissorRect,
			BlendState);
	}
}

static FRDGTextureRef CreateDebugOutput(FRDGBuilder& GraphBuilder, FIntPoint Extent)
{
	FRDGTextureDesc DebugOutputDesc = FRDGTextureDesc::Create2D(
		Extent,
		PF_A32B32G32R32F,
		FClearValueBinding::Transparent,
		TexCreate_ShaderResource | TexCreate_RenderTargetable);
	return GraphBuilder.CreateTexture(DebugOutputDesc, TEXT("VirtSmDebugProj"));
}

static void RenderVirtualShadowMapProjectionCommon(
	const FLightSceneProxy* LightProxy,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	FVirtualShadowMapArray& VirtualShadowMapArray,
	int32 VirtualShadowMapId,
	const FIntRect ScissorRect,
	EVirtualShadowMapProjectionOutputType OutputType,
	const FRenderTargetBinding& RenderTargetBinding,
	FRHIBlendState* BlendState)
{
	// Main Pass
	AddPass_RenderVirtualShadowMapProjection(
		LightProxy,
		GraphBuilder,
		View,
		VirtualShadowMapArray,
		VirtualShadowMapId,
		ScissorRect,
		OutputType,
		RenderTargetBinding,
		BlendState);

	// Debug visualization
	const int32 DebugVisualize = CVarVirtualShadowMapDebugProjection.GetValueOnRenderThread();
	if (DebugVisualize > 0)
	{
		FRDGTextureRef DebugOutput = CreateDebugOutput(GraphBuilder, View.ViewRect.Max);
		FRenderTargetBinding DebugRenderTargetBinding(DebugOutput, ERenderTargetLoadAction::EClear);

		AddPass_RenderVirtualShadowMapProjection(
			LightProxy,
			GraphBuilder,
			View,
			VirtualShadowMapArray,
			VirtualShadowMapId,
			ScissorRect,
			EVirtualShadowMapProjectionOutputType::Debug,
			DebugRenderTargetBinding,
			nullptr);

		GraphBuilder.QueueTextureExtraction(DebugOutput, &VirtualShadowMapArray.DebugVisualizationProjectionOutput);
	}
}

void RenderVirtualShadowMapProjectionForDenoising(
	FProjectedShadowInfo* ShadowInfo,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef SignalTexture)
{
	FRenderTargetBinding RenderTargetBinding(SignalTexture, ERenderTargetLoadAction::EClear);
	RenderVirtualShadowMapProjectionCommon(
		ShadowInfo->GetLightSceneInfo().Proxy,
		GraphBuilder,
		View,
		VirtualShadowMapArray,
		ShadowInfo->VirtualShadowMap->ID,
		ScissorRect,
		EVirtualShadowMapProjectionOutputType::Denoiser,
		RenderTargetBinding,
		nullptr);
}

void RenderVirtualShadowMapProjectionForDenoising(
	const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef SignalTexture)
{	
	FRenderTargetBinding RenderTargetBinding(SignalTexture, ERenderTargetLoadAction::EClear);
	RenderVirtualShadowMapProjectionCommon(
		Clipmap->GetLightSceneInfo().Proxy,
		GraphBuilder,
		View,
		VirtualShadowMapArray,
		Clipmap->GetVirtualShadowMap()->ID,
		ScissorRect,
		EVirtualShadowMapProjectionOutputType::Denoiser,
		RenderTargetBinding,
		nullptr);
}

void RenderVirtualShadowMapProjection(
	FProjectedShadowInfo* ShadowInfo,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef ScreenShadowMaskTexture,
	bool bProjectingForForwardShading)
{
	FRenderTargetBinding RenderTargetBinding(ScreenShadowMaskTexture, ERenderTargetLoadAction::ELoad);
	FRHIBlendState* BlendState = ShadowInfo->GetBlendStateForProjection(bProjectingForForwardShading, false);
	RenderVirtualShadowMapProjectionCommon(
		ShadowInfo->GetLightSceneInfo().Proxy,
		GraphBuilder,
		View,
		VirtualShadowMapArray,
		ShadowInfo->VirtualShadowMap->ID,
		ScissorRect,
		EVirtualShadowMapProjectionOutputType::ScreenShadowMask,
		RenderTargetBinding,
		BlendState);
}

void RenderVirtualShadowMapProjection(
	const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef ScreenShadowMaskTexture,
	bool bProjectingForForwardShading)
{
	FRenderTargetBinding RenderTargetBinding(ScreenShadowMaskTexture, ERenderTargetLoadAction::ELoad);	
	// See FProjectedShadowInfo::GetBlendStateForProjection. TODO: Support other modes in this path?
	FRHIBlendState* BlendState = TStaticBlendState<CW_BA, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI();
	RenderVirtualShadowMapProjectionCommon(
		Clipmap->GetLightSceneInfo().Proxy,
		GraphBuilder,
		View,
		VirtualShadowMapArray,
		Clipmap->GetVirtualShadowMap()->ID,
		ScissorRect,
		EVirtualShadowMapProjectionOutputType::ScreenShadowMask,
		RenderTargetBinding,
		BlendState);
}


void CompositeVirtualShadowMapMask(
	FRDGBuilder& GraphBuilder,
	const FIntRect ScissorRect,
	const FSSDSignalTextures& InputSignal,
	FRDGTextureRef OutputShadowMaskTexture)
{
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	FVirtualShadowMapProjectionCompositeParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualShadowMapProjectionCompositeParameters>();
	PassParameters->InputSignal = InputSignal.Textures[0];

	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputShadowMaskTexture, ERenderTargetLoadAction::ELoad);

	// See FProjectedShadowInfo::GetBlendStateForProjection, but we don't want any of the special cascade behavior, etc. as this is a post-denoised mask.
	FRHIBlendState* BlendState = TStaticBlendState<CW_BA, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI();

	auto PixelShader = ShaderMap->GetShader<FVirtualShadowMapProjectionCompositePS>();
	ValidateShaderParameters(PixelShader, *PassParameters);

	FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("Mask Composite"),
		PixelShader,
		PassParameters,
		ScissorRect,
		BlendState);
}
