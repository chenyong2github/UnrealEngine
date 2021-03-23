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
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, Forward)
		SHADER_PARAMETER_STRUCT_REF(FMobileDirectionalLightShaderParameters, MobileDirectionalLight)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
		SHADER_PARAMETER(FMatrix, WorldToLight)
		SHADER_PARAMETER(FVector4, LightFunctionParameters)
		SHADER_PARAMETER(FVector, LightFunctionParameters2)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT(PREPROCESSOR_TO_STRING(MAX_MOBILE_SHADOWCASCADES)), GetMobileMaxShadowCascades());
		OutEnvironment.SetDefine(TEXT("SUPPORTS_TEXTURECUBE_ARRAY"), 1);
		OutEnvironment.SetDefine(TEXT("USE_LIGHT_FUNCTION"), Parameters.MaterialParameters.bIsDefaultMaterial ? 0 : 1);
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
		MaterialShader->SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
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
	class FInverseSquaredDim	: SHADER_PERMUTATION_BOOL("INVERSE_SQUARED_FALLOFF");
	class FIESProfileDim		: SHADER_PERMUTATION_BOOL("USE_IES_PROFILE");
	using FPermutationDomain = TShaderPermutationDomain<FSpotLightDim, FInverseSquaredDim, FIESProfileDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix, WorldToLight)
		SHADER_PARAMETER(FVector4, LightFunctionParameters)
		SHADER_PARAMETER(FVector, LightFunctionParameters2)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_TEXTURE(Texture2D, IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, IESTextureSampler)
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
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FLightSceneInfo* DirectionalLight = nullptr;
	for (int32 ChannelIdx = 0; ChannelIdx < UE_ARRAY_COUNT(Scene.MobileDirectionalLights) && !DirectionalLight; ChannelIdx++)
	{
		DirectionalLight = Scene.MobileDirectionalLights[ChannelIdx];
	}

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
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	FMobileDirectLightFunctionPS::FParameters PassParameters;
	PassParameters.Forward = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
	PassParameters.MobileDirectionalLight = Scene.UniformBuffers.MobileDirectionalLightUniformBuffers[1];
	PassParameters.ReflectionCaptureData = Scene.UniformBuffers.ReflectionCaptureUniformBuffer;
	FReflectionUniformParameters ReflectionUniformParameters;
	SetupReflectionUniformParameters(View, ReflectionUniformParameters);
	PassParameters.ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
	PassParameters.LightFunctionParameters = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
	if (DirectionalLight)
	{
		const bool bUseMovableLight = DirectionalLight && !DirectionalLight->Proxy->HasStaticShadowing();
		PassParameters.LightFunctionParameters2 = FVector(DirectionalLight->Proxy->GetLightFunctionFadeDistance(), DirectionalLight->Proxy->GetLightFunctionDisabledBrightness(), bUseMovableLight ? 1.0f : 0.0f);
		const FVector Scale = DirectionalLight->Proxy->GetLightFunctionScale();
		// Switch x and z so that z of the user specified scale affects the distance along the light direction
		const FVector InverseScale = FVector(1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X);
		PassParameters.WorldToLight = DirectionalLight->Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));
	}
	FMobileDirectLightFunctionPS::SetParameters(RHICmdList, PixelShader, View, LightMaterial.MaterialProxy, *LightMaterial.Material, PassParameters);
	
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

	TShaderMapRef<TDeferredLightVS<true>> VertexShader(View.ShaderMap);
		
	const FMaterialRenderProxy* LightFunctionMaterialProxy = nullptr;
	if (View.Family->EngineShowFlags.LightFunctions)
	{
		LightFunctionMaterialProxy = LightSceneInfo.Proxy->GetLightFunctionMaterial();
	}
	FMobileRadialLightFunctionPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FMobileRadialLightFunctionPS::FSpotLightDim>(bIsSpotLight);
	PermutationVector.Set<FMobileRadialLightFunctionPS::FInverseSquaredDim>(LightSceneInfo.Proxy->IsInverseSquared());
	PermutationVector.Set<FMobileRadialLightFunctionPS::FIESProfileDim>(bUseIESTexture);
	FCachedLightMaterial LightMaterial;
	TShaderRef<FMobileRadialLightFunctionPS> PixelShader;
	GetLightMaterial(DefaultLightMaterial, LightFunctionMaterialProxy, PermutationVector.ToDimensionValueId(), LightMaterial, PixelShader);
			
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, View, &LightSceneInfo);

	FMobileRadialLightFunctionPS::FParameters PassParameters;
	PassParameters.DeferredLightUniforms = TUniformBufferRef<FDeferredLightUniformStruct>::CreateUniformBufferImmediate(GetDeferredLightParameters(View, LightSceneInfo), EUniformBufferUsage::UniformBuffer_SingleFrame);
	PassParameters.IESTexture = IESTextureResource->TextureRHI;
	PassParameters.IESTextureSampler = IESTextureResource->SamplerStateRHI;
	const float TanOuterAngle = bIsSpotLight ? FMath::Tan(LightSceneInfo.Proxy->GetOuterConeAngle()) : 1.0f;
	PassParameters.LightFunctionParameters = FVector4(TanOuterAngle, 1.0f /*ShadowFadeFraction*/, bIsSpotLight ? 1.0f : 0.0f, bIsPointLight ? 1.0f : 0.0f);
	PassParameters.LightFunctionParameters2 = FVector(LightSceneInfo.Proxy->GetLightFunctionFadeDistance(), LightSceneInfo.Proxy->GetLightFunctionDisabledBrightness(),	0.0f);
	const FVector Scale = LightSceneInfo.Proxy->GetLightFunctionScale();
	// Switch x and z so that z of the user specified scale affects the distance along the light direction
	const FVector InverseScale = FVector(1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X);
	PassParameters.WorldToLight = LightSceneInfo.Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));
	FMobileRadialLightFunctionPS::SetParameters(RHICmdList, PixelShader, View, LightMaterial.MaterialProxy, *LightMaterial.Material, PassParameters);

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

static void SetupSimpleLightPSO(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View,
	const TShaderMapRef<TDeferredLightVS<true>>& VertexShader,
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
	const TArrayView<const FViewInfo*> PassViews, 
	const FSortedLightSetSceneInfo &SortedLightSet, 
	const FCachedLightMaterial& DefaultMaterial)
{
	const FSimpleLightArray& SimpleLights = SortedLightSet.SimpleLights;
	const int32 NumViews = PassViews.Num();
	const FViewInfo& View0 = *PassViews[0];

	TShaderMapRef<TDeferredLightVS<true>> VertexShader(View0.ShaderMap);
	TShaderRef<FMobileRadialLightFunctionPS> PixelShaders[2];
	{
		const FMaterialShaderMap* MaterialShaderMap = DefaultMaterial.Material->GetRenderingThreadShaderMap();
		FMobileRadialLightFunctionPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FMobileRadialLightFunctionPS::FSpotLightDim>(false);
		PermutationVector.Set<FMobileRadialLightFunctionPS::FIESProfileDim>(false);
		PermutationVector.Set<FMobileRadialLightFunctionPS::FInverseSquaredDim>(false);
		PixelShaders[0] = MaterialShaderMap->GetShader<FMobileRadialLightFunctionPS>(PermutationVector);
		PermutationVector.Set<FMobileRadialLightFunctionPS::FInverseSquaredDim>(true);
		PixelShaders[1] = MaterialShaderMap->GetShader<FMobileRadialLightFunctionPS>(PermutationVector);
	}

	// Setup PSOs we going to use for light rendering 
	FGraphicsPipelineStateInitializer GraphicsPSOLight[2];
	{
		SetupSimpleLightPSO(RHICmdList, View0, VertexShader, PixelShaders[0], GraphicsPSOLight[0]);
		SetupSimpleLightPSO(RHICmdList, View0, VertexShader, PixelShaders[1], GraphicsPSOLight[1]);
	}
	// Setup stencil mask PSO
	FGraphicsPipelineStateInitializer GraphicsPSOLightMask;
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOLightMask);
		GraphicsPSOLightMask.PrimitiveType = PT_TriangleList;
		GraphicsPSOLightMask.BlendState = TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
		GraphicsPSOLightMask.RasterizerState = View0.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
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
		for (int32 ViewIndex = 0; ViewIndex < NumViews; ViewIndex++)
		{
			const FViewInfo& View = *PassViews[ViewIndex];
			const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, NumViews);
			const FSphere LightBounds(SimpleLightPerViewData.Position, SimpleLight.Radius);
			
			if (NumViews > 1)
			{
				// set viewports only we we have more than one 
				// otherwise it is set at the start of the pass
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
			}

			// Render light mask
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOLightMask);
			VertexShader->SetSimpleLightParameters(RHICmdList, View, LightBounds);
			RHICmdList.SetStencilRef(1);
			StencilingGeometry::DrawSphere(RHICmdList);
						
			// Render light
			FMobileRadialLightFunctionPS::FParameters PassParameters;
			FDeferredLightUniformStruct DeferredLightUniformsValue;
			SetupSimpleDeferredLightParameters(SimpleLight, SimpleLightPerViewData, DeferredLightUniformsValue);
			PassParameters.DeferredLightUniforms = TUniformBufferRef<FDeferredLightUniformStruct>::CreateUniformBufferImmediate(DeferredLightUniformsValue, EUniformBufferUsage::UniformBuffer_SingleFrame);
			PassParameters.IESTexture = GWhiteTexture->TextureRHI;
			PassParameters.IESTextureSampler = GWhiteTexture->SamplerStateRHI;
			if (SimpleLight.Exponent == 0)
			{
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOLight[1]);
				FMobileRadialLightFunctionPS::SetParameters(RHICmdList, PixelShaders[1], View, DefaultMaterial.MaterialProxy, *DefaultMaterial.Material, PassParameters);
			}
			else
			{
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOLight[0]);
				FMobileRadialLightFunctionPS::SetParameters(RHICmdList, PixelShaders[0], View, DefaultMaterial.MaterialProxy, *DefaultMaterial.Material, PassParameters);
			}
			VertexShader->SetSimpleLightParameters(RHICmdList, View, LightBounds);
			
			// Shade only MSM_DefaultLit pixels
			uint8 StencilRef = GET_STENCIL_MOBILE_SM_MASK(MSM_DefaultLit);
			RHICmdList.SetStencilRef(StencilRef);

			// Apply the point or spot light with some approximately bounding geometry,
			// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
			StencilingGeometry::DrawSphere(RHICmdList);
		}
	}
}

void MobileDeferredShadingPass(
	FRHICommandListImmediate& RHICmdList, 
	const FScene& Scene, 
	const TArrayView<const FViewInfo*> PassViews, 
	const FSortedLightSetSceneInfo &SortedLightSet)
{
	SCOPED_DRAW_EVENT(RHICmdList, MobileDeferredShading);

	const FViewInfo& View0 = *PassViews[0];

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FUniformBufferRHIRef PassUniformBuffer = CreateMobileSceneTextureUniformBuffer(RHICmdList);
	FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
	SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);
	RHICmdList.SetViewport(View0.ViewRect.Min.X, View0.ViewRect.Min.Y, 0.0f, View0.ViewRect.Max.X, View0.ViewRect.Max.Y, 1.0f);

	// Default material for light rendering
	FCachedLightMaterial DefaultMaterial;
	DefaultMaterial.MaterialProxy = UMaterial::GetDefaultMaterial(MD_LightFunction)->GetRenderProxy();
	DefaultMaterial.Material = DefaultMaterial.MaterialProxy->GetMaterialNoFallback(ERHIFeatureLevel::ES3_1);
	check(DefaultMaterial.Material);

	RenderDirectLight(RHICmdList, Scene, View0, DefaultMaterial);

	if (GMobileUseClusteredDeferredShading == 0)
	{
		// Render non-clustered simple lights
		RenderSimpleLights(RHICmdList, Scene, PassViews, SortedLightSet, DefaultMaterial);
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
		RenderLocalLight(RHICmdList, Scene, View0, LightSceneInfo, DefaultMaterial);
	}
}
