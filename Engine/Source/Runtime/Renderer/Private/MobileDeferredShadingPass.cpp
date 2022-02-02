// Copyright Epic Games, Inc. All Rights Reserved.

#include "MobileDeferredShadingPass.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"
#include "PlanarReflectionRendering.h"

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

class FMobileDirectLightFunctionPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FMobileDirectLightFunctionPS, Material);
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FMobileDirectLightFunctionPS, FMaterialShader)

	class FUseClustred			: SHADER_PERMUTATION_BOOL("USE_CLUSTERED");
	class FApplySkyReflection	: SHADER_PERMUTATION_BOOL("APPLY_SKY_REFLECTION");
	class FApplyCSM				: SHADER_PERMUTATION_BOOL("APPLY_CSM");
	class FApplyReflection		: SHADER_PERMUTATION_BOOL("APPLY_REFLECTION");
	class FShadowQuality		: SHADER_PERMUTATION_INT("MOBILE_SHADOW_QUALITY", 4);
	using FPermutationDomain = TShaderPermutationDomain< FUseClustred, FApplySkyReflection, FApplyCSM, FApplyReflection, FShadowQuality>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FMobileDirectionalLightShaderParameters, MobileDirectionalLight)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
		SHADER_PARAMETER(FMatrix44f, TranslatedWorldToLight)
		SHADER_PARAMETER(FVector4f, LightFunctionParameters)
		SHADER_PARAMETER(FVector3f, LightFunctionParameters2)
		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, ScreenSpaceShadowMaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceShadowMaskSampler)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());
		OutEnvironment.SetDefine(TEXT("SUPPORTS_TEXTURECUBE_ARRAY"), 1);
		OutEnvironment.SetDefine(TEXT("USE_LIGHT_FUNCTION"), Parameters.MaterialParameters.bIsDefaultMaterial ? 0 : 1);
		OutEnvironment.SetDefine(TEXT("ENABLE_DISTANCE_FIELD"), IsMobileDistanceFieldEnabled(Parameters.Platform));
	}

	static FPermutationDomain RemapPermutationVector(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FApplyCSM>() == false)
		{
			PermutationVector.Set<FShadowQuality>(0);
		}
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		if (Parameters.MaterialParameters.MaterialDomain != MD_LightFunction || 
			!IsMobilePlatform(Parameters.Platform) || 
			!IsMobileDeferredShadingEnabled(Parameters.Platform))
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

	static FPermutationDomain BuildPermutationVector(const FViewInfo& View, bool bDirectionalLight)
	{
		bool bUseClustered = bDirectionalLight && GMobileUseClusteredDeferredShading != 0;
		bool bApplySky = View.Family->EngineShowFlags.SkyLighting;
		int32 ShadowQuality = bDirectionalLight ? (int32)GetShadowQuality() : 0;
		int32 NumReflectionCaptures = View.NumBoxReflectionCaptures + View.NumSphereReflectionCaptures;
		
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FMobileDirectLightFunctionPS::FUseClustred>(bUseClustered);
		PermutationVector.Set<FMobileDirectLightFunctionPS::FApplySkyReflection>(bApplySky);
		PermutationVector.Set<FMobileDirectLightFunctionPS::FApplyCSM>(ShadowQuality > 0);
		PermutationVector.Set<FMobileDirectLightFunctionPS::FApplyReflection>(NumReflectionCaptures > 0);
		PermutationVector.Set<FMobileDirectLightFunctionPS::FShadowQuality>(FMath::Clamp(ShadowQuality - 1, 0, 3));
		return PermutationVector;
	}

	static void SetParameters(FRHICommandList& RHICmdList, const TShaderRef<FMobileDirectLightFunctionPS>& Shader, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FMaterial& Material, const FParameters& Parameters)
	{
		FMaterialShader* MaterialShader = Shader.GetShader();
		FRHIPixelShader* ShaderRHI = Shader.GetPixelShader();
		MaterialShader->SetParameters(RHICmdList, ShaderRHI, Proxy, Material, View);
		SetShaderParameters(RHICmdList, Shader, ShaderRHI, Parameters);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FMobileDirectLightFunctionPS, TEXT("/Engine/Private/MobileDeferredShading.usf"), TEXT("MobileDirectLightPS"), SF_Pixel);

/**
 * A pixel shader for projecting a light function onto the scene.
 */
class FMobileRadialLightFunctionPS : public FMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FMobileRadialLightFunctionPS,Material);
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FMobileRadialLightFunctionPS, FMaterialShader)

	class FSpotLightDim			: SHADER_PERMUTATION_BOOL("IS_SPOT_LIGHT");
	class FIESProfileDim		: SHADER_PERMUTATION_BOOL("USE_IES_PROFILE");
	using FPermutationDomain = TShaderPermutationDomain<FSpotLightDim, FIESProfileDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, TranslatedWorldToLight)
		SHADER_PARAMETER(FVector4f, LightFunctionParameters)
		SHADER_PARAMETER(FVector3f, LightFunctionParameters2)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_TEXTURE(Texture2D, IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, IESTextureSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		if (Parameters.MaterialParameters.MaterialDomain != MD_LightFunction || 
			!IsMobilePlatform(Parameters.Platform) || 
			!IsMobileDeferredShadingEnabled(Parameters.Platform))
		{
			return false;
		}
		return true;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_LIGHT_FUNCTION"), Parameters.MaterialParameters.bIsDefaultMaterial ? 0 : 1);
	}

	static void SetParameters(FRHICommandList& RHICmdList, const TShaderRef<FMobileRadialLightFunctionPS>& Shader, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FMaterial& Material, const FParameters& Parameters)
	{
		FMaterialShader* MaterialShader = Shader.GetShader();
		FRHIPixelShader* ShaderRHI = Shader.GetPixelShader();
		MaterialShader->SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
		MaterialShader->SetParameters(RHICmdList, ShaderRHI, Proxy, Material, View);
		SetShaderParameters(RHICmdList, Shader, ShaderRHI, Parameters);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FMobileRadialLightFunctionPS, TEXT("/Engine/Private/MobileDeferredShading.usf"), TEXT("MobileRadialLightPS"), SF_Pixel);

struct FCachedLightMaterial
{
	const FMaterial* Material;
	const FMaterialRenderProxy* MaterialProxy;
};

template<class ShaderType>
static void GetLightMaterial(const FCachedLightMaterial& DefaultLightMaterial, const FMaterialRenderProxy* MaterialProxy, int32 PermutationId, FCachedLightMaterial& OutLightMaterial, TShaderRef<ShaderType>& OutShader)
{
	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<ShaderType>(PermutationId);
	FMaterialShaders Shaders;

	if (MaterialProxy)
	{
		const FMaterial* Material = MaterialProxy->GetMaterialNoFallback(ERHIFeatureLevel::ES3_1);
		if (Material && Material->IsLightFunction())
		{
			OutLightMaterial.Material = Material;
			OutLightMaterial.MaterialProxy = MaterialProxy;
			if (Material->TryGetShaders(ShaderTypes, nullptr, Shaders))
			{
				Shaders.TryGetPixelShader(OutShader);
				return;
			}
		}
	}

	// use default material
	OutLightMaterial.Material = DefaultLightMaterial.Material;
	OutLightMaterial.MaterialProxy = DefaultLightMaterial.MaterialProxy;
	const FMaterialShaderMap* MaterialShaderMap = OutLightMaterial.Material->GetRenderingThreadShaderMap();
	OutShader = MaterialShaderMap->GetShader<ShaderType>(PermutationId);
}

static void RenderDirectLight(FRHICommandListImmediate& RHICmdList, const FScene& Scene, const FViewInfo& View, const FCachedLightMaterial& DefaultLightMaterial)
{
	FLightSceneInfo* DirectionalLight = nullptr;
	for (int32 ChannelIdx = 0; ChannelIdx < UE_ARRAY_COUNT(Scene.MobileDirectionalLights) && !DirectionalLight; ChannelIdx++)
	{
		DirectionalLight = Scene.MobileDirectionalLights[ChannelIdx];
	}

	FString LightNameWithLevel = TEXT("DirectionalLight");
	if (DirectionalLight)
	{
		FSceneRenderer::GetLightNameForDrawEvent(DirectionalLight->Proxy, LightNameWithLevel);
	}
	SCOPED_DRAW_EVENTF(RHICmdList, DirectionalLight, TEXT("%s"), *LightNameWithLevel);

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
	
	const FMaterialRenderProxy* LightFunctionMaterialProxy = nullptr;
	if (View.Family->EngineShowFlags.LightFunctions && DirectionalLight)
	{
		LightFunctionMaterialProxy = DirectionalLight->Proxy->GetLightFunctionMaterial();
	}
	FMobileDirectLightFunctionPS::FPermutationDomain PermutationVector = FMobileDirectLightFunctionPS::BuildPermutationVector(View, DirectionalLight != nullptr);
	FCachedLightMaterial LightMaterial;
	TShaderRef<FMobileDirectLightFunctionPS> PixelShader;
	GetLightMaterial(DefaultLightMaterial, LightFunctionMaterialProxy, PermutationVector.ToDimensionValueId(), LightMaterial, PixelShader);
	
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, StencilRef);

	FMobileDirectLightFunctionPS::FParameters PassParameters;
	PassParameters.MobileDirectionalLight = Scene.UniformBuffers.MobileDirectionalLightUniformBuffers[1];
	PassParameters.ReflectionCaptureData = GetShaderBinding(View.ReflectionCaptureUniformBuffer);
	FReflectionUniformParameters ReflectionUniformParameters;
	SetupReflectionUniformParameters(View, ReflectionUniformParameters);
	PassParameters.ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
	PassParameters.LightFunctionParameters = FVector4f(1.0f, 1.0f, 0.0f, 0.0f);
	PassParameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	PassParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (IsMobileDistanceFieldEnabled(View.GetShaderPlatform()) && GScreenSpaceShadowMaskTextureMobileOutputs.ScreenSpaceShadowMaskTextureMobile.IsValid())
	{
		PassParameters.ScreenSpaceShadowMaskTexture = GScreenSpaceShadowMaskTextureMobileOutputs.ScreenSpaceShadowMaskTextureMobile->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters.ScreenSpaceShadowMaskSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
	else
	{
		PassParameters.ScreenSpaceShadowMaskTexture = GSystemTextures.WhiteDummy->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters.ScreenSpaceShadowMaskSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
	
	if (DirectionalLight)
	{
		const bool bUseMovableLight = DirectionalLight && !DirectionalLight->Proxy->HasStaticShadowing();
		PassParameters.LightFunctionParameters2 = FVector3f(DirectionalLight->Proxy->GetLightFunctionFadeDistance(), DirectionalLight->Proxy->GetLightFunctionDisabledBrightness(), bUseMovableLight ? 1.0f : 0.0f);
		const FVector Scale = DirectionalLight->Proxy->GetLightFunctionScale();
		// Switch x and z so that z of the user specified scale affects the distance along the light direction
		const FVector InverseScale = FVector(1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X);
		const FMatrix WorldToLight = DirectionalLight->Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));
		PassParameters.TranslatedWorldToLight = FMatrix44f(FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation()) * WorldToLight);
	}
	FMobileDirectLightFunctionPS::SetParameters(RHICmdList, PixelShader, View, LightMaterial.MaterialProxy, *LightMaterial.Material, PassParameters);
	
	const FIntPoint TargetSize = GetSceneTextureExtent();
	
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

static void SetLocalLightRasterizerAndDepthState_StencilMask(FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FViewInfo& View)
{
	// Render backfaces with depth and stencil tests
	// and clear stencil to zero for next light mask
	GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
		false, CF_LessEqual,
		false, CF_Equal, SO_Keep, SO_Keep, SO_Keep,		
		true, CF_Equal, SO_Zero, SO_Keep, SO_Zero,
		GET_STENCIL_MOBILE_SM_MASK(0x7) | STENCIL_SANDBOX_MASK, STENCIL_SANDBOX_MASK>::GetRHI();
}

static void SetLocalLightRasterizerAndDepthState(FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FViewInfo& View, const FSphere& LightBounds)
{
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

static void RenderLocalLight_StencilMask(FRHICommandListImmediate& RHICmdList, const FScene& Scene, const FViewInfo& View, const FLightSceneInfo& LightSceneInfo)
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

	FDeferredLightVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeferredLightVS::FRadialLight>(true);
	TShaderMapRef<FDeferredLightVS> VertexShader(View.ShaderMap, PermutationVector);
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = nullptr;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 1);

	FDeferredLightVS::FParameters ParametersVS = FDeferredLightVS::GetParameters(View, &LightSceneInfo);
	SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);

	if (LightType == LightType_Point)
	{
		StencilingGeometry::DrawSphere(RHICmdList);
	}
	else // LightType_Spot
	{
		StencilingGeometry::DrawCone(RHICmdList);
	}
}

static void RenderLocalLight(
	FRHICommandListImmediate& RHICmdList, 
	const FScene& Scene, 
	const FViewInfo& View, 
	const FLightSceneInfo& LightSceneInfo, 
	const FCachedLightMaterial& DefaultLightMaterial)
{
	if (!LightSceneInfo.ShouldRenderLight(View))
	{
		return;
	}

	const uint8 LightType = LightSceneInfo.Proxy->GetLightType();
	const bool bIsSpotLight = LightType == LightType_Spot;
	const bool bIsPointLight = LightType == LightType_Point;
	if (!bIsSpotLight && !bIsPointLight)
	{
		return;
	}

	FString LightNameWithLevel;
	FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo.Proxy, LightNameWithLevel);
	SCOPED_DRAW_EVENTF(RHICmdList, LocalLight, TEXT("%s"), *LightNameWithLevel);
	
	if (GMobileUseLightStencilCulling != 0)
	{
		RenderLocalLight_StencilMask(RHICmdList, Scene, View, LightSceneInfo);
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
	
	if (GMobileUseLightStencilCulling != 0)
	{
		SetLocalLightRasterizerAndDepthState_StencilMask(GraphicsPSOInit, View);
	}
	else
	{
		SetLocalLightRasterizerAndDepthState(GraphicsPSOInit, View, LightBounds);
	}

	FDeferredLightVS::FPermutationDomain PermutationVectorVS;
	PermutationVectorVS.Set<FDeferredLightVS::FRadialLight>(true);
	TShaderMapRef<FDeferredLightVS> VertexShader(View.ShaderMap, PermutationVectorVS);
		
	const FMaterialRenderProxy* LightFunctionMaterialProxy = nullptr;
	if (View.Family->EngineShowFlags.LightFunctions)
	{
		LightFunctionMaterialProxy = LightSceneInfo.Proxy->GetLightFunctionMaterial();
	}
	FMobileRadialLightFunctionPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FMobileRadialLightFunctionPS::FSpotLightDim>(bIsSpotLight);
	PermutationVector.Set<FMobileRadialLightFunctionPS::FIESProfileDim>(bUseIESTexture);
	FCachedLightMaterial LightMaterial;
	TShaderRef<FMobileRadialLightFunctionPS> PixelShader;
	GetLightMaterial(DefaultLightMaterial, LightFunctionMaterialProxy, PermutationVector.ToDimensionValueId(), LightMaterial, PixelShader);
			
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

	// Shade only MSM_DefaultLit pixels
	uint8 StencilRef = GET_STENCIL_MOBILE_SM_MASK(MSM_DefaultLit);
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, StencilRef);

	FDeferredLightVS::FParameters ParametersVS = FDeferredLightVS::GetParameters(View, &LightSceneInfo);
	SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);

	FMobileRadialLightFunctionPS::FParameters PassParameters;
	PassParameters.DeferredLightUniforms = TUniformBufferRef<FDeferredLightUniformStruct>::CreateUniformBufferImmediate(GetDeferredLightParameters(View, LightSceneInfo), EUniformBufferUsage::UniformBuffer_SingleFrame);
	PassParameters.IESTexture = IESTextureResource->TextureRHI;
	PassParameters.IESTextureSampler = IESTextureResource->SamplerStateRHI;
	const float TanOuterAngle = bIsSpotLight ? FMath::Tan(LightSceneInfo.Proxy->GetOuterConeAngle()) : 1.0f;
	PassParameters.LightFunctionParameters = FVector4f(TanOuterAngle, 1.0f /*ShadowFadeFraction*/, bIsSpotLight ? 1.0f : 0.0f, bIsPointLight ? 1.0f : 0.0f);
	PassParameters.LightFunctionParameters2 = FVector3f(LightSceneInfo.Proxy->GetLightFunctionFadeDistance(), LightSceneInfo.Proxy->GetLightFunctionDisabledBrightness(),	0.0f);
	const FVector Scale = LightSceneInfo.Proxy->GetLightFunctionScale();
	// Switch x and z so that z of the user specified scale affects the distance along the light direction
	const FVector InverseScale = FVector(1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X);
	const FMatrix WorldToLight = LightSceneInfo.Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));
	PassParameters.TranslatedWorldToLight = FMatrix44f(FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation()) * WorldToLight);
	PassParameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	PassParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FMobileRadialLightFunctionPS::SetParameters(RHICmdList, PixelShader, View, LightMaterial.MaterialProxy, *LightMaterial.Material, PassParameters);

	if (LightType == LightType_Point)
	{
		StencilingGeometry::DrawSphere(RHICmdList);
	}
	else // LightType_Spot
	{
		StencilingGeometry::DrawCone(RHICmdList);
	}
}

static void SetupSimpleLightPSO(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View,
	const TShaderMapRef<FDeferredLightVS>& VertexShader,
	const TShaderRef<FMobileRadialLightFunctionPS>& PixelShader, 
	FGraphicsPipelineStateInitializer& GraphicsPSOInit)
{
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	// Use additive blending for color
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetLocalLightRasterizerAndDepthState_StencilMask(GraphicsPSOInit, View);
}

static void RenderSimpleLights(
	FRHICommandListImmediate& RHICmdList, 
	const FScene& Scene, 
	int32 ViewIndex,
	int32 NumViews,
	const FViewInfo& View,
	const FSortedLightSetSceneInfo &SortedLightSet, 
	const FCachedLightMaterial& DefaultMaterial)
{
	const FSimpleLightArray& SimpleLights = SortedLightSet.SimpleLights;
	if (SimpleLights.InstanceData.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, SimpleLights);

	FDeferredLightVS::FPermutationDomain PermutationVectorVS;
	PermutationVectorVS.Set<FDeferredLightVS::FRadialLight>(true);
	TShaderMapRef<FDeferredLightVS> VertexShader(View.ShaderMap, PermutationVectorVS);
	TShaderRef<FMobileRadialLightFunctionPS> PixelShaders;
	{
		const FMaterialShaderMap* MaterialShaderMap = DefaultMaterial.Material->GetRenderingThreadShaderMap();
		FMobileRadialLightFunctionPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FMobileRadialLightFunctionPS::FSpotLightDim>(false);
		PermutationVector.Set<FMobileRadialLightFunctionPS::FIESProfileDim>(false);
		PixelShaders = MaterialShaderMap->GetShader<FMobileRadialLightFunctionPS>(PermutationVector);
	}

	// Setup PSOs we going to use for light rendering 
	FGraphicsPipelineStateInitializer GraphicsPSOLight;
	{
		SetupSimpleLightPSO(RHICmdList, View, VertexShader, PixelShaders, GraphicsPSOLight);
	}
	// Setup stencil mask PSO
	FGraphicsPipelineStateInitializer GraphicsPSOLightMask;
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOLightMask);
		GraphicsPSOLightMask.PrimitiveType = PT_TriangleList;
		GraphicsPSOLightMask.BlendState = TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
		GraphicsPSOLightMask.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
		// set stencil to 1 where depth test fails
		GraphicsPSOLightMask.DepthStencilState = TStaticDepthStencilState<
			false, CF_DepthNearOrEqual,
			true, CF_Always, SO_Keep, SO_Replace, SO_Keep,		
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			0x00, STENCIL_SANDBOX_MASK>::GetRHI();
		GraphicsPSOLightMask.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOLightMask.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOLightMask.BoundShaderState.PixelShaderRHI = nullptr;
	}
		
	for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
	{
		const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[LightIndex];
		const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, NumViews);
		const FSphere LightBounds(SimpleLightPerViewData.Position, SimpleLight.Radius);

		if (NumViews > 1)
		{
			// set viewports only we we have more than one 
			// otherwise it is set at the start of the pass
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
		}

		// Render light mask
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOLightMask, 1);

		FDeferredLightVS::FParameters ParametersVS = FDeferredLightVS::GetParameters(View, LightBounds);
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);

		StencilingGeometry::DrawSphere(RHICmdList);

		// Render light
		FMobileRadialLightFunctionPS::FParameters PassParameters;
		FDeferredLightUniformStruct DeferredLightUniformsValue = GetSimpleDeferredLightParameters(View, SimpleLight, SimpleLightPerViewData);
		PassParameters.DeferredLightUniforms = TUniformBufferRef<FDeferredLightUniformStruct>::CreateUniformBufferImmediate(DeferredLightUniformsValue, EUniformBufferUsage::UniformBuffer_SingleFrame);
		PassParameters.IESTexture = GWhiteTexture->TextureRHI;
		PassParameters.IESTextureSampler = GWhiteTexture->SamplerStateRHI;

		// Shade only MSM_DefaultLit pixels
		uint8 StencilRef = GET_STENCIL_MOBILE_SM_MASK(MSM_DefaultLit);

		{
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOLight, StencilRef);
			FMobileRadialLightFunctionPS::SetParameters(RHICmdList, PixelShaders, View, DefaultMaterial.MaterialProxy, *DefaultMaterial.Material, PassParameters);
		}

		// Apply the point or spot light with some approximately bounding geometry,
		// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
		StencilingGeometry::DrawSphere(RHICmdList);
	}
}

void MobileDeferredShadingPass(
	FRHICommandListImmediate& RHICmdList,
	int32 ViewIndex,
	int32 NumViews,
	const FViewInfo& View,
	const FScene& Scene, 
	const FSortedLightSetSceneInfo &SortedLightSet)
{
	SCOPED_DRAW_EVENT(RHICmdList, DeferredShading);
	
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

	// Default material for light rendering
	FCachedLightMaterial DefaultMaterial;
	DefaultMaterial.MaterialProxy = UMaterial::GetDefaultMaterial(MD_LightFunction)->GetRenderProxy();
	DefaultMaterial.Material = DefaultMaterial.MaterialProxy->GetMaterialNoFallback(ERHIFeatureLevel::ES3_1);
	check(DefaultMaterial.Material);

	RenderDirectLight(RHICmdList, Scene, View, DefaultMaterial);

	if (GMobileUseClusteredDeferredShading == 0)
	{
		// Render non-clustered simple lights
		RenderSimpleLights(RHICmdList, Scene, ViewIndex, NumViews, View, SortedLightSet, DefaultMaterial);
	}

	// Render non-clustered local lights
	int32 NumLights = SortedLightSet.SortedLights.Num();
	int32 StandardDeferredStart = SortedLightSet.SimpleLightsEnd;
	if (GMobileUseClusteredDeferredShading != 0)
	{
		StandardDeferredStart = SortedLightSet.ClusteredSupportedEnd;
	}

	for (int32 LightIdx = StandardDeferredStart; LightIdx < NumLights; ++LightIdx)
	{
		const FSortedLightSceneInfo& SortedLight = SortedLightSet.SortedLights[LightIdx];
		const FLightSceneInfo& LightSceneInfo = *SortedLight.LightSceneInfo;
		RenderLocalLight(RHICmdList, Scene, View, LightSceneInfo, DefaultMaterial);
	}
}