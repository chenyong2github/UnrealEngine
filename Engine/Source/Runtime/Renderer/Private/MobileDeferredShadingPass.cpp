// Copyright Epic Games, Inc. All Rights Reserved.

#include "MobileDeferredShadingPass.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"

int32 GMobileUseClusteredDeferredShading = 0;
static FAutoConsoleVariableRef CVarMobileUseClusteredDeferredShading(
	TEXT("r.Mobile.UseClusteredDeferredShading"),
	GMobileUseClusteredDeferredShading,
	TEXT("Toggle use of clustered deferred shading for lights that support it. 0 is off (default), 1 is on"),
	ECVF_RenderThreadSafe
);

int32 GMobileUseLightStencilCulling = 1;
static FAutoConsoleVariableRef CVarMobileUseLightStencilCulling(
	TEXT("r.Mobile.UseLightStencilCulling"),
	GMobileUseLightStencilCulling,
	TEXT("Whether to use stencil to cull local lights. 0 is off, 1 is on (default)"),
	ECVF_RenderThreadSafe
);

BEGIN_SHADER_PARAMETER_STRUCT(FMobileDeferredPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileSceneTextureUniformParameters, MobileSceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FMobileDeferredShadingPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMobileDeferredShadingPS, Global);
	SHADER_USE_PARAMETER_STRUCT(FMobileDeferredShadingPS, FGlobalShader)

	class FUseClustred			: SHADER_PERMUTATION_BOOL("USE_CLUSTERED");
	class FApplySkyReflection	: SHADER_PERMUTATION_BOOL("APPLY_SKY_REFLECTION");
	class FApplyCSM				: SHADER_PERMUTATION_BOOL("APPLY_CSM");
	class FApplyReflection		: SHADER_PERMUTATION_BOOL("APPLY_REFLECTION");
	class FShadowQuality		: SHADER_PERMUTATION_INT("MOBILE_SHADOW_QUALITY", 4);
	using FPermutationDomain = TShaderPermutationDomain< FUseClustred, FApplySkyReflection, FApplyCSM, FApplyReflection, FShadowQuality>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, Forward)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FMobileDirectionalLightShaderParameters, MobileDirectionalLight)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
	END_SHADER_PARAMETER_STRUCT()

	//SHADER_PARAMETER_STRUCT_REF(FMobileSceneTextureUniformParameters, SceneTextures)

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());
		OutEnvironment.SetDefine(TEXT("SUPPORTS_TEXTURECUBE_ARRAY"), 1);
	}

	static FPermutationDomain RemapPermutationVector(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FApplyCSM>() == false)
		{
			PermutationVector.Set<FShadowQuality>(0);
		}
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsMobilePlatform(Parameters.Platform))
		{
			return false;
		}
		
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		// Compile out the shader if this permutation gets remapped.
		if (RemapPermutationVector(PermutationVector) != PermutationVector)
		{
			return false;
		}
		
		return true;
	}

	static FPermutationDomain BuildPermutationVector(const FViewInfo& View)
	{
		bool bApplySky = View.Family->EngineShowFlags.SkyLighting;
		int32 ShadowQuality = (int32)GetShadowQuality();
		int32 NumReflectionCaptures = View.NumBoxReflectionCaptures + View.NumSphereReflectionCaptures;

		FPermutationDomain PermutationVector;
		PermutationVector.Set<FMobileDeferredShadingPS::FUseClustred>(GMobileUseClusteredDeferredShading != 0);
		PermutationVector.Set<FMobileDeferredShadingPS::FApplySkyReflection>(bApplySky);
		PermutationVector.Set<FMobileDeferredShadingPS::FApplyCSM>(ShadowQuality > 0);
		PermutationVector.Set<FMobileDeferredShadingPS::FApplyReflection>(NumReflectionCaptures > 0);
		PermutationVector.Set<FMobileDeferredShadingPS::FShadowQuality>(FMath::Clamp(ShadowQuality - 1, 0, 3));
		return PermutationVector;
	}
};

IMPLEMENT_SHADER_TYPE(, FMobileDeferredShadingPS, TEXT("/Engine/Private/MobileDeferredShading.usf"), TEXT("MobileDeferredShadingPS"), SF_Pixel);

class FMobileRadialLightPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMobileRadialLightPS, Global);
	SHADER_USE_PARAMETER_STRUCT(FMobileRadialLightPS, FGlobalShader)

	class FSpotLightDim			: SHADER_PERMUTATION_BOOL("IS_SPOT_LIGHT");
	class FInverseSquaredDim	: SHADER_PERMUTATION_BOOL("INVERSE_SQUARED_FALLOFF");
	class FIESProfileDim		: SHADER_PERMUTATION_BOOL("USE_IES_PROFILE");
	using FPermutationDomain = TShaderPermutationDomain<FSpotLightDim, FInverseSquaredDim, FIESProfileDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_TEXTURE(Texture2D, IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, IESTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform);
	}
};

IMPLEMENT_SHADER_TYPE(, FMobileRadialLightPS, TEXT("/Engine/Private/MobileDeferredShading.usf"), TEXT("MobileRadialLightPS"), SF_Pixel);

static void RenderDirectLight(FRHICommandListImmediate& RHICmdList, const FScene& Scene, const FViewInfo& View)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
	
	FMobileDeferredShadingPS::FParameters PassParameters;
	PassParameters.View = View.ViewUniformBuffer;
	PassParameters.Forward = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
	//PassParameters.SceneTextures = SceneTexturesBuffer;
	PassParameters.MobileDirectionalLight = Scene.UniformBuffers.MobileDirectionalLightUniformBuffers[1];

	PassParameters.ReflectionCaptureData = GetShaderBinding(View.ReflectionCaptureUniformBuffer);
	FReflectionUniformParameters ReflectionUniformParameters;
	SetupReflectionUniformParameters(View, ReflectionUniformParameters);
	PassParameters.ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	// Add to emissive in SceneColor
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	// Shade only MSM_DefaultLit pixels
	uint8 StencilRef = GET_STENCIL_MOBILE_SM_MASK(MSM_DefaultLit);
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
										false, CF_Always,
										true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,		
										false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
										GET_STENCIL_MOBILE_SM_MASK(0x7), 0x00>::GetRHI(); // 4 bits for shading models
	
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	FMobileDeferredShadingPS::FPermutationDomain PermutationVector = FMobileDeferredShadingPS::BuildPermutationVector(View);
	TShaderMapRef<FMobileDeferredShadingPS> PixelShader(View.ShaderMap, PermutationVector);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters);
	RHICmdList.SetStencilRef(StencilRef);
			
	const FIntPoint TargetSize = SceneContext.GetBufferSizeXY();
	
	DrawRectangle(
		RHICmdList, 
		0, 0, 
		View.ViewRect.Width(), View.ViewRect.Height(), 
		View.ViewRect.Min.X, View.ViewRect.Min.Y, 
		View.ViewRect.Width(), View.ViewRect.Height(),
		FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()), 
		TargetSize, 
		VertexShader);
}

static void SetLocalLightRasterizerAndDepthState(FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FViewInfo& View, const FSphere& LightBounds)
{
	if (GMobileUseLightStencilCulling != 0)
	{
		// Render backfaces with depth and stencil tests
		// and clear stencil to zero for next light mask
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
			false, CF_LessEqual,
			false, CF_Equal, SO_Keep, SO_Keep, SO_Keep,		
			true, CF_Equal, SO_Zero, SO_Keep, SO_Zero,
			GET_STENCIL_MOBILE_SM_MASK(0x7) | STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK>::GetRHI();
		return;
	}
		
	const bool bCameraInsideLightGeometry = ((FVector)View.ViewMatrices.GetViewOrigin() - LightBounds.Center).SizeSquared() < FMath::Square(LightBounds.W * 1.05f + View.NearClippingDistance * 2.0f)
		// Always draw backfaces in ortho
		//@todo - accurate ortho camera / light intersection
		|| !View.IsPerspectiveProjection();

	if (bCameraInsideLightGeometry)
	{
		// Render backfaces with depth tests disabled since the camera is inside (or close to inside) the light geometry
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,		
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			GET_STENCIL_MOBILE_SM_MASK(0x7), 0x00>::GetRHI();
	}
	else
	{
		// Render frontfaces with depth tests on to get the speedup from HiZ since the camera is outside the light geometry
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,		
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			GET_STENCIL_MOBILE_SM_MASK(0x7), 0x00>::GetRHI();
	}
}

static void RenderLocalLightStencilMask(FRHICommandListImmediate& RHICmdList, const FScene& Scene, const FViewInfo& View, const FLightSceneInfo& LightSceneInfo)
{
	const uint8 LightType = LightSceneInfo.Proxy->GetLightType();

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
	GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
	// set stencil to 1 where depth test fails
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
		false, CF_DepthNearOrEqual,
		true, CF_Always, SO_Keep, SO_Replace, SO_Keep,		
		false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
		0x00, STENCIL_SANDBOX_MASK>::GetRHI();
	   	
	TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = nullptr;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	VertexShader->SetParameters(RHICmdList, View, &LightSceneInfo);
	RHICmdList.SetStencilRef(1);

	if (LightType == LightType_Point)
	{
		StencilingGeometry::DrawSphere(RHICmdList);
	}
	else // LightType_Spot
	{
		StencilingGeometry::DrawCone(RHICmdList);
	}
}

static void RenderLocalLight(FRHICommandListImmediate& RHICmdList, const FScene& Scene, const FViewInfo& View, const FLightSceneInfo& LightSceneInfo)
{
	if (!LightSceneInfo.ShouldRenderLight(View))
	{
		return;
	}

	const uint8 LightType = LightSceneInfo.Proxy->GetLightType();
	if (LightType != LightType_Point && LightType != LightType_Spot)
	{
		return;
	}
	
	if (GMobileUseLightStencilCulling != 0)
	{
		RenderLocalLightStencilMask(RHICmdList, Scene, View, LightSceneInfo);
	}

	bool bUseIESTexture = false;
	FTexture* IESTextureResource = GWhiteTexture;
	if (View.Family->EngineShowFlags.TexturedLightProfiles && LightSceneInfo.Proxy->GetIESTextureResource())
	{
		IESTextureResource = LightSceneInfo.Proxy->GetIESTextureResource();
		bUseIESTexture = true;
	}
			
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	const FSphere LightBounds = LightSceneInfo.Proxy->GetBoundingSphere();
	SetLocalLightRasterizerAndDepthState(GraphicsPSOInit, View, LightBounds);
	TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);
	FMobileRadialLightPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FMobileRadialLightPS::FSpotLightDim>(LightType == LightType_Spot);
	PermutationVector.Set<FMobileRadialLightPS::FInverseSquaredDim>(LightSceneInfo.Proxy->IsInverseSquared());
	PermutationVector.Set<FMobileRadialLightPS::FIESProfileDim>(bUseIESTexture);
	TShaderMapRef<FMobileRadialLightPS> PixelShader(View.ShaderMap, PermutationVector);
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	FMobileRadialLightPS::FParameters PassParameters;
	PassParameters.View = View.ViewUniformBuffer;
	PassParameters.DeferredLightUniforms = TUniformBufferRef<FDeferredLightUniformStruct>::CreateUniformBufferImmediate(GetDeferredLightParameters(View, LightSceneInfo), EUniformBufferUsage::UniformBuffer_SingleFrame);
	PassParameters.IESTexture = IESTextureResource->TextureRHI;
	PassParameters.IESTextureSampler = IESTextureResource->SamplerStateRHI;
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters);
	VertexShader->SetParameters(RHICmdList, View, &LightSceneInfo);

	// Shade only MSM_DefaultLit pixels
	uint8 StencilRef = GET_STENCIL_MOBILE_SM_MASK(MSM_DefaultLit);
	RHICmdList.SetStencilRef(StencilRef);

	if (LightType == LightType_Point)
	{
		StencilingGeometry::DrawSphere(RHICmdList);
	}
	else // LightType_Spot
	{
		StencilingGeometry::DrawCone(RHICmdList);
	}
}

void MobileDeferredShadingPass(FRDGBuilder& GraphBuilder, FRenderTargetBindingSlots& BasePassRenderTargets, TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> MobileSceneTextures, const FScene& Scene, const FViewInfo& View, const FSortedLightSetSceneInfo &SortedLightSet)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FMobileDeferredPassParameters>();
	PassParameters->RenderTargets = BasePassRenderTargets;
	PassParameters->MobileSceneTextures = MobileSceneTextures;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MobileDeferredShadingPass"), PassParameters, ERDGPassFlags::Raster,
		[&Scene, &View, &SortedLightSet](FRHICommandListImmediate& RHICmdList)
	{

		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		RenderDirectLight(RHICmdList, Scene, View);

		// Render non-clustered local lights
		int32 StandardDeferredStart = SortedLightSet.SimpleLightsEnd;
		int32 AttenuationLightStart = SortedLightSet.AttenuationLightStart;
		if (GMobileUseClusteredDeferredShading != 0)
		{
			StandardDeferredStart = SortedLightSet.ClusteredSupportedEnd;
		}

		for (int32 LightIdx = StandardDeferredStart; LightIdx < AttenuationLightStart; ++LightIdx)
		{
			const FSortedLightSceneInfo& SortedLight = SortedLightSet.SortedLights[LightIdx];
			const FLightSceneInfo& LightSceneInfo = *SortedLight.LightSceneInfo;
			RenderLocalLight(RHICmdList, Scene, View, LightSceneInfo);
		}
	});
}