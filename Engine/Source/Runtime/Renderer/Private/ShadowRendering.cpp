// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShadowRendering.cpp: Shadow rendering implementation
=============================================================================*/

#include "ShadowRendering.h"
#include "PrimitiveViewRelevance.h"
#include "DepthRendering.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "HairStrands/HairStrandsRendering.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Directional light
static TAutoConsoleVariable<float> CVarCSMShadowDepthBias(
	TEXT("r.Shadow.CSMDepthBias"),
	10.0f,
	TEXT("Constant depth bias used by CSM"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarCSMShadowSlopeScaleDepthBias(
	TEXT("r.Shadow.CSMSlopeScaleDepthBias"),
	3.0f,
	TEXT("Slope scale depth bias used by CSM"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarPerObjectDirectionalShadowDepthBias(
	TEXT("r.Shadow.PerObjectDirectionalDepthBias"),
	10.0f,
	TEXT("Constant depth bias used by per-object shadows from directional lights\n")
	TEXT("Lower values give better shadow contact, but increase self-shadowing artifacts"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarPerObjectDirectionalShadowSlopeScaleDepthBias(
	TEXT("r.Shadow.PerObjectDirectionalSlopeDepthBias"),
	3.0f,
	TEXT("Slope scale depth bias used by per-object shadows from directional lights\n")
	TEXT("Lower values give better shadow contact, but increase self-shadowing artifacts"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarCSMSplitPenumbraScale(
	TEXT("r.Shadow.CSMSplitPenumbraScale"),
	0.5f,
	TEXT("Scale applied to the penumbra size of Cascaded Shadow Map splits, useful for minimizing the transition between splits"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarCSMDepthBoundsTest(
	TEXT("r.Shadow.CSMDepthBoundsTest"),
	1,
	TEXT("Whether to use depth bounds tests rather than stencil tests for the CSM bounds"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarShadowTransitionScale(
	TEXT("r.Shadow.TransitionScale"),
	60.0f,
	TEXT("This controls the 'fade in' region between a caster and where his shadow shows up.  Larger values make a smaller region which will have more self shadowing artifacts"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarCSMShadowReceiverBias(
	TEXT("r.Shadow.CSMReceiverBias"),
	0.9f,
	TEXT("Receiver bias used by CSM. Value between 0 and 1."),
	ECVF_RenderThreadSafe);


///////////////////////////////////////////////////////////////////////////////////////////////////
// Point light
static TAutoConsoleVariable<float> CVarPointLightShadowDepthBias(
	TEXT("r.Shadow.PointLightDepthBias"),
	0.02f,
	TEXT("Depth bias that is applied in the depth pass for shadows from point lights. (0.03 avoids peter paning but has some shadow acne)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarPointLightShadowSlopeScaleDepthBias(
	TEXT("r.Shadow.PointLightSlopeScaleDepthBias"),
	3.0f,
	TEXT("Slope scale depth bias that is applied in the depth pass for shadows from point lights"),
	ECVF_RenderThreadSafe);


///////////////////////////////////////////////////////////////////////////////////////////////////
// Rect light
static TAutoConsoleVariable<float> CVarRectLightShadowDepthBias(
	TEXT("r.Shadow.RectLightDepthBias"),
	0.025f,
	TEXT("Depth bias that is applied in the depth pass for shadows from rect lights. (0.03 avoids peter paning but has some shadow acne)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRectLightShadowSlopeScaleDepthBias(
	TEXT("r.Shadow.RectLightSlopeScaleDepthBias"),
	2.5f,
	TEXT("Slope scale depth bias that is applied in the depth pass for shadows from rect lights"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRectLightShadowReceiverBias(
	TEXT("r.Shadow.RectLightReceiverBias"),
	0.3f,
	TEXT("Receiver bias used by rect light. Value between 0 and 1."),
	ECVF_RenderThreadSafe);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Spot light
static TAutoConsoleVariable<float> CVarSpotLightShadowDepthBias(
	TEXT("r.Shadow.SpotLightDepthBias"),
	3.0f,
	TEXT("Depth bias that is applied in the depth pass for per object projected shadows from spot lights"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSpotLightShadowSlopeScaleDepthBias(
	TEXT("r.Shadow.SpotLightSlopeDepthBias"),
	3.0f,
	TEXT("Slope scale depth bias that is applied in the depth pass for per object projected shadows from spot lights"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSpotLightShadowTransitionScale(
	TEXT("r.Shadow.SpotLightTransitionScale"),
	60.0f,
	TEXT("Transition scale for spotlights"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSpotLightShadowReceiverBias(
	TEXT("r.Shadow.SpotLightReceiverBias"),
	0.5f,
	TEXT("Receiver bias used by spotlights. Value between 0 and 1."),
	ECVF_RenderThreadSafe);


///////////////////////////////////////////////////////////////////////////////////////////////////
// General
static TAutoConsoleVariable<int32> CVarEnableModulatedSelfShadow(
	TEXT("r.Shadow.EnableModulatedSelfShadow"),
	0,
	TEXT("Allows modulated shadows to affect the shadow caster. (mobile only)"),
	ECVF_RenderThreadSafe);

static int GStencilOptimization = 1;
static FAutoConsoleVariableRef CVarStencilOptimization(
	TEXT("r.Shadow.StencilOptimization"),
	GStencilOptimization,
	TEXT("Removes stencil clears between shadow projections by zeroing the stencil during testing"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarFilterMethod(
	TEXT("r.Shadow.FilterMethod"),
	0,
	TEXT("Chooses the shadow filtering method.\n")
	TEXT(" 0: Uniform PCF (default)\n")
	TEXT(" 1: PCSS (experimental)\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxSoftKernelSize(
	TEXT("r.Shadow.MaxSoftKernelSize"),
	40,
	TEXT("Mazimum size of the softening kernels in pixels."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarShadowMaxSlopeScaleDepthBias(
	TEXT("r.Shadow.ShadowMaxSlopeScaleDepthBias"),
	1.0f,
	TEXT("Max Slope depth bias used for shadows for all lights\n")
	TEXT("Higher values give better self-shadowing, but increase self-shadowing artifacts"),
	ECVF_RenderThreadSafe);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Hair
static TAutoConsoleVariable<int32> CVarHairStrandsCullPerObjectShadowCaster(
	TEXT("r.HairStrands.Shadow.CullPerObjectShadowCaster"),
	1,
	TEXT("Enable CPU culling of object casting per-object shadow (stationnary object)"),
	ECVF_RenderThreadSafe);

DEFINE_GPU_DRAWCALL_STAT(ShadowProjection);

// 0:off, 1:low, 2:med, 3:high, 4:very high, 5:max
uint32 GetShadowQuality()
{
	static const auto ICVarQuality = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShadowQuality"));

	int Ret = ICVarQuality->GetValueOnRenderThread();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	static const auto ICVarLimit = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LimitRenderingFeatures"));
	if(ICVarLimit)
	{
		int32 Limit = ICVarLimit->GetValueOnRenderThread();

		if(Limit > 2)
		{
			Ret = 0;
		}
	}
#endif

	return FMath::Clamp(Ret, 0, 5);
}

void GetOnePassPointShadowProjectionParameters(const FProjectedShadowInfo* ShadowInfo, FOnePassPointShadowProjection& OutParameters)
{
	//@todo DynamicGI: remove duplication with FOnePassPointShadowProjectionShaderParameters
	FRHITexture* ShadowDepthTextureValue = ShadowInfo
		? ShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture->GetTextureCube()
		: GBlackTextureDepthCube->TextureRHI.GetReference();
	if (!ShadowDepthTextureValue)
	{
		ShadowDepthTextureValue = GBlackTextureDepthCube->TextureRHI.GetReference();
	}

	OutParameters.ShadowDepthCubeTexture = ShadowDepthTextureValue;
	OutParameters.ShadowDepthCubeTexture2 = ShadowDepthTextureValue;
	// Use a comparison sampler to do hardware PCF
	OutParameters.ShadowDepthCubeTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0, 0, SCF_Less>::GetRHI();

	if (ShadowInfo)
	{
		for (int32 i = 0; i < ShadowInfo->OnePassShadowViewProjectionMatrices.Num(); i++)
		{
			OutParameters.ShadowViewProjectionMatrices[i] = ShadowInfo->OnePassShadowViewProjectionMatrices[i];
		}

		OutParameters.InvShadowmapResolution = 1.0f / ShadowInfo->ResolutionX;
	}
	else
	{
		FPlatformMemory::Memzero(&OutParameters.ShadowViewProjectionMatrices[0], sizeof(OutParameters.ShadowViewProjectionMatrices));
		OutParameters.InvShadowmapResolution = 0;
	}
}

/*-----------------------------------------------------------------------------
	FShadowVolumeBoundProjectionVS
-----------------------------------------------------------------------------*/

void FShadowVolumeBoundProjectionVS::SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo)
{
	FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);
	
	if(ShadowInfo->IsWholeSceneDirectionalShadow())
	{
		// Calculate bounding geometry transform for whole scene directional shadow.
		// Use a pair of pre-transformed planes for stenciling.
		StencilingGeometryParameters.Set(RHICmdList, this, FVector4(0,0,0,1));
	}
	else if(ShadowInfo->IsWholeScenePointLightShadow())
	{
		// Handle stenciling sphere for point light.
		StencilingGeometryParameters.Set(RHICmdList, this, View, ShadowInfo->LightSceneInfo);
	}
	else
	{
		// Other bounding geometry types are pre-transformed.
		StencilingGeometryParameters.Set(RHICmdList, this, FVector4(0,0,0,1));
	}
}

IMPLEMENT_TYPE_LAYOUT(FShadowProjectionVertexShaderInterface);
IMPLEMENT_TYPE_LAYOUT(FShadowProjectionPixelShaderInterface);

IMPLEMENT_SHADER_TYPE(,FShadowProjectionNoTransformVS,TEXT("/Engine/Private/ShadowProjectionVertexShader.usf"),TEXT("Main"),SF_Vertex);

IMPLEMENT_SHADER_TYPE(,FShadowVolumeBoundProjectionVS,TEXT("/Engine/Private/ShadowProjectionVertexShader.usf"),TEXT("Main"),SF_Vertex);

/**
 * Implementations for TShadowProjectionPS.  
 */
#if !UE_BUILD_DOCS
#define IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(Quality,UseFadePlane,UseTransmission, SupportSubPixel) \
	typedef TShadowProjectionPS<Quality, UseFadePlane, false, UseTransmission, SupportSubPixel> FShadowProjectionPS##Quality##UseFadePlane##UseTransmission##SupportSubPixel; \
	IMPLEMENT_SHADER_TYPE(template<>,FShadowProjectionPS##Quality##UseFadePlane##UseTransmission##SupportSubPixel,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);

// Projection shaders without the distance fade, with different quality levels.
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(1,false,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(2,false,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(3,false,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(4,false,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5,false,false,false);

IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(1,false,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(2,false,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(3,false,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(4,false,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5,false,true,false);

// Projection shaders with the distance fade, with different quality levels.
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(1,true,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(2,true,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(3,true,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(4,true,false,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5,true,false,false);

IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(1,true,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(2,true,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(3,true,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(4,true,true,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5,true,true,false);

// Projection shaders without the distance fade, without transmission, with Sub-PixelSupport with different quality levels
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(1, false, false, true);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(2, false, false, true);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(3, false, false, true);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(4, false, false, true);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5, false, false, true);

#undef IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER

#define IMPLEMENT_MODULATED_SHADOW_PROJECTION_PIXEL_SHADER(Quality) \
	using FShadowModulatedProjectionPS##Quality = TShadowProjectionPS<Quality, false, true>; \
	IMPLEMENT_TEMPLATE_TYPE_LAYOUT(template<>, FShadowModulatedProjectionPS##Quality); \
	IMPLEMENT_SHADER_TYPE(template<>, TModulatedShadowProjection<Quality>, TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"), TEXT("Main"), SF_Pixel);

// Implement a pixel shader for rendering modulated shadow projections.
IMPLEMENT_MODULATED_SHADOW_PROJECTION_PIXEL_SHADER(1);
IMPLEMENT_MODULATED_SHADOW_PROJECTION_PIXEL_SHADER(2);
IMPLEMENT_MODULATED_SHADOW_PROJECTION_PIXEL_SHADER(3);
IMPLEMENT_MODULATED_SHADOW_PROJECTION_PIXEL_SHADER(4);
IMPLEMENT_MODULATED_SHADOW_PROJECTION_PIXEL_SHADER(5);

#undef IMPLEMENT_MODULATED_SHADOW_PROJECTION_PIXEL_SHADER

#endif

// with different quality levels
IMPLEMENT_SHADER_TYPE(template<>,TShadowProjectionFromTranslucencyPS<1>,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TShadowProjectionFromTranslucencyPS<2>,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TShadowProjectionFromTranslucencyPS<3>,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TShadowProjectionFromTranslucencyPS<4>,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TShadowProjectionFromTranslucencyPS<5>,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);

// Implement a pixel shader for rendering one pass point light shadows with different quality levels
#define IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(Quality,UseTransmission,UseSubPixel) \
	typedef TOnePassPointShadowProjectionPS<Quality,  UseTransmission, UseSubPixel> FOnePassPointShadowProjectionPS##Quality##UseTransmission##UseSubPixel; \
	IMPLEMENT_SHADER_TYPE(template<>,FOnePassPointShadowProjectionPS##Quality##UseTransmission##UseSubPixel,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("MainOnePassPointLightPS"),SF_Pixel);

IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(1, false, true);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(2, false, true);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(3, false, true);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(4, false, true);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(5, false, true);

IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(1, false, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(2, false, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(3, false, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(4, false, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(5, false, false);

IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(1, true, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(2, true, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(3, true, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(4, true, false);
IMPLEMENT_ONEPASS_POINT_SHADOW_PROJECTION_PIXEL_SHADER(5, true, false);

IMPLEMENT_SHADER_TYPE(, TScreenSpaceModulatedShadowVS, TEXT("/Engine/Private/ShadowProjectionVertexShader.usf"), TEXT("MainVS_ScreenSpaceModulatedShadow"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, TScreenSpaceModulatedShadowPS, TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"), TEXT("MainPS_ScreenSpaceModulatedShadow"), SF_Pixel);

// Implements a pixel shader for directional light PCSS.
#define IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(Quality,UseFadePlane) \
	typedef TDirectionalPercentageCloserShadowProjectionPS<Quality, UseFadePlane> TDirectionalPercentageCloserShadowProjectionPS##Quality##UseFadePlane; \
	IMPLEMENT_SHADER_TYPE(template<>,TDirectionalPercentageCloserShadowProjectionPS##Quality##UseFadePlane,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5,false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5,true);
#undef IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER

// Implements a pixel shader for spot light PCSS.
#define IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(Quality,UseFadePlane) \
	typedef TSpotPercentageCloserShadowProjectionPS<Quality, UseFadePlane> TSpotPercentageCloserShadowProjectionPS##Quality##UseFadePlane; \
	IMPLEMENT_SHADER_TYPE(template<>,TSpotPercentageCloserShadowProjectionPS##Quality##UseFadePlane,TEXT("/Engine/Private/ShadowProjectionPixelShader.usf"),TEXT("Main"),SF_Pixel);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5, false);
IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER(5, true);
#undef IMPLEMENT_SHADOW_PROJECTION_PIXEL_SHADER

template<typename VertexShaderType, typename PixelShaderType>
static void BindShaderShaders(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit,
	int32 ViewIndex, const FViewInfo& View, const FHairStrandsVisibilityData* HairVisibilityData, const FProjectedShadowInfo* ShadowInfo)
{
	TShaderRef<VertexShaderType> VertexShader = View.ShaderMap->GetShader<VertexShaderType>();
	TShaderRef<PixelShaderType> PixelShader = View.ShaderMap->GetShader<PixelShaderType>();

	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, View, ShadowInfo);
	PixelShader->SetParameters(RHICmdList, ViewIndex, View, HairVisibilityData, ShadowInfo);
}


static void BindShadowProjectionShaders(int32 Quality, FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer GraphicsPSOInit, int32 ViewIndex, const FViewInfo& View,
	const FHairStrandsVisibilityData* HairVisibilityData, const FProjectedShadowInfo* ShadowInfo, bool bMobileModulatedProjections)
{
	if (HairVisibilityData)
	{
		check(!bMobileModulatedProjections);

		if (ShadowInfo->IsWholeSceneDirectionalShadow())
		{
			switch (Quality)
			{
			case 1: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<1, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 2: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<2, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 3: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<3, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 4: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<4, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 5: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<5, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			default:
				check(0);
			}
		}
		else
		{
			switch (Quality)
			{
			case 1: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<1, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 2: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<2, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 3: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<3, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 4: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<4, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 5: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<5, false, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			default:
				check(0);
			}
		}
		return;
	}

	if (ShadowInfo->bTranslucentShadow)
	{
		switch (Quality)
		{
		case 1: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionFromTranslucencyPS<1> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
		case 2: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionFromTranslucencyPS<2> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
		case 3: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionFromTranslucencyPS<3> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
		case 4: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionFromTranslucencyPS<4> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
		case 5: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionFromTranslucencyPS<5> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
		default:
			check(0);
		}
	}
	else if (ShadowInfo->IsWholeSceneDirectionalShadow())
	{
		if (CVarFilterMethod.GetValueOnRenderThread() == 1)
		{
			if (ShadowInfo->CascadeSettings.FadePlaneLength > 0)
				BindShaderShaders<FShadowProjectionNoTransformVS, TDirectionalPercentageCloserShadowProjectionPS<5, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo);
			else
				BindShaderShaders<FShadowProjectionNoTransformVS, TDirectionalPercentageCloserShadowProjectionPS<5, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo);
		}
		else if (ShadowInfo->CascadeSettings.FadePlaneLength > 0)
		{
			if (ShadowInfo->bTransmission)
			{
				switch (Quality)
				{
				case 1: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<1, true, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 2: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<2, true, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 3: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<3, true, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 4: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<4, true, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 5: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<5, true, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				default:
					check(0);
				}
			}
			else
			{
				switch (Quality)
				{
				case 1: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<1, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 2: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<2, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 3: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<3, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 4: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<4, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 5: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<5, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				default:
					check(0);
				}
			}
		}
		else
		{
			if (ShadowInfo->bTransmission)
			{
				switch (Quality)
				{
				case 1: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<1, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 2: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<2, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 3: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<3, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 4: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<4, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 5: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<5, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				default:
					check(0);
				}
			}
			else
			{ 
				switch (Quality)
				{
				case 1: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<1, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 2: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<2, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 3: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<3, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 4: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<4, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 5: BindShaderShaders<FShadowProjectionNoTransformVS, TShadowProjectionPS<5, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				default:
					check(0);
				}
			}
		}
	}
	else
	{
		if(bMobileModulatedProjections)
		{
			switch (Quality)
			{
			case 1: BindShaderShaders<FShadowVolumeBoundProjectionVS, TModulatedShadowProjection<1> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 2: BindShaderShaders<FShadowVolumeBoundProjectionVS, TModulatedShadowProjection<2> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 3: BindShaderShaders<FShadowVolumeBoundProjectionVS, TModulatedShadowProjection<3> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 4: BindShaderShaders<FShadowVolumeBoundProjectionVS, TModulatedShadowProjection<4> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 5: BindShaderShaders<FShadowVolumeBoundProjectionVS, TModulatedShadowProjection<5> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			default:
				check(0);
			}
		}
		else if (ShadowInfo->bTransmission)
		{
			switch (Quality)
			{
			case 1: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<1, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 2: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<2, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 3: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<3, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 4: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<4, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			case 5: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<5, false, false, true> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
			default:
				check(0);
			}
		}
		else
		{
			if (CVarFilterMethod.GetValueOnRenderThread() == 1 && ShadowInfo->GetLightSceneInfo().Proxy->GetLightType() == LightType_Spot)
			{
				BindShaderShaders<FShadowVolumeBoundProjectionVS, TSpotPercentageCloserShadowProjectionPS<5, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo);
			}
			else
			{
				switch (Quality)
				{
				case 1: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<1, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 2: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<2, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 3: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<3, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 4: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<4, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				case 5: BindShaderShaders<FShadowVolumeBoundProjectionVS, TShadowProjectionPS<5, false> >(RHICmdList, GraphicsPSOInit, ViewIndex, View, HairVisibilityData, ShadowInfo); break;
				default:
					check(0);
				}
			}
		}
	}

	check(GraphicsPSOInit.BoundShaderState.VertexShaderRHI);
	check(GraphicsPSOInit.BoundShaderState.PixelShaderRHI);
}

FRHIBlendState* FProjectedShadowInfo::GetBlendStateForProjection(
	int32 ShadowMapChannel, 
	bool bIsWholeSceneDirectionalShadow,
	bool bUseFadePlane,
	bool bProjectingForForwardShading, 
	bool bMobileModulatedProjections)
{
	// With forward shading we are packing shadowing for all 4 possible stationary lights affecting each pixel into channels of the same texture, based on assigned shadowmap channels.
	// With deferred shading we have 4 channels for each light.  
	//	* CSM and per-object shadows are kept in separate channels to allow fading CSM out to precomputed shadowing while keeping per-object shadows past the fade distance.
	//	* Subsurface shadowing requires an extra channel for each

	FRHIBlendState* BlendState = nullptr;

	if (bProjectingForForwardShading)
	{
		if (bUseFadePlane)
		{
			if (ShadowMapChannel == 0)
			{
				// alpha is used to fade between cascades
				BlendState = TStaticBlendState<CW_RED, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
			}
			else if (ShadowMapChannel == 1)
			{
				BlendState = TStaticBlendState<CW_GREEN, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
			}
			else if (ShadowMapChannel == 2)
			{
				BlendState = TStaticBlendState<CW_BLUE, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
			}
			else if (ShadowMapChannel == 3)
			{
				BlendState = TStaticBlendState<CW_ALPHA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
			}
		}
		else
		{
			if (ShadowMapChannel == 0)
			{
				BlendState = TStaticBlendState<CW_RED, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI();
			}
			else if (ShadowMapChannel == 1)
			{
				BlendState = TStaticBlendState<CW_GREEN, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI();
			}
			else if (ShadowMapChannel == 2)
			{
				BlendState = TStaticBlendState<CW_BLUE, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI();
			}
			else if (ShadowMapChannel == 3)
			{
				BlendState = TStaticBlendState<CW_ALPHA, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI();
			}
		}

		checkf(BlendState, TEXT("Only shadows whose stationary lights have a valid ShadowMapChannel can be projected with forward shading"));
	}
	else
	{
		// Light Attenuation channel assignment:
		//  R:     WholeSceneShadows, non SSS
		//  G:     WholeSceneShadows,     SSS
		//  B: non WholeSceneShadows, non SSS
		//  A: non WholeSceneShadows,     SSS
		//
		// SSS: SubsurfaceScattering materials
		// non SSS: shadow for opaque materials
		// WholeSceneShadows: directional light CSM
		// non WholeSceneShadows: spotlight, per object shadows, translucency lighting, omni-directional lights

		if (bIsWholeSceneDirectionalShadow)
		{
			// Note: blend logic has to match ordering in FCompareFProjectedShadowInfoBySplitIndex.  For example the fade plane blend mode requires that shadow to be rendered first.
			// use R and G in Light Attenuation
			if (bUseFadePlane)
			{
				// alpha is used to fade between cascades, we don't don't need to do BO_Min as we leave B and A untouched which has translucency shadow
				BlendState = TStaticBlendState<CW_RG, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
			}
			else
			{
				// first cascade rendered doesn't require fading (CO_Min is needed to combine multiple shadow passes)
				// RTDF shadows: CO_Min is needed to combine with far shadows which overlap the same depth range
				BlendState = TStaticBlendState<CW_RG, BO_Min, BF_One, BF_One>::GetRHI();
			}
		}
		else
		{
			if (bMobileModulatedProjections)
			{
				// Color modulate shadows, ignore alpha.
				BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_Zero, BF_SourceColor, BO_Add, BF_Zero, BF_One>::GetRHI();
			}
			else
			{
				// use B and A in Light Attenuation
				// CO_Min is needed to combine multiple shadow passes
				BlendState = TStaticBlendState<CW_BA, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI();
			}
		}
	}

	return BlendState;
}

FRHIBlendState* FProjectedShadowInfo::GetBlendStateForProjection(bool bProjectingForForwardShading, bool bMobileModulatedProjections) const
{
	return GetBlendStateForProjection(
		GetLightSceneInfo().GetDynamicShadowMapChannel(),
		IsWholeSceneDirectionalShadow(),
		CascadeSettings.FadePlaneLength > 0 && !bRayTracedDistanceField,
		bProjectingForForwardShading,
		bMobileModulatedProjections);
}

void FProjectedShadowInfo::SetupFrustumForProjection(const FViewInfo* View, TArray<FVector4, TInlineAllocator<8>>& OutFrustumVertices, bool& bOutCameraInsideShadowFrustum, FPlane* OutPlanes) const
{
	bOutCameraInsideShadowFrustum = true;

	// Calculate whether the camera is inside the shadow frustum, or the near plane is potentially intersecting the frustum.
	if (!IsWholeSceneDirectionalShadow())
	{
		OutFrustumVertices.AddUninitialized(8);

		// The shadow transforms and view transforms are relative to different origins, so the world coordinates need to be translated.
		const FVector PreShadowToPreViewTranslation(View->ViewMatrices.GetPreViewTranslation() - PreShadowTranslation);

		// fill out the frustum vertices (this is only needed in the non-whole scene case)
		for(uint32 vZ = 0;vZ < 2;vZ++)
		{
			for(uint32 vY = 0;vY < 2;vY++)
			{
				for(uint32 vX = 0;vX < 2;vX++)
				{
					const FVector4 UnprojectedVertex = InvReceiverMatrix.TransformFVector4(
						FVector4(
							(vX ? -1.0f : 1.0f),
							(vY ? -1.0f : 1.0f),
							(vZ ?  0.0f : 1.0f),
							1.0f
							)
						);
					const FVector ProjectedVertex = UnprojectedVertex / UnprojectedVertex.W + PreShadowToPreViewTranslation;
					OutFrustumVertices[GetCubeVertexIndex(vX,vY,vZ)] = FVector4(ProjectedVertex, 0);
				}
			}
		}

		const FVector ShadowViewOrigin = View->ViewMatrices.GetViewOrigin();
		const FVector ShadowPreViewTranslation = View->ViewMatrices.GetPreViewTranslation();

		const FVector FrontTopRight		= OutFrustumVertices[GetCubeVertexIndex(0,0,1)] - ShadowPreViewTranslation;
		const FVector FrontTopLeft		= OutFrustumVertices[GetCubeVertexIndex(1,0,1)] - ShadowPreViewTranslation;
		const FVector FrontBottomLeft	= OutFrustumVertices[GetCubeVertexIndex(1,1,1)] - ShadowPreViewTranslation;
		const FVector FrontBottomRight	= OutFrustumVertices[GetCubeVertexIndex(0,1,1)] - ShadowPreViewTranslation;
		const FVector BackTopRight		= OutFrustumVertices[GetCubeVertexIndex(0,0,0)] - ShadowPreViewTranslation;
		const FVector BackTopLeft		= OutFrustumVertices[GetCubeVertexIndex(1,0,0)] - ShadowPreViewTranslation;
		const FVector BackBottomLeft	= OutFrustumVertices[GetCubeVertexIndex(1,1,0)] - ShadowPreViewTranslation;
		const FVector BackBottomRight	= OutFrustumVertices[GetCubeVertexIndex(0,1,0)] - ShadowPreViewTranslation;

		const FPlane Front(FrontTopRight, FrontTopLeft, FrontBottomLeft);
		const float FrontDistance = Front.PlaneDot(ShadowViewOrigin);

		const FPlane Right(BackBottomRight, BackTopRight, FrontTopRight);
		const float RightDistance = Right.PlaneDot(ShadowViewOrigin);

		const FPlane Back(BackTopLeft, BackTopRight, BackBottomRight);
		const float BackDistance = Back.PlaneDot(ShadowViewOrigin);

		const FPlane Left(FrontTopLeft, BackTopLeft, BackBottomLeft);
		const float LeftDistance = Left.PlaneDot(ShadowViewOrigin);

		const FPlane Top(BackTopRight, BackTopLeft, FrontTopLeft);
		const float TopDistance = Top.PlaneDot(ShadowViewOrigin);

		const FPlane Bottom(BackBottomLeft, BackBottomRight, FrontBottomLeft);
		const float BottomDistance = Bottom.PlaneDot(ShadowViewOrigin);

		OutPlanes[0] = Front;
		OutPlanes[1] = Right;
		OutPlanes[2] = Back;
		OutPlanes[3] = Left;
		OutPlanes[4] = Top;
		OutPlanes[5] = Bottom;

		// Use a distance threshold to treat the case where the near plane is intersecting the frustum as the camera being inside
		// The near plane handling is not exact since it just needs to be conservative about saying the camera is outside the frustum
		const float DistanceThreshold = -View->NearClippingDistance * 3.0f;

		bOutCameraInsideShadowFrustum = 
			FrontDistance > DistanceThreshold && 
			RightDistance > DistanceThreshold && 
			BackDistance > DistanceThreshold && 
			LeftDistance > DistanceThreshold && 
			TopDistance > DistanceThreshold && 
			BottomDistance > DistanceThreshold;
	}
}

void FProjectedShadowInfo::SetupProjectionStencilMask(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo* View, 
	int32 ViewIndex, 
	const FSceneRenderer* SceneRender,
	const TArray<FVector4, TInlineAllocator<8>>& FrustumVertices,
	bool bMobileModulatedProjections, 
	bool bCameraInsideShadowFrustum) const
{
	FMeshPassProcessorRenderState DrawRenderState(*View);

	// Depth test wo/ writes, no color writing.
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	DrawRenderState.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());

	const bool bDynamicInstancing = IsDynamicInstancingEnabled(View->FeatureLevel);

	// If this is a preshadow, mask the projection by the receiver primitives.
	if (bPreShadow || bSelfShadowOnly)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, EventMaskSubjects, TEXT("Stencil Mask Subjects"));

		// If instanced stereo is enabled, we need to render each view of the stereo pair using the instanced stereo transform to avoid bias issues.
		// TODO: Support instanced stereo properly in the projection stenciling pass.
		const bool bIsInstancedStereoEmulated = View->bIsInstancedStereoEnabled && !View->bIsMultiViewEnabled && IStereoRendering::IsStereoEyeView(*View);
		if (bIsInstancedStereoEmulated)
		{
			RHICmdList.SetViewport(0, 0, 0, SceneRender->InstancedStereoWidth, View->ViewRect.Max.Y, 1);
			RHICmdList.SetScissorRect(true, View->ViewRect.Min.X, View->ViewRect.Min.Y, View->ViewRect.Max.X, View->ViewRect.Max.Y);
		}

		const FShadowMeshDrawCommandPass& ProjectionStencilingPass = ProjectionStencilingPasses[ViewIndex];
		if (ProjectionStencilingPass.VisibleMeshDrawCommands.Num() > 0)
		{
			SubmitMeshDrawCommands(ProjectionStencilingPass.VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, ProjectionStencilingPass.PrimitiveIdVertexBuffer, 0, bDynamicInstancing, bIsInstancedStereoEmulated ? 2 : 1, RHICmdList);
		}

		// Restore viewport
		if (bIsInstancedStereoEmulated)
		{
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
			RHICmdList.SetViewport(View->ViewRect.Min.X, View->ViewRect.Min.Y, 0.0f, View->ViewRect.Max.X, View->ViewRect.Max.Y, 1.0f);
		}
		
	}
	else if (IsWholeSceneDirectionalShadow())
	{
		// Increment stencil on front-facing zfail, decrement on back-facing zfail.
		DrawRenderState.SetDepthStencilState(
			TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Always, SO_Keep, SO_Increment, SO_Keep,
			true, CF_Always, SO_Keep, SO_Decrement, SO_Keep,
			0xff, 0xff
			>::GetRHI());

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		DrawRenderState.ApplyToPSO(GraphicsPSOInit);

		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

		checkSlow(CascadeSettings.ShadowSplitIndex >= 0);
		checkSlow(bDirectionalLight);

		// Draw 2 fullscreen planes, front facing one at the near subfrustum plane, and back facing one at the far.

		// Find the projection shaders.
		TShaderMapRef<FShadowProjectionNoTransformVS> VertexShaderNoTransform(View->ShaderMap);
		VertexShaderNoTransform->SetParameters(RHICmdList, View->ViewUniformBuffer);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShaderNoTransform.GetVertexShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		FVector4 Near = View->ViewMatrices.GetProjectionMatrix().TransformFVector4(FVector4(0, 0, CascadeSettings.SplitNear));
		FVector4 Far = View->ViewMatrices.GetProjectionMatrix().TransformFVector4(FVector4(0, 0, CascadeSettings.SplitFar));
		float StencilNear = Near.Z / Near.W;
		float StencilFar = Far.Z / Far.W;

		FRHIResourceCreateInfo CreateInfo;
		FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector4) * 12, BUF_Volatile, CreateInfo);
		void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector4) * 12, RLM_WriteOnly);

		// Generate the vertices used
		FVector4* Vertices = (FVector4*)VoidPtr;

			// Far Plane
		Vertices[0] = FVector4( 1,  1,  StencilFar);
		Vertices[1] = FVector4(-1,  1,  StencilFar);
		Vertices[2] = FVector4( 1, -1,  StencilFar);
		Vertices[3] = FVector4( 1, -1,  StencilFar);
		Vertices[4] = FVector4(-1,  1,  StencilFar);
		Vertices[5] = FVector4(-1, -1,  StencilFar);

			// Near Plane
		Vertices[6]  = FVector4(-1,  1, StencilNear);
		Vertices[7]  = FVector4( 1,  1, StencilNear);
		Vertices[8]  = FVector4(-1, -1, StencilNear);
		Vertices[9]  = FVector4(-1, -1, StencilNear);
		Vertices[10] = FVector4( 1,  1, StencilNear);
		Vertices[11] = FVector4( 1, -1, StencilNear);

		RHIUnlockVertexBuffer(VertexBufferRHI);
		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		RHICmdList.DrawPrimitive(0, (CascadeSettings.ShadowSplitIndex > 0) ? 4 : 2, 1);
	}
	// Not a preshadow, mask the projection to any pixels inside the frustum.
	else
	{
		if (bCameraInsideShadowFrustum)
		{
			// Use zfail stenciling when the camera is inside the frustum or the near plane is potentially clipping, 
			// Because zfail handles these cases while zpass does not.
			// zfail stenciling is somewhat slower than zpass because on modern GPUs HiZ will be disabled when setting up stencil.
			// Increment stencil on front-facing zfail, decrement on back-facing zfail.
			DrawRenderState.SetDepthStencilState(
				TStaticDepthStencilState<
				false, CF_DepthNearOrEqual,
				true, CF_Always, SO_Keep, SO_Increment, SO_Keep,
				true, CF_Always, SO_Keep, SO_Decrement, SO_Keep,
				0xff, 0xff
				>::GetRHI());
		}
		else
		{
			// Increment stencil on front-facing zpass, decrement on back-facing zpass.
			// HiZ will be enabled on modern GPUs which will save a little GPU time.
			DrawRenderState.SetDepthStencilState(
				TStaticDepthStencilState<
				false, CF_DepthNearOrEqual,
				true, CF_Always, SO_Keep, SO_Keep, SO_Increment,
				true, CF_Always, SO_Keep, SO_Keep, SO_Decrement,
				0xff, 0xff
				>::GetRHI());
		}
		
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		DrawRenderState.ApplyToPSO(GraphicsPSOInit);
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

		// Find the projection shaders.
		TShaderMapRef<FShadowVolumeBoundProjectionVS> VertexShader(View->ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		// Set the projection vertex shader parameters
		VertexShader->SetParameters(RHICmdList, *View, this);

		FRHIResourceCreateInfo CreateInfo;
		FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector4) * FrustumVertices.Num(), BUF_Volatile, CreateInfo);
		void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector4) * FrustumVertices.Num(), RLM_WriteOnly);
		FPlatformMemory::Memcpy(VoidPtr, FrustumVertices.GetData(), sizeof(FVector4) * FrustumVertices.Num());
		RHIUnlockVertexBuffer(VertexBufferRHI);

		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		// Draw the frustum using the stencil buffer to mask just the pixels which are inside the shadow frustum.
		RHICmdList.DrawIndexedPrimitive(GCubeIndexBuffer.IndexBufferRHI, 0, 0, 8, 0, 12, 1);
		VertexBufferRHI.SafeRelease();

		// if rendering modulated shadows mask out subject mesh elements to prevent self shadowing.
		if (bMobileModulatedProjections && !CVarEnableModulatedSelfShadow.GetValueOnRenderThread())
		{
			const FShadowMeshDrawCommandPass& ProjectionStencilingPass = ProjectionStencilingPasses[ViewIndex];
			if (ProjectionStencilingPass.VisibleMeshDrawCommands.Num() > 0)
			{
				SubmitMeshDrawCommands(ProjectionStencilingPass.VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, ProjectionStencilingPass.PrimitiveIdVertexBuffer, 0, bDynamicInstancing, 1, RHICmdList);
			}
		}
	}
}

void FProjectedShadowInfo::RenderProjection(FRHICommandListImmediate& RHICmdList, int32 ViewIndex, const FViewInfo* View, const FSceneRenderer* SceneRender, bool bProjectingForForwardShading, bool bMobileModulatedProjections, const FHairStrandsVisibilityData* HairVisibilityData, const FHairStrandsMacroGroupDatas* HairMacroGroupData) const
{
#if WANTS_DRAW_MESH_EVENTS
	FString EventName;

	if (GetEmitDrawEvents())
	{
		GetShadowTypeNameForDrawEvent(EventName);
	}
	SCOPED_DRAW_EVENTF(RHICmdList, EventShadowProjectionActor, *EventName);
#endif

	FScopeCycleCounter Scope(bWholeSceneShadow ? GET_STATID(STAT_RenderWholeSceneShadowProjectionsTime) : GET_STATID(STAT_RenderPerObjectShadowProjectionsTime));

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Find the shadow's view relevance.
	const FVisibleLightViewInfo& VisibleLightViewInfo = View->VisibleLightInfos[LightSceneInfo->Id];
	{
		FPrimitiveViewRelevance ViewRelevance = VisibleLightViewInfo.ProjectedShadowViewRelevanceMap[ShadowId];

		// Don't render shadows for subjects which aren't view relevant.
		if (ViewRelevance.bShadowRelevance == false)
		{
			return;
		}
	}

	bool bCameraInsideShadowFrustum;
	TArray<FVector4, TInlineAllocator<8>> FrustumVertices;
	FPlane OutPlanes[6];
	SetupFrustumForProjection(View, FrustumVertices, bCameraInsideShadowFrustum, OutPlanes);

	const bool bSubPixelSupport = HairVisibilityData != nullptr;
	const bool bStencilTestEnabled = true;// !bSubPixelSupport;
	const bool bDepthBoundsTestEnabled = IsWholeSceneDirectionalShadow() && GSupportsDepthBoundsTest && CVarCSMDepthBoundsTest.GetValueOnRenderThread() != 0;// && !bSubPixelSupport;

	if (bSubPixelSupport)
	{
		// Do not apply pre-shadow on hair, as this is intended only for targed opaque geometry
		if (bPreShadow)
		{
			return;
		}

		const bool bValidPlanes = FrustumVertices.Num() > 0;
		if (bValidPlanes && CVarHairStrandsCullPerObjectShadowCaster.GetValueOnRenderThread() > 0)
		{
			// Skip volume which does not intersect hair clusters
			bool bIntersect = bValidPlanes;
			for (const FHairStrandsMacroGroupData& Data : HairMacroGroupData->Datas)
			{
				const FSphere BoundSphere = Data.Bounds.GetSphere();
				// Return the signed distance to the plane. The planes are pointing inward
				const float D0 = -OutPlanes[0].PlaneDot(BoundSphere.Center);
				const float D1 = -OutPlanes[1].PlaneDot(BoundSphere.Center);
				const float D2 = -OutPlanes[2].PlaneDot(BoundSphere.Center);
				const float D3 = -OutPlanes[3].PlaneDot(BoundSphere.Center);
				const float D4 = -OutPlanes[4].PlaneDot(BoundSphere.Center);
				const float D5 = -OutPlanes[5].PlaneDot(BoundSphere.Center);

				const bool bOutside =
					D0 - BoundSphere.W > 0 ||
					D1 - BoundSphere.W > 0 ||
					D2 - BoundSphere.W > 0 ||
					D3 - BoundSphere.W > 0 ||
					D4 - BoundSphere.W > 0 ||
					D5 - BoundSphere.W > 0;

				bIntersect = !bOutside;
				if (bIntersect)
				{
					break;
				}
			}

			// The light frustum does not intersect the hair cluster, and thus doesn't have any interacction with it, and the shadow mask computation is not needed in this case
			if (!bIntersect)
			{
				return;
			}
		}
	}
	
	if (!bDepthBoundsTestEnabled && bStencilTestEnabled)
	{
		SetupProjectionStencilMask(RHICmdList, View, ViewIndex, SceneRender, FrustumVertices, bMobileModulatedProjections, bCameraInsideShadowFrustum);
	}

	// solid rasterization w/ back-face culling.
	GraphicsPSOInit.RasterizerState = (View->bReverseCulling || IsWholeSceneDirectionalShadow()) ? TStaticRasterizerState<FM_Solid,CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid,CM_CW>::GetRHI();

	GraphicsPSOInit.bDepthBounds = bDepthBoundsTestEnabled;
	if (bDepthBoundsTestEnabled)
	{
		// no depth test or writes
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	}
	else if (bStencilTestEnabled)
	{
		if (GStencilOptimization)
		{
			// No depth test or writes, zero the stencil
			// Note: this will disable hi-stencil on many GPUs, but still seems 
			// to be faster. However, early stencil still works 
			GraphicsPSOInit.DepthStencilState =
				TStaticDepthStencilState<
				false, CF_Always,
				true, CF_NotEqual, SO_Zero, SO_Zero, SO_Zero,
				false, CF_Always, SO_Zero, SO_Zero, SO_Zero,
				0xff, 0xff
				>::GetRHI();
		}
		else
		{
			// no depth test or writes, Test stencil for non-zero.
			GraphicsPSOInit.DepthStencilState = 
				TStaticDepthStencilState<
				false, CF_Always,
				true, CF_NotEqual, SO_Keep, SO_Keep, SO_Keep,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				0xff, 0xff
				>::GetRHI();
		}
	}
	else
	{
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	}


	GraphicsPSOInit.BlendState = GetBlendStateForProjection(bProjectingForForwardShading, bMobileModulatedProjections);

	GraphicsPSOInit.PrimitiveType = IsWholeSceneDirectionalShadow() ? PT_TriangleStrip : PT_TriangleList;

	{
		uint32 LocalQuality = GetShadowQuality();

		if (LocalQuality > 1)
		{
			if (IsWholeSceneDirectionalShadow() && CascadeSettings.ShadowSplitIndex > 0)
			{
				// adjust kernel size so that the penumbra size of distant splits will better match up with the closer ones
				const float SizeScale = CascadeSettings.ShadowSplitIndex / FMath::Max(0.001f, CVarCSMSplitPenumbraScale.GetValueOnRenderThread());
			}
			else if (LocalQuality > 2 && !bWholeSceneShadow)
			{
				static auto CVarPreShadowResolutionFactor = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Shadow.PreShadowResolutionFactor"));
				const int32 TargetResolution = bPreShadow ? FMath::TruncToInt(512 * CVarPreShadowResolutionFactor->GetValueOnRenderThread()) : 512;

				int32 Reduce = 0;

				{
					int32 Res = ResolutionX;

					while (Res < TargetResolution)
					{
						Res *= 2;
						++Reduce;
					}
				}

				// Never drop to quality 1 due to low resolution, aliasing is too bad
				LocalQuality = FMath::Clamp((int32)LocalQuality - Reduce, 3, 5);
			}
		}

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		BindShadowProjectionShaders(LocalQuality, RHICmdList, GraphicsPSOInit, ViewIndex, *View, HairVisibilityData, this, bMobileModulatedProjections);

		if (bDepthBoundsTestEnabled)
		{
			SetDepthBoundsTest(RHICmdList, CascadeSettings.SplitNear, CascadeSettings.SplitFar, View->ViewMatrices.GetProjectionMatrix());
		}

		RHICmdList.SetStencilRef(0);
	}

	if (IsWholeSceneDirectionalShadow())
	{
		RHICmdList.SetStreamSource(0, GClearVertexBuffer.VertexBufferRHI, 0);
		RHICmdList.DrawPrimitive(0, 2, 1);
	}
	else
	{
		FRHIResourceCreateInfo CreateInfo;
		FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector4) * FrustumVertices.Num(), BUF_Volatile, CreateInfo);
		void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector4) * FrustumVertices.Num(), RLM_WriteOnly);
		FPlatformMemory::Memcpy(VoidPtr, FrustumVertices.GetData(), sizeof(FVector4) * FrustumVertices.Num());
		RHIUnlockVertexBuffer(VertexBufferRHI);

		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		// Draw the frustum using the projection shader..
		RHICmdList.DrawIndexedPrimitive(GCubeIndexBuffer.IndexBufferRHI, 0, 0, 8, 0, 12, 1);
		VertexBufferRHI.SafeRelease();
	}

	if (!bDepthBoundsTestEnabled && bStencilTestEnabled)
	{
		// Clear the stencil buffer to 0.
		if (!GStencilOptimization)
		{
			DrawClearQuad(RHICmdList, false, FLinearColor::Transparent, false, 0, true, 0);
		}
	}
}


template <uint32 Quality, bool bUseTransmission, bool bUseSubPixel>
static void SetPointLightShaderTempl(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, int32 ViewIndex, const FViewInfo& View, const FProjectedShadowInfo* ShadowInfo, const FHairStrandsVisibilityData* HairVisibilityData)
{
	TShaderMapRef<FShadowVolumeBoundProjectionVS> VertexShader(View.ShaderMap);
	TShaderMapRef<TOnePassPointShadowProjectionPS<Quality,bUseTransmission,bUseSubPixel> > PixelShader(View.ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	
	VertexShader->SetParameters(RHICmdList, View, ShadowInfo);
	PixelShader->SetParameters(RHICmdList, ViewIndex, View, HairVisibilityData, ShadowInfo);
}

/** Render one pass point light shadow projections. */
void FProjectedShadowInfo::RenderOnePassPointLightProjection(FRHICommandListImmediate& RHICmdList, int32 ViewIndex, const FViewInfo& View, bool bProjectingForForwardShading, const FHairStrandsVisibilityData* HairVisibilityData, const FHairStrandsMacroGroupDatas* HairMacroGroupData) const
{
	SCOPE_CYCLE_COUNTER(STAT_RenderWholeSceneShadowProjectionsTime);

	checkSlow(bOnePassPointLightShadow);
	
	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();

	bool bUseTransmission = LightSceneInfo->Proxy->Transmission();

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = GetBlendStateForProjection(bProjectingForForwardShading, false);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const bool bCameraInsideLightGeometry = ((FVector)View.ViewMatrices.GetViewOrigin() - LightBounds.Center).SizeSquared() < FMath::Square(LightBounds.W * 1.05f + View.NearClippingDistance * 2.0f);

	if (bCameraInsideLightGeometry)
	{
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		// Render backfaces with depth tests disabled since the camera is inside (or close to inside) the light geometry
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
	}
	else
	{
		// Render frontfaces with depth tests on to get the speedup from HiZ since the camera is outside the light geometry
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
	}	

	{
		uint32 LocalQuality = GetShadowQuality();

		if(LocalQuality > 1)
		{
			// adjust kernel size so that the penumbra size of distant splits will better match up with the closer ones
			//const float SizeScale = ShadowInfo->ResolutionX;
			int32 Reduce = 0;

			{
				int32 Res = ResolutionX;

				while(Res < 512)
				{
					Res *= 2;
					++Reduce;
				}
			}
		}

		const bool bSubPixelSupport = HairVisibilityData != nullptr;
		if (bSubPixelSupport)
		{
			// Do not apply pre-shadow on hair, as this is intended only for targed opaque geometry
			if (bPreShadow)
			{
				return;
			}

			// Skip volume which does not intersect hair clusters
			bool bIntersect = false;
			if (CVarHairStrandsCullPerObjectShadowCaster.GetValueOnRenderThread() > 0)
			{
				for (const FHairStrandsMacroGroupData& Data : HairMacroGroupData->Datas)
				{
					const FSphere BoundSphere = Data.Bounds.GetSphere();
					if (BoundSphere.Intersects(LightBounds))
					{
						bIntersect = true;
						break;
					}
				}

				// The light frustum does not intersect the hair cluster, and thus doesn't have any interacction with it, and the shadow mask computation is not needed in this case
				if (!bIntersect)
				{
					return;
				}
			}

			switch (LocalQuality)
			{
				case 1: SetPointLightShaderTempl<1, false, true>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, HairVisibilityData); break;
				case 2: SetPointLightShaderTempl<2, false, true>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, HairVisibilityData); break;
				case 3: SetPointLightShaderTempl<3, false, true>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, HairVisibilityData); break;
				case 4: SetPointLightShaderTempl<4, false, true>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, HairVisibilityData); break;
				case 5: SetPointLightShaderTempl<5, false, true>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, HairVisibilityData); break;
				default:
					check(0);
			}
		}
		else if (bUseTransmission)
		{
			switch (LocalQuality)
			{
				case 1: SetPointLightShaderTempl<1, true, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 2: SetPointLightShaderTempl<2, true, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 3: SetPointLightShaderTempl<3, true, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 4: SetPointLightShaderTempl<4, true, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 5: SetPointLightShaderTempl<5, true, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				default:
					check(0);
			}
		}
		else
		{
			switch (LocalQuality)
			{
				case 1: SetPointLightShaderTempl<1, false, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 2: SetPointLightShaderTempl<2, false, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 3: SetPointLightShaderTempl<3, false, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 4: SetPointLightShaderTempl<4, false, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				case 5: SetPointLightShaderTempl<5, false, false>(RHICmdList, GraphicsPSOInit, ViewIndex, View, this, nullptr); break;
				default:
					check(0);
			}
		}
	}

	// Project the point light shadow with some approximately bounding geometry, 
	// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
	StencilingGeometry::DrawSphere(RHICmdList);
}

void FProjectedShadowInfo::RenderFrustumWireframe(FPrimitiveDrawInterface* PDI) const
{
	// Find the ID of an arbitrary subject primitive to use to color the shadow frustum.
	int32 SubjectPrimitiveId = 0;
	if(DynamicSubjectPrimitives.Num())
	{
		SubjectPrimitiveId = DynamicSubjectPrimitives[0]->GetIndex();
	}

	const FMatrix InvShadowTransform = (bWholeSceneShadow || bPreShadow) ? SubjectAndReceiverMatrix.InverseFast() : InvReceiverMatrix;

	FColor Color;

	if(IsWholeSceneDirectionalShadow())
	{
		Color = FColor::White;
		switch(CascadeSettings.ShadowSplitIndex)
		{
			case 0: Color = FColor::Red; break;
			case 1: Color = FColor::Yellow; break;
			case 2: Color = FColor::Green; break;
			case 3: Color = FColor::Blue; break;
		}
	}
	else
	{
		Color = FLinearColor::MakeFromHSV8(( ( SubjectPrimitiveId + LightSceneInfo->Id ) * 31 ) & 255, 0, 255).ToFColor(true);
	}

	// Render the wireframe for the frustum derived from ReceiverMatrix.
	DrawFrustumWireframe(
		PDI,
		InvShadowTransform * FTranslationMatrix(-PreShadowTranslation),
		Color,
		SDPG_World
		);
}

FMatrix FProjectedShadowInfo::GetScreenToShadowMatrix(const FSceneView& View, uint32 TileOffsetX, uint32 TileOffsetY, uint32 TileResolutionX, uint32 TileResolutionY) const
{
	const FIntPoint ShadowBufferResolution = GetShadowBufferResolution();
	const float InvBufferResolutionX = 1.0f / (float)ShadowBufferResolution.X;
	const float ShadowResolutionFractionX = 0.5f * (float)TileResolutionX * InvBufferResolutionX;
	const float InvBufferResolutionY = 1.0f / (float)ShadowBufferResolution.Y;
	const float ShadowResolutionFractionY = 0.5f * (float)TileResolutionY * InvBufferResolutionY;
	// Calculate the matrix to transform a screenspace position into shadow map space

	FMatrix ScreenToShadow;
	FMatrix ViewDependentTransform =
		// Z of the position being transformed is actually view space Z, 
			// Transform it into post projection space by applying the projection matrix,
			// Which is the required space before applying View.InvTranslatedViewProjectionMatrix
		FMatrix(
			FPlane(1,0,0,0),
			FPlane(0,1,0,0),
			FPlane(0,0,View.ViewMatrices.GetProjectionMatrix().M[2][2],1),
			FPlane(0,0,View.ViewMatrices.GetProjectionMatrix().M[3][2],0)) *
		// Transform the post projection space position into translated world space
		// Translated world space is normal world space translated to the view's origin, 
		// Which prevents floating point imprecision far from the world origin.
		View.ViewMatrices.GetInvTranslatedViewProjectionMatrix() *
		FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation());

	FMatrix ShadowMapDependentTransform =
		// Translate to the origin of the shadow's translated world space
		FTranslationMatrix(PreShadowTranslation) *
		// Transform into the shadow's post projection space
		// This has to be the same transform used to render the shadow depths
		SubjectAndReceiverMatrix *
		// Scale and translate x and y to be texture coordinates into the ShadowInfo's rectangle in the shadow depth buffer
		// Normalize z by MaxSubjectDepth, as was done when writing shadow depths
		FMatrix(
			FPlane(ShadowResolutionFractionX,0,							0,									0),
			FPlane(0,						 -ShadowResolutionFractionY,0,									0),
			FPlane(0,						0,							InvMaxSubjectDepth,	0),
			FPlane(
				(TileOffsetX + BorderSize) * InvBufferResolutionX + ShadowResolutionFractionX,
				(TileOffsetY + BorderSize) * InvBufferResolutionY + ShadowResolutionFractionY,
				0,
				1
				)
			);

	if (View.bIsMobileMultiViewEnabled && View.Family->Views.Num() > 0)
	{
		// In Multiview, we split ViewDependentTransform out into ViewUniformShaderParameters.MobileMultiviewShadowTransform
		// So we can multiply it later in shader.
		ScreenToShadow = ShadowMapDependentTransform;
	}
	else
	{
		ScreenToShadow = ViewDependentTransform * ShadowMapDependentTransform;
	}
	return ScreenToShadow;
}

FMatrix FProjectedShadowInfo::GetWorldToShadowMatrix(FVector4& ShadowmapMinMax, const FIntPoint* ShadowBufferResolutionOverride) const
{
	FIntPoint ShadowBufferResolution = ( ShadowBufferResolutionOverride ) ? *ShadowBufferResolutionOverride : GetShadowBufferResolution();

	const float InvBufferResolutionX = 1.0f / (float)ShadowBufferResolution.X;
	const float ShadowResolutionFractionX = 0.5f * (float)ResolutionX * InvBufferResolutionX;
	const float InvBufferResolutionY = 1.0f / (float)ShadowBufferResolution.Y;
	const float ShadowResolutionFractionY = 0.5f * (float)ResolutionY * InvBufferResolutionY;

	const FMatrix WorldToShadowMatrix =
		// Translate to the origin of the shadow's translated world space
		FTranslationMatrix(PreShadowTranslation) *
		// Transform into the shadow's post projection space
		// This has to be the same transform used to render the shadow depths
		SubjectAndReceiverMatrix *
		// Scale and translate x and y to be texture coordinates into the ShadowInfo's rectangle in the shadow depth buffer
		// Normalize z by MaxSubjectDepth, as was done when writing shadow depths
		FMatrix(
			FPlane(ShadowResolutionFractionX,0,							0,									0),
			FPlane(0,						 -ShadowResolutionFractionY,0,									0),
			FPlane(0,						0,							InvMaxSubjectDepth,	0),
			FPlane(
				(X + BorderSize) * InvBufferResolutionX + ShadowResolutionFractionX,
				(Y + BorderSize) * InvBufferResolutionY + ShadowResolutionFractionY,
				0,
				1
			)
		);

	ShadowmapMinMax = FVector4(
		(X + BorderSize) * InvBufferResolutionX, 
		(Y + BorderSize) * InvBufferResolutionY,
		(X + BorderSize * 2 + ResolutionX) * InvBufferResolutionX, 
		(Y + BorderSize * 2 + ResolutionY) * InvBufferResolutionY);

	return WorldToShadowMatrix;
}

void FProjectedShadowInfo::UpdateShaderDepthBias()
{
	float DepthBias = 0;
	float SlopeScaleDepthBias = 1;

	if (IsWholeScenePointLightShadow())
	{
		const bool bIsRectLight = LightSceneInfo->Proxy->GetLightType() == LightType_Rect;
		float DeptBiasConstant = 0;
		float SlopeDepthBiasConstant = 0;
		if (bIsRectLight)
		{
			DeptBiasConstant = CVarRectLightShadowDepthBias.GetValueOnRenderThread();
			SlopeDepthBiasConstant = CVarRectLightShadowSlopeScaleDepthBias.GetValueOnRenderThread();
		}
		else
		{
			DeptBiasConstant = CVarPointLightShadowDepthBias.GetValueOnRenderThread();
			SlopeDepthBiasConstant = CVarPointLightShadowSlopeScaleDepthBias.GetValueOnRenderThread();
		}

		DepthBias = DeptBiasConstant * 512.0f / FMath::Max(ResolutionX, ResolutionY);
		// * 2.0f to be compatible with the system we had before ShadowBias
		DepthBias *= 2.0f * LightSceneInfo->Proxy->GetUserShadowBias();

		SlopeScaleDepthBias = SlopeDepthBiasConstant;
		SlopeScaleDepthBias *= LightSceneInfo->Proxy->GetUserShadowSlopeBias();
	}
	else if (IsWholeSceneDirectionalShadow())
	{
		check(CascadeSettings.ShadowSplitIndex >= 0);

		// the z range is adjusted to we need to adjust here as well
		DepthBias = CVarCSMShadowDepthBias.GetValueOnRenderThread() / (MaxSubjectZ - MinSubjectZ);
		const float WorldSpaceTexelScale = ShadowBounds.W / ResolutionX;
		DepthBias = FMath::Lerp(DepthBias, DepthBias * WorldSpaceTexelScale, CascadeSettings.CascadeBiasDistribution);
		DepthBias *= LightSceneInfo->Proxy->GetUserShadowBias();

		SlopeScaleDepthBias = CVarCSMShadowSlopeScaleDepthBias.GetValueOnRenderThread();
		SlopeScaleDepthBias *= LightSceneInfo->Proxy->GetUserShadowSlopeBias();
	}
	else if (bPreShadow)
	{
		// Preshadows don't need a depth bias since there is no self shadowing
		DepthBias = 0;
		SlopeScaleDepthBias = 0;
	}
	else
	{
		// per object shadows
		if(bDirectionalLight)
		{
			// we use CSMShadowDepthBias cvar but this is per object shadows, maybe we want to use different settings

			// the z range is adjusted to we need to adjust here as well
			DepthBias = CVarPerObjectDirectionalShadowDepthBias.GetValueOnRenderThread() / (MaxSubjectZ - MinSubjectZ);

			float WorldSpaceTexelScale = ShadowBounds.W / FMath::Max(ResolutionX, ResolutionY);
		
			DepthBias *= WorldSpaceTexelScale;
			DepthBias *= 0.5f;	// avg GetUserShadowBias, in that case we don't want this adjustable

			SlopeScaleDepthBias = CVarPerObjectDirectionalShadowSlopeScaleDepthBias.GetValueOnRenderThread();
			SlopeScaleDepthBias *= LightSceneInfo->Proxy->GetUserShadowSlopeBias();
		}
		else
		{
			// spot lights (old code, might need to be improved)
			const float LightTypeDepthBias = CVarSpotLightShadowDepthBias.GetValueOnRenderThread();
			DepthBias = LightTypeDepthBias * 512.0f / ((MaxSubjectZ - MinSubjectZ) * FMath::Max(ResolutionX, ResolutionY));
			// * 2.0f to be compatible with the system we had before ShadowBias
			DepthBias *= 2.0f * LightSceneInfo->Proxy->GetUserShadowBias();

			SlopeScaleDepthBias = CVarSpotLightShadowSlopeScaleDepthBias.GetValueOnRenderThread();
			SlopeScaleDepthBias *= LightSceneInfo->Proxy->GetUserShadowSlopeBias();
		}

		// Prevent a large depth bias due to low resolution from causing near plane clipping
		DepthBias = FMath::Min(DepthBias, .1f);
	}

	ShaderDepthBias = FMath::Max(DepthBias, 0.0f);
	ShaderSlopeDepthBias = FMath::Max(DepthBias * SlopeScaleDepthBias, 0.0f);
	ShaderMaxSlopeDepthBias = CVarShadowMaxSlopeScaleDepthBias.GetValueOnRenderThread();
}

float FProjectedShadowInfo::ComputeTransitionSize() const
{
	float TransitionSize = 1.0f;

	if (IsWholeScenePointLightShadow())
	{
		// todo: optimize
		TransitionSize = bDirectionalLight ? (1.0f / CVarShadowTransitionScale.GetValueOnRenderThread()) : (1.0f / CVarSpotLightShadowTransitionScale.GetValueOnRenderThread());
		// * 2.0f to be compatible with the system we had before ShadowBias
		TransitionSize *= 2.0f * LightSceneInfo->Proxy->GetUserShadowBias();
	}
	else if (IsWholeSceneDirectionalShadow())
	{
		check(CascadeSettings.ShadowSplitIndex >= 0);

		// todo: remove GetShadowTransitionScale()
		// make 1/ ShadowTransitionScale, SpotLightShadowTransitionScale

		// the z range is adjusted to we need to adjust here as well
		TransitionSize = CVarCSMShadowDepthBias.GetValueOnRenderThread() / (MaxSubjectZ - MinSubjectZ);

		float WorldSpaceTexelScale = ShadowBounds.W / ResolutionX;

		TransitionSize *= WorldSpaceTexelScale;
		TransitionSize *= LightSceneInfo->Proxy->GetUserShadowBias();
	}
	else if (bPreShadow)
	{
		// Preshadows don't have self shadowing, so make sure the shadow starts as close to the caster as possible
		TransitionSize = 0.0f;
	}
	else
	{
		// todo: optimize
		TransitionSize = bDirectionalLight ? (1.0f / CVarShadowTransitionScale.GetValueOnRenderThread()) : (1.0f / CVarSpotLightShadowTransitionScale.GetValueOnRenderThread());
		// * 2.0f to be compatible with the system we had before ShadowBias
		TransitionSize *= 2.0f * LightSceneInfo->Proxy->GetUserShadowBias();
	}

	// Make sure that shadow soft transition size is greater than zero so 1/TransitionSize shader parameter won't be INF.
	const float MinTransitionSize = 0.00001f;
	return FMath::Max(TransitionSize, MinTransitionSize);
}

float FProjectedShadowInfo::GetShaderReceiverDepthBias() const
{
	float ShadowReceiverBias = 1;
	{
		switch (GetLightSceneInfo().Proxy->GetLightType())
		{
		case LightType_Directional	: ShadowReceiverBias = CVarCSMShadowReceiverBias.GetValueOnRenderThread(); break;
		case LightType_Rect			: ShadowReceiverBias = CVarRectLightShadowReceiverBias.GetValueOnRenderThread(); break;
		case LightType_Spot			: ShadowReceiverBias = CVarSpotLightShadowReceiverBias.GetValueOnRenderThread(); break;
		case LightType_Point		: ShadowReceiverBias = GetShaderSlopeDepthBias(); break;
		}
	}

	// Return the min lerp value for depth biasing
	// 0 : max bias when NoL == 0
	// 1 : no bias
	return 1.0f - FMath::Clamp(ShadowReceiverBias, 0.0f, 1.0f);
}
/*-----------------------------------------------------------------------------
FDeferredShadingSceneRenderer
-----------------------------------------------------------------------------*/

/**
 * Used by RenderLights to figure out if projected shadows need to be rendered to the attenuation buffer.
 *
 * @param LightSceneInfo Represents the current light
 * @return true if anything needs to be rendered
 */
bool FSceneRenderer::CheckForProjectedShadows( const FLightSceneInfo* LightSceneInfo ) const
{
	// If light has ray-traced occlusion enabled, then it will project some shadows. No need 
	// for doing a lookup through shadow maps data
	const FLightOcclusionType LightOcclusionType = GetLightOcclusionType(*LightSceneInfo->Proxy);
	if (LightOcclusionType == FLightOcclusionType::Raytraced)
		return true;

	// Find the projected shadows cast by this light.
	const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];
	for( int32 ShadowIndex=0; ShadowIndex<VisibleLightInfo.AllProjectedShadows.Num(); ShadowIndex++ )
	{
		const FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.AllProjectedShadows[ShadowIndex];

		// Check that the shadow is visible in at least one view before rendering it.
		bool bShadowIsVisible = false;
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			if (ProjectedShadowInfo->DependentView && ProjectedShadowInfo->DependentView != &View)
			{
				continue;
			}
			const FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightSceneInfo->Id];
			bShadowIsVisible |= VisibleLightViewInfo.ProjectedShadowVisibilityMap[ShadowIndex];
		}

		if(bShadowIsVisible)
		{
			return true;
		}
	}
	return false;
}

bool FDeferredShadingSceneRenderer::InjectReflectiveShadowMaps(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo* LightSceneInfo)
{
	FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

	// Inject the RSM into the LPVs
	for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.RSMsToProject.Num(); ShadowIndex++)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.RSMsToProject[ShadowIndex];

		check(ProjectedShadowInfo->bReflectiveShadowmap);

		if (ProjectedShadowInfo->bAllocated && ProjectedShadowInfo->DependentView)
		{
			FSceneViewState* ViewState = (FSceneViewState*)ProjectedShadowInfo->DependentView->State;

			FLightPropagationVolume* LightPropagationVolume = ViewState ? ViewState->GetLightPropagationVolume(FeatureLevel) : NULL;

			if (LightPropagationVolume)
			{
				if (ProjectedShadowInfo->bWholeSceneShadow)
				{
					LightPropagationVolume->InjectDirectionalLightRSM( 
						RHICmdList, 
						*ProjectedShadowInfo->DependentView,
						(const FTexture2DRHIRef&)ProjectedShadowInfo->RenderTargets.ColorTargets[0]->GetRenderTargetItem().ShaderResourceTexture,
						(const FTexture2DRHIRef&)ProjectedShadowInfo->RenderTargets.ColorTargets[1]->GetRenderTargetItem().ShaderResourceTexture, 
						(const FTexture2DRHIRef&)ProjectedShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture,
						*ProjectedShadowInfo, 
						LightSceneInfo->Proxy->GetColor() );
				}
			}
		}
	}

	return true;
}

void FSceneRenderer::RenderShadowProjections(
	FRHICommandListImmediate& RHICmdList,
	const FLightSceneProxy* LightSceneProxy,
	const FHairStrandsRenderingData* HairDatas,
	TArrayView<const FProjectedShadowInfo* const> Shadows,
	bool bProjectingForForwardShading,
	bool bMobileModulatedProjections)
{
	FPersistentUniformBuffers& UniformBuffers = Scene->UniformBuffers;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
		SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
		UniformBuffers.UpdateViewUniformBuffer(View);

		const FHairStrandsVisibilityData* HairVisibilityData = nullptr;
		const FHairStrandsMacroGroupDatas* HairMacroGroupData = nullptr;
		if (HairDatas)
		{
			HairVisibilityData = &(HairDatas->HairVisibilityViews.HairDatas[ViewIndex]);
			HairMacroGroupData = &(HairDatas->MacroGroupsPerViews.Views[ViewIndex]);
		}

		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
		LightSceneProxy->SetScissorRect(RHICmdList, View, View.ViewRect);

		// Project the shadow depth buffers onto the scene.
		for (const FProjectedShadowInfo* ProjectedShadowInfo : Shadows)
		{
			if (ProjectedShadowInfo->bAllocated) 
			{
				// Only project the shadow if it's large enough in this particular view (split screen, etc... may have shadows that are large in one view but irrelevantly small in others)
				if (ProjectedShadowInfo->FadeAlphas[ViewIndex] > 1.0f / 256.0f)
				{
					if (ProjectedShadowInfo->bOnePassPointLightShadow)
					{
						ProjectedShadowInfo->RenderOnePassPointLightProjection(RHICmdList, ViewIndex, View, bProjectingForForwardShading, HairVisibilityData, HairMacroGroupData);
					}
					else
					{
						ProjectedShadowInfo->RenderProjection(RHICmdList, ViewIndex, &View, this, bProjectingForForwardShading, bMobileModulatedProjections, HairVisibilityData, HairMacroGroupData);
					}
				}
			}
		}
	}

	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
}

// TODO(RDG): This is a temporary solution while the shadow depth rendering code still isn't ported to RDG.
static void TransitionShadowsToReadable(FRHICommandList& RHICmdList, TArrayView<const FProjectedShadowInfo* const> Shadows)
{
	TSet<IPooledRenderTarget*, DefaultKeyFuncs<IPooledRenderTarget*>, SceneRenderingSetAllocator> FoundTextures;
	FoundTextures.Reserve(Shadows.Num());

	TArray<FRHITransitionInfo, SceneRenderingAllocator> TexturesToTransition;
	TexturesToTransition.Reserve(Shadows.Num());

	for (const FProjectedShadowInfo* ProjectedShadowInfo : Shadows)
	{
		IPooledRenderTarget* DepthTarget = ProjectedShadowInfo->RenderTargets.DepthTarget;

		if (ProjectedShadowInfo->bAllocated && DepthTarget)
		{
			bool bAlreadyInSet = false;
			FoundTextures.Emplace(DepthTarget, &bAlreadyInSet);
			if (!bAlreadyInSet)
			{
				TexturesToTransition.Add(FRHITransitionInfo(DepthTarget->GetShaderResourceRHI(), ERHIAccess::Unknown, ERHIAccess::SRVMask));
			}
		}
	}

	RHICmdList.Transition(TexturesToTransition);
}

BEGIN_SHADER_PARAMETER_STRUCT(FRenderShadowProjectionsParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderShadowProjections(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef ScreenShadowMaskTexture,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture,
	FRDGTextureRef SceneDepthTexture,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsRenderingData* HairDatas,
	bool bProjectingForForwardShading)
{
	const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];
	const FLightSceneProxy* LightSceneProxy = LightSceneInfo->Proxy;

	// Allocate arrays using the graph allocator so we can safely reference them in passes.
	using FProjectedShadowInfoArray = TArray<const FProjectedShadowInfo*, SceneRenderingAllocator>;
	auto& DistanceFieldShadows = *GraphBuilder.AllocObject<FProjectedShadowInfoArray>();
	auto& NormalShadows = *GraphBuilder.AllocObject<FProjectedShadowInfoArray>();

	for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.ShadowsToProject.Num(); ShadowIndex++)
	{
		const FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.ShadowsToProject[ShadowIndex];
		if (ProjectedShadowInfo->bRayTracedDistanceField)
		{
			DistanceFieldShadows.Add(ProjectedShadowInfo);
		}
		else
		{
			NormalShadows.Add(ProjectedShadowInfo);
		}
	}

	if (NormalShadows.Num() > 0)
	{
		AddPass(GraphBuilder, [&NormalShadows](FRHICommandList& RHICmdList)
		{
			TransitionShadowsToReadable(RHICmdList, NormalShadows);
		});

		const auto RenderNormalShadows = [&](FRDGTextureRef OutputTexture, FExclusiveDepthStencil ExclusiveDepthStencil, bool bSubPixel)
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FRenderShadowProjectionsParameters>();
			PassParameters->SceneTextures = SceneTexturesUniformBuffer;
			PassParameters->HairCategorizationTexture = bSubPixel && HairDatas->HairVisibilityViews.HairDatas.Num() > 0 ? HairDatas->HairVisibilityViews.HairDatas[0].CategorizationTexture : nullptr;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);
			PassParameters->RenderTargets.DepthStencil = 
				bSubPixel && HairDatas->HairVisibilityViews.HairDatas.Num() > 0 ?
				FDepthStencilBinding(HairDatas->HairVisibilityViews.HairDatas[0].HairOnlyDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, ExclusiveDepthStencil) :
				FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, ExclusiveDepthStencil);

			FString LightNameWithLevel;
			GetLightNameForDrawEvent(LightSceneProxy, LightNameWithLevel);

			// All shadows projections are rendered in one RDG pass for efficiency purposes. Technically, RDG is able to merge all these
			// render passes together if we used a separate one per shadow, but we are paying a cost for it which just seems unnecessary
			// here. We are also able to bulk-transition all of the shadows in one go, which RDG is currently not able to do.
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("%s", *LightNameWithLevel),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &NormalShadows, LightSceneProxy, HairDatas, bProjectingForForwardShading, bSubPixel](FRHICommandListImmediate& RHICmdList)
			{
				const bool bMobileModulatedProjections = false;
				FSceneRenderer::RenderShadowProjections(RHICmdList, LightSceneProxy, bSubPixel ? HairDatas : nullptr, NormalShadows, bProjectingForForwardShading, bMobileModulatedProjections);
			});
		};

		{
			RDG_EVENT_SCOPE(GraphBuilder, "Shadows");
			RenderNormalShadows(ScreenShadowMaskTexture, FExclusiveDepthStencil::DepthRead_StencilWrite, false);
		}

		if (ScreenShadowMaskSubPixelTexture && HairDatas && HairDatas->HairVisibilityViews.HairDatas.Num() > 0 && HairDatas->HairVisibilityViews.HairDatas[0].CategorizationTexture)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "SubPixelShadows");

			// Sub-pixel shadows don't use stencil.
			RenderNormalShadows(ScreenShadowMaskSubPixelTexture, FExclusiveDepthStencil::DepthRead_StencilWrite, true);
		}
	}

	if (DistanceFieldShadows.Num() > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "DistanceFieldShadows");

		// Distance field shadows need to be renderer last as they blend over far shadow cascades.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			FIntRect ScissorRect;
			if (!LightSceneProxy->GetScissorRect(ScissorRect, View, View.ViewRect))
			{
				ScissorRect = View.ViewRect;
			}

			if (ScissorRect.Area() > 0)
			{
				for (int32 ShadowIndex = 0; ShadowIndex < DistanceFieldShadows.Num(); ShadowIndex++)
				{
					const FProjectedShadowInfo* ProjectedShadowInfo = DistanceFieldShadows[ShadowIndex];
					ProjectedShadowInfo->RenderRayTracedDistanceFieldProjection(
						GraphBuilder,
						SceneTexturesUniformBuffer,
						ScreenShadowMaskTexture,
						SceneDepthTexture,
						View,
						ScissorRect,
						bProjectingForForwardShading);
				}
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderDeferredShadowProjections(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FLightSceneInfo* LightSceneInfo,
	FRDGTextureRef ScreenShadowMaskTexture,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture,
	FRDGTextureRef SceneDepthTexture,
	const FHairStrandsRenderingData* HairDatas,
	bool& bInjectedTranslucentVolume)
{
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderShadowProjections, FColor::Emerald);
	SCOPE_CYCLE_COUNTER(STAT_ProjectedShadowDrawTime);
	RDG_EVENT_SCOPE(GraphBuilder, "ShadowProjectionOnOpaque");
	RDG_GPU_STAT_SCOPE(GraphBuilder, ShadowProjection);

	const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];
	
	const bool bProjectingForForwardShading = false;
	RenderShadowProjections(GraphBuilder, SceneTexturesUniformBuffer, ScreenShadowMaskTexture, ScreenShadowMaskSubPixelTexture, SceneDepthTexture, LightSceneInfo, HairDatas, bProjectingForForwardShading);

	for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.ShadowsToProject.Num(); ShadowIndex++)
	{
		const FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.ShadowsToProject[ShadowIndex];

		if (ProjectedShadowInfo->bAllocated
			&& ProjectedShadowInfo->bWholeSceneShadow
			// Not supported on translucency yet
			&& !ProjectedShadowInfo->bRayTracedDistanceField
			// Don't inject shadowed lighting with whole scene shadows used for previewing a light with static shadows,
			// Since that would cause a mismatch with the built lighting
			// However, stationary directional lights allow whole scene shadows that blend with precomputed shadowing
			&& (!LightSceneInfo->Proxy->HasStaticShadowing() || ProjectedShadowInfo->IsWholeSceneDirectionalShadow()))
		{
			bInjectedTranslucentVolume = true;
			RDG_EVENT_SCOPE(GraphBuilder, "InjectTranslucentVolume");

			// Inject the shadowed light into the translucency lighting volumes
			if (ProjectedShadowInfo->DependentView != nullptr)
			{
				int32 ViewIndex = -1;
				for (int32 i = 0; i < Views.Num(); ++i)
				{
					if (ProjectedShadowInfo->DependentView == &Views[i])
					{
						ViewIndex = i;
						break;
					}
				}

				RDG_GPU_MASK_SCOPE(GraphBuilder, ProjectedShadowInfo->DependentView->GPUMask);
				InjectTranslucentVolumeLighting(GraphBuilder, *LightSceneInfo, ProjectedShadowInfo, *ProjectedShadowInfo->DependentView, ViewIndex);
			}
			else
			{
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					FViewInfo& View = Views[ViewIndex];
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					InjectTranslucentVolumeLighting(GraphBuilder, *LightSceneInfo, ProjectedShadowInfo, View, ViewIndex);
				}
			}
		}
	}

	RenderCapsuleDirectShadows(GraphBuilder, SceneTexturesUniformBuffer, *LightSceneInfo, ScreenShadowMaskTexture, VisibleLightInfo.CapsuleShadowsToProject, bProjectingForForwardShading);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.ShadowsToProject.Num(); ShadowIndex++)
		{
			const FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.ShadowsToProject[ShadowIndex];

			if (ProjectedShadowInfo->bAllocated && ProjectedShadowInfo->bWholeSceneShadow)
			{
				View.HeightfieldLightingViewInfo.ComputeShadowMapShadowing(GraphBuilder, View, ProjectedShadowInfo);
			}
		}
	}

	// Inject deep shadow mask
	if (HairDatas)
	{
		RenderHairStrandsShadowMask(GraphBuilder, Views, LightSceneInfo, HairDatas, ScreenShadowMaskTexture);
	}
}

void FMobileSceneRenderer::RenderModulatedShadowProjections(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	if (IsSimpleForwardShadingEnabled(ShaderPlatform) || !ViewFamily.EngineShowFlags.DynamicShadows)
	{
		return;
	}

	SCOPED_NAMED_EVENT(FMobileSceneRenderer_RenderModulatedShadowProjections, FColor::Emerald);
	SCOPE_CYCLE_COUNTER(STAT_ProjectedShadowDrawTime);
	SCOPED_DRAW_EVENT(RHICmdList, ShadowProjectionOnOpaque);
	SCOPED_GPU_STAT(RHICmdList, ShadowProjection);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	TRefCountPtr<FRHIUniformBuffer> SceneTexturesUniformBuffer = CreateMobileSceneTextureUniformBuffer(RHICmdList, EMobileSceneTextureSetupMode::SceneColor);
	SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, SceneTexturesUniformBuffer);

	bool bMobileMSAA = NumMSAASamples > 1 && SceneContext.GetSceneColorSurface()->GetNumSamples() > 1;

	// render shadowmaps for relevant lights.
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;
		const FLightSceneProxy* LightSceneProxy = LightSceneInfo->Proxy;

		if(LightSceneInfo->ShouldRenderLightViewIndependent() && LightSceneProxy && LightSceneProxy->CastsModulatedShadows())
		{
			const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];
			if (VisibleLightInfo.ShadowsToProject.Num() > 0)
			{
				FRHITexture* ScreenShadowMaskTexture = nullptr;
				// Shadow projections collection phase
				{
					const FIntPoint SceneTextureExtent = SceneContext.GetBufferSizeXY();
					FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SceneTextureExtent, PF_B8G8R8A8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
					Desc.NumSamples = FSceneRenderTargets::GetNumSceneColorMSAASamples(FeatureLevel);
					GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SceneContext.MobileScreenShadowMask, TEXT("MobileScreenShadowMask"), ERenderTargetTransience::NonTransient);
					ScreenShadowMaskTexture = SceneContext.MobileScreenShadowMask->GetRenderTargetItem().TargetableTexture;

					FRHIRenderPassInfo ShadowProjectionsCollectionRenderPassInfo(
						ScreenShadowMaskTexture,
						ERenderTargetActions::Clear_Store,
						nullptr,
						SceneContext.GetSceneDepthSurface(),
						bKeepDepthContent && !bMobileMSAA ? EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil : EDepthStencilTargetActions::LoadDepthStencil_DontStoreDepthStencil,
						nullptr,
						FExclusiveDepthStencil::DepthRead_StencilWrite
					);

					ShadowProjectionsCollectionRenderPassInfo.SubpassHint = ESubpassHint::DepthReadSubpass;
					RHICmdList.BeginRenderPass(ShadowProjectionsCollectionRenderPassInfo, TEXT("ShadowProjectionsCollection"));
				}

				TransitionShadowsToReadable(RHICmdList, VisibleLightInfo.ShadowsToProject);

				const bool bProjectingForForwardShading = false;
				const bool bMobileModulatedProjections = true;
				RenderShadowProjections(RHICmdList, LightSceneProxy, nullptr, VisibleLightInfo.ShadowsToProject, bProjectingForForwardShading, bMobileModulatedProjections);

				// Screen space modulated shadow sample phase
				{
					RHICmdList.EndRenderPass();

					FRHITexture* SceneColor = SceneContext.GetSceneColorSurface();
					FRHITexture* SceneColorResolve = nullptr;

					if (bMobileMSAA)
					{
						SceneColorResolve = SceneContext.GetSceneColorTexture();
						RHICmdList.Transition(FRHITransitionInfo(SceneColorResolve, ERHIAccess::Unknown, ERHIAccess::RTV | ERHIAccess::ResolveDst));
					}

					FRHIRenderPassInfo ScreenSpaceModulatedShadowRenderPassInfo(
						SceneColor,
						SceneColorResolve ? ERenderTargetActions::Load_Resolve : ERenderTargetActions::Load_Store,
						SceneColorResolve,
						SceneContext.GetSceneDepthSurface(),
						bKeepDepthContent && !bMobileMSAA ? EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil : EDepthStencilTargetActions::LoadDepthStencil_DontStoreDepthStencil,
						nullptr,
						FExclusiveDepthStencil::DepthRead_StencilWrite
					);
					RHICmdList.BeginRenderPass(ScreenSpaceModulatedShadowRenderPassInfo, TEXT("ScreenSpaceModulatedShadow"));

					RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

					// Get shaders.
					FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
					TShaderMapRef<TScreenSpaceModulatedShadowVS> VertexShader(GlobalShaderMap);
					TShaderMapRef<TScreenSpaceModulatedShadowPS> PixelShader(GlobalShaderMap);

					// Set the graphic pipeline state.
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_Zero, BF_SourceColor, BO_Add, BF_Zero, BF_One>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector2();
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					VertexShader->SetParameters(RHICmdList, View);
					PixelShader->SetParameters(RHICmdList, View, ScreenShadowMaskTexture, LightSceneProxy->GetModulatedShadowColor());

					// Draw screen quad.
					RHICmdList.SetStreamSource(0, GScreenSpaceVertexBuffer.VertexBufferRHI, 0);
					RHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);

					RHICmdList.EndRenderPass();
				}
			}
		}
	}
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FTranslucentSelfShadowUniformParameters, "TranslucentSelfShadow");

void SetupTranslucentSelfShadowUniformParameters(const FProjectedShadowInfo* ShadowInfo, FTranslucentSelfShadowUniformParameters& OutParameters)
{
	if (ShadowInfo)
	{
		FVector4 ShadowmapMinMax;
		FMatrix WorldToShadowMatrixValue = ShadowInfo->GetWorldToShadowMatrix(ShadowmapMinMax);

		OutParameters.WorldToShadowMatrix = WorldToShadowMatrixValue;
		OutParameters.ShadowUVMinMax = ShadowmapMinMax;

		const FLightSceneProxy* const LightProxy = ShadowInfo->GetLightSceneInfo().Proxy;
		OutParameters.DirectionalLightDirection = LightProxy->GetDirection();

		//@todo - support fading from both views
		const float FadeAlpha = ShadowInfo->FadeAlphas[0];
		// Incorporate the diffuse scale of 1 / PI into the light color
		OutParameters.DirectionalLightColor = FVector4(FVector(LightProxy->GetColor() * FadeAlpha / PI), FadeAlpha);

		OutParameters.Transmission0 = ShadowInfo->RenderTargets.ColorTargets[0]->GetRenderTargetItem().ShaderResourceTexture.GetReference();
		OutParameters.Transmission1 = ShadowInfo->RenderTargets.ColorTargets[1]->GetRenderTargetItem().ShaderResourceTexture.GetReference();
		OutParameters.Transmission0Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		OutParameters.Transmission1Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
	else
	{
		OutParameters.Transmission0 = GBlackTexture->TextureRHI;
		OutParameters.Transmission1 = GBlackTexture->TextureRHI;
		OutParameters.Transmission0Sampler = GBlackTexture->SamplerStateRHI;
		OutParameters.Transmission1Sampler = GBlackTexture->SamplerStateRHI;
		
		OutParameters.DirectionalLightColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

void FEmptyTranslucentSelfShadowUniformBuffer::InitDynamicRHI()
{
	FTranslucentSelfShadowUniformParameters Parameters;
	SetupTranslucentSelfShadowUniformParameters(nullptr, Parameters);
	SetContentsNoUpdate(Parameters);

	Super::InitDynamicRHI();
}

/** */
TGlobalResource< FEmptyTranslucentSelfShadowUniformBuffer > GEmptyTranslucentSelfShadowUniformBuffer;
