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
	0.02f,
	TEXT( "Length of the screen space contact shadow trace (smart shadow bias) before the virtual shadow map lookup." ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarNormalOffsetWorld(
	TEXT( "r.Shadow.v.NormalOffsetWorld" ),
	0.1f,
	TEXT( "World space offset along surface normal for shadow lookup." )
	TEXT( "Higher values avoid artifacts on surfaces nearly parallel to the light, but also visibility offset shadows and increase the chance of hitting unmapped pages." ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVirtualShadowMapDebugProjection(
	TEXT( "r.Shadow.v.DebugVisualizeProjection" ),
	0,
	TEXT( "Projection pass debug output visualization for use with 'vis VirtSmDebugProj'." ),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarVirtualShadowOnePassProjection(
	TEXT("r.Shadow.v.OnePassProjection"),
	0,
	TEXT("Single pass projects all local VSMs culled with the light grid. Used in conjunction with clustered deferred shading."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTRayCountLocal(
	TEXT( "r.Shadow.v.SMRT.RayCountLocal" ),
	0,
	TEXT( "Ray count for shadow map tracing of local lights. 0 = disabled." ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTSamplesPerRayLocal(
	TEXT( "r.Shadow.v.SMRT.SamplesPerRayLocal" ),
	16,
	TEXT( "Shadow map samples per ray for local lights" ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarSMRTMaxRayAngleFromLight(
	TEXT( "r.Shadow.v.SMRT.MaxRayAngleFromLight" ),
	0.03f,
	TEXT( "Max angle (in radians) a ray is allowed to span from the light's perspective for local lights." )
	TEXT( "Smaller angles limit the screen space size of shadow penumbra. " )
	TEXT( "Larger angles lead to more noise. " ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTRayCountDirectional(
	TEXT( "r.Shadow.v.SMRT.RayCountDirectional" ),
	0,
	TEXT( "Ray count for shadow map tracing of directional lights. 0 = disabled." ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTSamplesPerRayDirectional(
	TEXT( "r.Shadow.v.SMRT.SamplesPerRayDirectional" ),
	12,
	TEXT( "Shadow map samples per ray for directional lights" ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarSMRTRayLengthScaleDirectional(
	TEXT( "r.Shadow.v.SMRT.RayLengthScaleDirectional" ),
	1.0f,
	TEXT( "Length of ray to shoot for directional lights, scaled by distance to camera." )
	TEXT( "Shorter rays limit the screen space size of shadow penumbra. " )
	TEXT( "Longer rays require more samples to avoid shadows disconnecting from contact points. " ),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTAdaptiveRayCount(
	TEXT( "r.Shadow.v.SMRT.AdaptiveRayCount" ),
	1,
	TEXT( "Shoot fewer rays in fully shadowed and unshadowed regions. Currently only supported with OnePassProjection. " ),
	ECVF_RenderThreadSafe
);

BEGIN_SHADER_PARAMETER_STRUCT(FProjectionParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, ProjectionParameters)
	SHADER_PARAMETER_STRUCT(FLightShaderParameters, Light)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER(int32, VirtualShadowMapId)
	SHADER_PARAMETER(float, ContactShadowLength)
	SHADER_PARAMETER(float, NormalOffsetWorld)
	SHADER_PARAMETER(int32, DebugOutputType)
	SHADER_PARAMETER(int32, SMRTRayCount)
	SHADER_PARAMETER(int32, SMRTSamplesPerRay)
	SHADER_PARAMETER(float, SMRTRayLengthScale)
	SHADER_PARAMETER(float, SMRTCotMaxRayAngleFromLight)
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
	PassParameters->NormalOffsetWorld = CVarNormalOffsetWorld.GetValueOnRenderThread();

	PassParameters->RenderTargets[0] = RenderTargetBinding;

	if (LightProxy->GetLightType() == LightType_Directional)
	{
		PassParameters->SMRTRayCount = CVarSMRTRayCountDirectional.GetValueOnRenderThread();
		PassParameters->SMRTSamplesPerRay = CVarSMRTSamplesPerRayDirectional.GetValueOnRenderThread();
		PassParameters->SMRTRayLengthScale = CVarSMRTRayLengthScaleDirectional.GetValueOnRenderThread();
		PassParameters->SMRTCotMaxRayAngleFromLight = 0.0f;	// unused in this path

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
	else
	{
		PassParameters->SMRTRayCount = CVarSMRTRayCountLocal.GetValueOnRenderThread();
		PassParameters->SMRTSamplesPerRay = CVarSMRTSamplesPerRayLocal.GetValueOnRenderThread();
		PassParameters->SMRTRayLengthScale = 0.0f;		// unused in this path
		PassParameters->SMRTCotMaxRayAngleFromLight = 1.0f / FMath::Tan(CVarSMRTMaxRayAngleFromLight.GetValueOnRenderThread());

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

	return false;
}

static FRDGTextureRef CreateDebugOutput(FRDGBuilder& GraphBuilder, FIntPoint Extent)
{
	FRDGTextureDesc DebugOutputDesc = FRDGTextureDesc::Create2D(
		Extent,
		PF_A32B32G32R32F,
		FClearValueBinding::Transparent,
		TexCreate_ShaderResource | TexCreate_RenderTargetable);
	return GraphBuilder.CreateTexture(DebugOutputDesc, TEXT("VSM.DebugProj"));
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
		ShadowInfo->VirtualShadowMaps[0]->ID,
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
		ShadowInfo->VirtualShadowMaps[0]->ID,
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

class FVirtualShadowMapProjectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualShadowMapProjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualShadowMapProjectionCS, FGlobalShader)
	
	class FSMRTAdaptiveRayCountDim : SHADER_PERMUTATION_BOOL("SMRT_ADAPTIVE_RAY_COUNT");
	using FPermutationDomain = TShaderPermutationDomain<FSMRTAdaptiveRayCountDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, ProjectionParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FVirtualShadowMapProjectionShaderData >, ShadowMapProjectionData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, VirtualShadowMapIdRemap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D< uint >, RWShadowMaskBits)
		SHADER_PARAMETER(uint32, NumDirectionalLightSmInds)
		SHADER_PARAMETER(float, ContactShadowLength)
		SHADER_PARAMETER(float, NormalOffsetWorld)
		SHADER_PARAMETER(int32, DebugOutputType)
		SHADER_PARAMETER(int32, SMRTRayCount)
		SHADER_PARAMETER(int32, SMRTSamplesPerRay)
		SHADER_PARAMETER(float, SMRTRayLengthScale)
		SHADER_PARAMETER(float, SMRTCotMaxRayAngleFromLight)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FSMRTAdaptiveRayCountDim>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualShadowMapProjectionCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapProjection.usf", "VirtualShadowMapProjection", SF_Compute);

void RenderVirtualShadowMapProjection(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	FRDGTextureRef ShadowMaskBits )
{
	FVirtualShadowMapProjectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FVirtualShadowMapProjectionCS::FParameters >();
	VirtualShadowMapArray.SetProjectionParameters( GraphBuilder, PassParameters->ProjectionParameters );

	PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
	PassParameters->VirtualShadowMapIdRemap = GraphBuilder.CreateSRV( VirtualShadowMapArray.VirtualShadowMapIdRemapRDG[0] );	// FIXME Index proper view
	PassParameters->NumDirectionalLightSmInds = VirtualShadowMapArray.NumDirectionalLights;

	PassParameters->DebugOutputType = CVarVirtualShadowMapDebugProjection.GetValueOnRenderThread();
	PassParameters->ContactShadowLength = CVarContactShadowLength.GetValueOnRenderThread();
	PassParameters->NormalOffsetWorld = CVarNormalOffsetWorld.GetValueOnRenderThread();

	PassParameters->SMRTRayCount = CVarSMRTRayCountLocal.GetValueOnRenderThread();
	PassParameters->SMRTSamplesPerRay = CVarSMRTSamplesPerRayLocal.GetValueOnRenderThread();
	PassParameters->SMRTRayLengthScale = 0.0f;		// Currently unused in this path
	PassParameters->SMRTCotMaxRayAngleFromLight = 1.0f / FMath::Tan(CVarSMRTMaxRayAngleFromLight.GetValueOnRenderThread());

	PassParameters->RWShadowMaskBits = GraphBuilder.CreateUAV( ShadowMaskBits );
	
	bool bAdaptiveRayCount = GRHISupportsWaveOperations && CVarSMRTAdaptiveRayCount.GetValueOnRenderThread() != 0;

	FVirtualShadowMapProjectionCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FVirtualShadowMapProjectionCS::FSMRTAdaptiveRayCountDim >( bAdaptiveRayCount );
	auto ComputeShader = View.ShaderMap->GetShader< FVirtualShadowMapProjectionCS >( PermutationVector );

	const FIntPoint GroupCount = FIntPoint::DivideAndRoundUp( View.ViewRect.Size(), 8 );

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		bAdaptiveRayCount ? RDG_EVENT_NAME("VirtualShadowMapProjection") : RDG_EVENT_NAME("VirtualShadowMapProjection (StaticRayCount)"),
		ComputeShader,
		PassParameters,
		FIntVector( GroupCount.X, GroupCount.Y, 1 )
	);
}
