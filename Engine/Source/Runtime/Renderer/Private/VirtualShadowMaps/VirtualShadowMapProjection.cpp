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

static TAutoConsoleVariable<int32> CVarSMRTRayCountDirectional(
	TEXT( "r.Shadow.v.SMRT.RayCountDirectional" ),
	0,
	TEXT( "Ray count for shadow map tracing of directional lights. 0 = disabled." ),
	ECVF_RenderThreadSafe
);

#if 0
static TAutoConsoleVariable<int32> CVarSMRTRayCountSpot(
	TEXT( "r.Shadow.v.SMRT.RayCountSpot" ),
	0,
	TEXT( "Ray count for shadow map tracing of spot lights. 0 = disabled." ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTSamplesPerRaySpot(
	TEXT( "r.Shadow.v.SMRT.SamplesPerRaySpot" ),
	16,
	TEXT( "Shadow map samples per ray for spot lights" ),
	ECVF_RenderThreadSafe
);
#endif

static TAutoConsoleVariable<int32> CVarSMRTSamplesPerRayDirectional(
	TEXT( "r.Shadow.v.SMRT.SamplesPerRayDirectional" ),
	12,
	TEXT( "Shadow map samples per ray for directional lights" ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarSMRTRayLengthScaleDirectional(
	TEXT( "r.Shadow.v.SMRT.RayLengthScaleDirectional" ),
	2.0f,
	TEXT( "Length of ray to shoot for directional lights, scaled by distance to camera." )
	TEXT( "Shorter rays limit the screen space size of shadow penumbra. " )
	TEXT( "Longer rays require more samples to avoid shadows disconnecting from contact points. " ),
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
	SHADER_PARAMETER(int32, SMRTRayCount)
	SHADER_PARAMETER(int32, SMRTSamplesPerRay)
	SHADER_PARAMETER(float, SMRTRayLengthScale)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FVirtualShadowMapProjectionSpotPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualShadowMapProjectionSpotPS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualShadowMapProjectionSpotPS, FGlobalShader);
	
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
IMPLEMENT_GLOBAL_SHADER(FVirtualShadowMapProjectionSpotPS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapProjection.usf", "VirtualShadowMapProjectionSpotPS", SF_Pixel);

class FVirtualShadowMapProjectionDirectionalPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualShadowMapProjectionDirectionalPS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualShadowMapProjectionDirectionalPS, FGlobalShader);

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
IMPLEMENT_GLOBAL_SHADER(FVirtualShadowMapProjectionDirectionalPS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapProjection.usf", "VirtualShadowMapProjectionDirectionalPS", SF_Pixel);



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

// Returns true if temporal denoising should be used
static bool AddPass_RenderVirtualShadowMapProjection(
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

	if (LightProxy->GetLightType() == LightType_Directional)
	{
		PassParameters->SMRTRayCount = CVarSMRTRayCountDirectional.GetValueOnRenderThread();
		PassParameters->SMRTSamplesPerRay = CVarSMRTSamplesPerRayDirectional.GetValueOnRenderThread();
		PassParameters->SMRTRayLengthScale = CVarSMRTRayLengthScaleDirectional.GetValueOnRenderThread();

		FVirtualShadowMapProjectionDirectionalPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVirtualShadowMapProjectionDirectionalPS::FOutputTypeDim>(OutputType);
		auto PixelShader = ShaderMap->GetShader<FVirtualShadowMapProjectionDirectionalPS>(PermutationVector);
		ValidateShaderParameters(PixelShader, *PassParameters);

		// NOTE: We use SV_Position in the shader, so we don't need separate scissor/view rect
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
			ShaderMap,
			(OutputType == EVirtualShadowMapProjectionOutputType::Debug) ? RDG_EVENT_NAME("Debug Directional Projection") : RDG_EVENT_NAME("Directional Projection"),
			PixelShader,
			PassParameters,
			ScissorRect,
			BlendState);

		// Use temporal denoising with SMRT
		return PassParameters->SMRTRayCount > 0;
	}
	else if (LightProxy->GetLightType() == LightType_Spot)
	{
		PassParameters->SMRTRayCount = 0; //CVarSMRTRayCountSpot.GetValueOnRenderThread();
		PassParameters->SMRTSamplesPerRay = 0; //CVarSMRTSamplesPerRaySpot.GetValueOnRenderThread();
		PassParameters->SMRTRayLengthScale = 0.0f;		// Currently unused in this path

		FVirtualShadowMapProjectionSpotPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVirtualShadowMapProjectionSpotPS::FOutputTypeDim>(OutputType);
		auto PixelShader = ShaderMap->GetShader<FVirtualShadowMapProjectionSpotPS>(PermutationVector);
		ValidateShaderParameters(PixelShader, *PassParameters);

		// NOTE: We use SV_Position in the shader, so we don't need separate scissor/view rect
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
			ShaderMap,
			(OutputType == EVirtualShadowMapProjectionOutputType::Debug) ? RDG_EVENT_NAME("Debug Spot Projection") : RDG_EVENT_NAME("Spot Projection"),
			PixelShader,
			PassParameters,
			ScissorRect,
			BlendState);

		// Use temporal denoising with SMRT
		return PassParameters->SMRTRayCount > 0;
	}
	else
	{
		check(false);	// Unsupported light type for VSM projection
	}

	return false;
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

// Returns true if temporal denoising should be used
static bool RenderVirtualShadowMapProjectionCommon(
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
	bool bOutUseTemporalDenoising = AddPass_RenderVirtualShadowMapProjection(
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

	return bOutUseTemporalDenoising;
}

void RenderVirtualShadowMapProjectionForDenoising(
	FProjectedShadowInfo* ShadowInfo,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef SignalTexture,
	bool& bOutUseTemporalDenoising)
{
	FRenderTargetBinding RenderTargetBinding(SignalTexture, ERenderTargetLoadAction::EClear);
	bOutUseTemporalDenoising = RenderVirtualShadowMapProjectionCommon(
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
	FRDGTextureRef SignalTexture,
	bool& bOutUseTemporalDenoising)
{	
	FRenderTargetBinding RenderTargetBinding(SignalTexture, ERenderTargetLoadAction::EClear);
	bOutUseTemporalDenoising = RenderVirtualShadowMapProjectionCommon(
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
