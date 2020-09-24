// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneDirectLighting.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"

int32 GLumenDirectLighting = 1;
FAutoConsoleVariableRef CVarLumenDirectLighting(
	TEXT("r.Lumen.DirectLighting"),
	GLumenDirectLighting,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenDirectLightingForceOffscreenShadowing = 0;
FAutoConsoleVariableRef CVarLumenDirectLightingForceOffscreenShadowing(
	TEXT("r.Lumen.DirectLighting.ForceOffscreenShadowing"),
	GLumenDirectLightingForceOffscreenShadowing,
	TEXT("Use offscreen shadowing for all lights casting shadows."),
	ECVF_RenderThreadSafe
);


int32 GLumenDirectLightingBatchSize = 16;
FAutoConsoleVariableRef CVarLumenDirectLightingBatchSize(
	TEXT("r.Lumen.DirectLighting.BatchSize"),
	GLumenDirectLightingBatchSize,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GOffscreenShadowingConeAngle = .2f;
FAutoConsoleVariableRef CVarOffscreenShadowingConeAngle(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowingConeAngle"),
	GOffscreenShadowingConeAngle,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GOffscreenShadowingMaxTraceDistance = 10000;
FAutoConsoleVariableRef CVarOffscreenShadowingMaxTraceDistance(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowingMaxTraceDistance"),
	GOffscreenShadowingMaxTraceDistance,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GOffscreenShadowingTraceStepFactor = 5;
FAutoConsoleVariableRef CVarOffscreenShadowingTraceStepFactor(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowingTraceStepFactor"),
	GOffscreenShadowingTraceStepFactor,
	TEXT(""),
	ECVF_RenderThreadSafe
	);


float GOffscreenShadowingSDFSurfaceBiasScale = 2;
FAutoConsoleVariableRef CVarOffscreenShadowingSDFSurfaceBiasScale(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowingSDFSurfaceBiasScale"),
	GOffscreenShadowingSDFSurfaceBiasScale,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GShadowingSurfaceBias = 20;
FAutoConsoleVariableRef CVarShadowingSurfaceBias(
	TEXT("r.Lumen.DirectLighting.ShadowingSurfaceBias"),
	GShadowingSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GShadowingSlopeScaledSurfaceBias = 50;
FAutoConsoleVariableRef CVarShadowingSlopeScaledSurfaceBias(
	TEXT("r.Lumen.DirectLighting.ShadowingSlopeScaledSurfaceBias"),
	GShadowingSlopeScaledSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenSceneCardDirectLightingUpdateFrequencyScale = .003f;
FAutoConsoleVariableRef CVarLumenSceneCardDirectLightingUpdateFrequencyScale(
	TEXT("r.Lumen.DirectLighting.CardUpdateFrequencyScale"),
	GLumenSceneCardDirectLightingUpdateFrequencyScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

BEGIN_SHADER_PARAMETER_STRUCT(FLightFunctionParameters, )
	SHADER_PARAMETER(FVector4, LightFunctionParameters)
	SHADER_PARAMETER(FMatrix, LightFunctionWorldToLight)
	SHADER_PARAMETER(FVector, LightFunctionParameters2)
END_SHADER_PARAMETER_STRUCT()

class FLumenCardDirectLightingPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenCardDirectLightingPS, Material);

	FLumenCardDirectLightingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) 
		: FMaterialShader(Initializer) 
	{ 
		Bindings.BindForLegacyShaderParameters( 
			this, 
			Initializer.PermutationId, 
			Initializer.ParameterMap,
			*FParameters::FTypeInfo::GetStructMetadata(), 
			// Don't require full bindings, we use FMaterialShader::SetParameters
			false); 
	} 
	
	FLumenCardDirectLightingPS() {}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParameters, VolumeShadowingShaderParameters)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)	
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightFunctionParameters, LightFunctionParameters)
		SHADER_PARAMETER(float, StepFactor)
		SHADER_PARAMETER(float, ConeHalfAngle)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)
		SHADER_PARAMETER(float, SDFSurfaceBiasScale)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>,ShadowMaskAtlas)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicallyShadowed	: SHADER_PERMUTATION_BOOL("DYNAMICALLY_SHADOWED");
	class FForceOffscreenShadowing : SHADER_PERMUTATION_BOOL("FORCE_OFFSCREEN_SHADOWING");
	class FShadowed	: SHADER_PERMUTATION_BOOL("SHADOWED_LIGHT");
	class FLightFunction : SHADER_PERMUTATION_BOOL("LIGHT_FUNCTION");
	class FLightType : SHADER_PERMUTATION_ENUM_CLASS("LIGHT_TYPE", ELumenLightType);
	class FRayTracingShadowPassCombine : SHADER_PERMUTATION_BOOL("HARDWARE_RAYTRACING_SHADOW_PASS_COMBINE");
	using FPermutationDomain = TShaderPermutationDomain<FLightType, FDynamicallyShadowed, FShadowed, 
								FForceOffscreenShadowing, FLightFunction, FRayTracingShadowPassCombine>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (!PermutationVector.Get<FShadowed>())
		{
			PermutationVector.Set<FForceOffscreenShadowing>(false);
			PermutationVector.Set<FDynamicallyShadowed>(false);
			PermutationVector.Set<FRayTracingShadowPassCombine>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return Parameters.MaterialParameters.MaterialDomain == EMaterialDomain::MD_LightFunction && DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	FORCENOINLINE static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) 
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FLumenCardDirectLightingPS, TEXT("/Engine/Private/Lumen/LumenSceneDirectLighting.usf"), TEXT("LumenCardDirectLightingPS"), SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardDirectLighting, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardDirectLightingPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void SetupLightFunctionParameters(const FLightSceneInfo* LightSceneInfo, float ShadowFadeFraction, FLightFunctionParameters& OutParameters)
{
	const bool bIsSpotLight = LightSceneInfo->Proxy->GetLightType() == LightType_Spot;
	const bool bIsPointLight = LightSceneInfo->Proxy->GetLightType() == LightType_Point;
	const float TanOuterAngle = bIsSpotLight ? FMath::Tan(LightSceneInfo->Proxy->GetOuterConeAngle()) : 1.0f;

	OutParameters.LightFunctionParameters = FVector4(TanOuterAngle, ShadowFadeFraction, bIsSpotLight ? 1.0f : 0.0f, bIsPointLight ? 1.0f : 0.0f);

	const FVector Scale = LightSceneInfo->Proxy->GetLightFunctionScale();
	// Switch x and z so that z of the user specified scale affects the distance along the light direction
	const FVector InverseScale = FVector( 1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X );
	const FMatrix WorldToLight = LightSceneInfo->Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));	

	OutParameters.LightFunctionWorldToLight = WorldToLight;

	const float PreviewShadowsMask = 0.0f;
	OutParameters.LightFunctionParameters2 = FVector(
		LightSceneInfo->Proxy->GetLightFunctionFadeDistance(),
		LightSceneInfo->Proxy->GetLightFunctionDisabledBrightness(),
		PreviewShadowsMask);
}

void RenderDirectLightIntoLumenCards(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FEngineShowFlags& EngineShowFlags,
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	FRDGTextureRef FinalLightingAtlas,
	FRDGTextureRef OpacityAtlas,
	const FLightSceneInfo* LightSceneInfo,
	const FString& LightName,
	const FLumenCardScatterContext& CardScatterContext,
	int32 ScatterInstanceIndex,
	FLumenDirectLightingHardwareRayTracingData& LumenDirectLightingHardwareRayTracingData)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
	const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();
	bool bDynamicallyShadowed = false;
	bool bShadowed = LightSceneInfo->Proxy->CastsDynamicShadow();

	ELumenLightType LumenLightType = ELumenLightType::MAX;
	{
		switch (LightType)
		{
		case LightType_Directional: LumenLightType = ELumenLightType::Directional;	break;
		case LightType_Point:		LumenLightType = ELumenLightType::Point;		break;
		case LightType_Spot:		LumenLightType = ELumenLightType::Spot;			break;
		case LightType_Rect:		LumenLightType = ELumenLightType::Rect;			break;
		}
		check(LumenLightType != ELumenLightType::MAX);
	}

	extern const FProjectedShadowInfo* GetShadowForInjectionIntoVolumetricFog(FVisibleLightInfo& VisibleLightInfo);
	const FProjectedShadowInfo* ProjectedShadowInfo = GetShadowForInjectionIntoVolumetricFog(VisibleLightInfos[LightSceneInfo->Id]);

	bDynamicallyShadowed = ProjectedShadowInfo != nullptr;


	// Run ray traced shadow pass only when it's enabled, the light shadow is enabled and the ray tracing scene has been setup.
	// to save frame time.
	const bool bLumenUseHardwareRayTracedShadow = Lumen::UseHardwareRayTracedShadows(View) && bShadowed;

	if (bLumenUseHardwareRayTracedShadow)
	{
		RenderHardwareRayTracedShadowIntoLumenCards(
			GraphBuilder, Scene, View, OpacityAtlas, LightSceneInfo, 
			LightName,CardScatterContext, ScatterInstanceIndex, 
			LumenDirectLightingHardwareRayTracingData, bDynamicallyShadowed, LumenLightType);
	}


	//Use ray-traced shadow if Ray Tracing is enabled otherwise use shadow map and sdf.
	FLumenCardDirectLighting* PassParameters = GraphBuilder.AllocParameters<FLumenCardDirectLighting>();
	{
		PassParameters->RenderTargets[0] = FRenderTargetBinding(FinalLightingAtlas, ERenderTargetLoadAction::ELoad);
		PassParameters->VS.InfluenceSphere = FVector4(LightBounds.Center, LightBounds.W);
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = ScatterInstanceIndex;
		PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;

		GetVolumeShadowingShaderParameters(
			View,
			LightSceneInfo,
			ProjectedShadowInfo,
			0,
			bDynamicallyShadowed,
			PassParameters->PS.VolumeShadowingShaderParameters);

		FDeferredLightUniformStruct DeferredLightUniforms = GetDeferredLightParameters(View, *LightSceneInfo);

		if (LightSceneInfo->Proxy->IsInverseSquared())
		{
			DeferredLightUniforms.LightParameters.FalloffExponent = 0;
		}

		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->PS.OpacityAtlas = OpacityAtlas;
		DeferredLightUniforms.LightParameters.Color *= LightSceneInfo->Proxy->GetIndirectLightingScale();
		PassParameters->PS.DeferredLightUniforms = CreateUniformBufferImmediate(DeferredLightUniforms, UniformBuffer_SingleDraw);
		PassParameters->PS.ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
		SetupLightFunctionParameters(LightSceneInfo, 1.0f, PassParameters->PS.LightFunctionParameters);
		PassParameters->PS.ConeHalfAngle = GOffscreenShadowingConeAngle * PI / 180.0f;
		PassParameters->PS.MaxTraceDistance = GOffscreenShadowingMaxTraceDistance;
		PassParameters->PS.StepFactor = FMath::Clamp(GOffscreenShadowingTraceStepFactor, .1f, 10.0f);
		PassParameters->PS.SurfaceBias = FMath::Clamp(GShadowingSurfaceBias, .01f, 100.0f);
		PassParameters->PS.SlopeScaledSurfaceBias = FMath::Clamp(GShadowingSlopeScaledSurfaceBias, .01f, 100.0f);
		PassParameters->PS.SDFSurfaceBiasScale = FMath::Clamp(GOffscreenShadowingSDFSurfaceBiasScale, .01f, 100.0f);

		if (bLumenUseHardwareRayTracedShadow)
		{
			PassParameters->PS.ShadowMaskAtlas = LumenDirectLightingHardwareRayTracingData.ShadowMaskAtlas;
		}
	}

	FRasterizeToCardsVS::FPermutationDomain VSPermutationVector;
	VSPermutationVector.Set< FRasterizeToCardsVS::FClampToInfluenceSphere >(LightType != LightType_Directional);
	auto VertexShader = View.ShaderMap->GetShader<FRasterizeToCardsVS>(VSPermutationVector);
	const FMaterialRenderProxy* LightFunctionMaterialProxy = LightSceneInfo->Proxy->GetLightFunctionMaterial();
	bool bUseLightFunction = true;

	if (!LightFunctionMaterialProxy
		|| !LightFunctionMaterialProxy->GetMaterial(Scene->GetFeatureLevel())->IsLightFunction()
		|| !EngineShowFlags.LightFunctions)
	{
		bUseLightFunction = false;
		LightFunctionMaterialProxy = UMaterial::GetDefaultMaterial(MD_LightFunction)->GetRenderProxy();
	}

	FLumenCardDirectLightingPS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FLumenCardDirectLightingPS::FLightType >(LumenLightType);
	PermutationVector.Set< FLumenCardDirectLightingPS::FDynamicallyShadowed >(bDynamicallyShadowed);
	PermutationVector.Set< FLumenCardDirectLightingPS::FShadowed >(bShadowed);
	PermutationVector.Set< FLumenCardDirectLightingPS::FForceOffscreenShadowing >(GLumenDirectLightingForceOffscreenShadowing != 0);
	PermutationVector.Set< FLumenCardDirectLightingPS::FLightFunction >(bUseLightFunction);
	PermutationVector.Set< FLumenCardDirectLightingPS::FRayTracingShadowPassCombine>(bLumenUseHardwareRayTracedShadow);
	
	PermutationVector = FLumenCardDirectLightingPS::RemapPermutation(PermutationVector);

	const FMaterial* Material = LightFunctionMaterialProxy->GetMaterial(Scene->GetFeatureLevel());
	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();
	auto PixelShader = MaterialShaderMap->GetShader<FLumenCardDirectLightingPS>(PermutationVector);

	const uint32 CardIndirectArgOffset = CardScatterContext.GetIndirectArgOffset(ScatterInstanceIndex);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s %s", *LightName, bDynamicallyShadowed ? TEXT("Shadowmap") : TEXT("")),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = LumenSceneData.MaxAtlasSize, PassParameters, LightSceneInfo, VertexShader, PixelShader, GlobalShaderMap = View.ShaderMap, LightFunctionMaterialProxy, Material, &View, CardIndirectArgOffset](FRHICommandListImmediate& RHICmdList)
		{
			DrawQuadsToAtlas(
				MaxAtlasSize,
				VertexShader,
				PixelShader,
				PassParameters,
				GlobalShaderMap,
				TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI(),
				RHICmdList,
				[LightFunctionMaterialProxy, Material, &View](FRHICommandListImmediate& RHICmdList, TShaderRefBase<FLumenCardDirectLightingPS, FShaderMapPointerTable> Shader, FRHIPixelShader* ShaderRHI, const FLumenCardDirectLightingPS::FParameters& Parameters)
				{
					Shader->SetParameters(RHICmdList, ShaderRHI, LightFunctionMaterialProxy, *Material, View);
				},
				CardIndirectArgOffset);
		});
}

void FDeferredShadingSceneRenderer::RenderDirectLightingForLumenScene(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef FinalLightingAtlas,
	FRDGTextureRef OpacityAtlas,
	FGlobalShaderMap* GlobalShaderMap,
	const FLumenCardScatterContext& VisibleCardScatterContext)
{
	LLM_SCOPE(ELLMTag::Lumen);

	if (GLumenDirectLighting)
	{
		check(Lumen::ShouldPrepareGlobalDistanceField(ShaderPlatform));
		RDG_EVENT_SCOPE(GraphBuilder, "DirectLighting");
		QUICK_SCOPE_CYCLE_COUNTER(RenderDirectLightingForLumenScene);

		const FViewInfo& MainView = Views[0];
		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		const bool bLumenUseHardwareRayTracedShadow = Lumen::UseHardwareRayTracedShadows(MainView);
		FLumenDirectLightingHardwareRayTracingData LumenDirectLightingHardwareRayTracingData;
		
		if(bLumenUseHardwareRayTracedShadow)
		{
			LumenDirectLightingHardwareRayTracingData.Initialize(GraphBuilder, Scene);
		}

		TArray<const FLightSceneInfo*, TInlineAllocator<64>> GatheredLocalLights;

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			const FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

			if (LightSceneInfo->ShouldRenderLightViewIndependent()
				&& LightSceneInfo->ShouldRenderLight(MainView, true)
				&& LightSceneInfo->Proxy->GetIndirectLightingScale() > 0.0f)
			{
				const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();

				if (LightType == LightType_Directional)
				{
					// Doesn't require culling, just draw immediately

					FString LightNameWithLevel;
					FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightNameWithLevel);

					RenderDirectLightIntoLumenCards(
						GraphBuilder,
						Scene,
						MainView,
						ViewFamily.EngineShowFlags,
						VisibleLightInfos,
						FinalLightingAtlas,
						OpacityAtlas,
						LightSceneInfo,
						LightNameWithLevel,
						VisibleCardScatterContext,
						0,
						LumenDirectLightingHardwareRayTracingData);
				}
				else
				{
					GatheredLocalLights.Add(LightSceneInfo);
				}
			}
		}

		const int32 LightBatchSize = FMath::Clamp(GLumenDirectLightingBatchSize, 1, 256);

		// Batched light culling and drawing
		for (int32 LightBatchIndex = 0; LightBatchIndex * LightBatchSize < GatheredLocalLights.Num(); ++LightBatchIndex)
		{
			const int32 FirstLightIndex = LightBatchIndex * LightBatchSize;
			const int32 LastLightIndex = FMath::Min((LightBatchIndex + 1) * LightBatchSize, GatheredLocalLights.Num());

			FLumenCardScatterContext CardScatterContext;

			{
				RDG_EVENT_SCOPE(GraphBuilder, "Cull Cards %d Lights", LastLightIndex - FirstLightIndex);

				CardScatterContext.Init(
					GraphBuilder,
					MainView,
					LumenSceneData,
					LumenCardRenderer,
					ECullCardsMode::OperateOnSceneForceUpdateForCardsToRender,
					LightBatchSize);

				for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
				{
					const int32 ScatterInstanceIndex = LightIndex - FirstLightIndex;
					const FLightSceneInfo* LightSceneInfo = GatheredLocalLights[LightIndex];
					const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();
					const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();

					ECullCardsShapeType ShapeType = ECullCardsShapeType::None;

					if (LightType == LightType_Point)
					{
						ShapeType = ECullCardsShapeType::PointLight;
					}
					else if (LightType == LightType_Spot)
					{
						ShapeType = ECullCardsShapeType::SpotLight;
					}
					else if (LightType == LightType_Rect)
					{
						ShapeType = ECullCardsShapeType::RectLight;
					}
					else
					{
						ensureMsgf(false, TEXT("Need Lumen card culling for new light type"));
					}

					FCullCardsShapeParameters ShapeParameters;
					ShapeParameters.InfluenceSphere = FVector4(LightBounds.Center, LightBounds.W);
					ShapeParameters.LightPosition = LightSceneInfo->Proxy->GetPosition();
					ShapeParameters.LightDirection = LightSceneInfo->Proxy->GetDirection();
					ShapeParameters.LightRadius = LightSceneInfo->Proxy->GetRadius();
					ShapeParameters.CosConeAngle = FMath::Cos(LightSceneInfo->Proxy->GetOuterConeAngle());
					ShapeParameters.SinConeAngle = FMath::Sin(LightSceneInfo->Proxy->GetOuterConeAngle());

					CardScatterContext.CullCardsToShape(
						GraphBuilder,
						MainView,
						LumenSceneData,
						LumenCardRenderer,
						ShapeType,
						ShapeParameters,
						GLumenSceneCardDirectLightingUpdateFrequencyScale,
						ScatterInstanceIndex);
				}

				CardScatterContext.BuildScatterIndirectArgs(
					GraphBuilder,
					MainView);
			}

			{
				RDG_EVENT_SCOPE(GraphBuilder, "Draw %d Lights", LastLightIndex - FirstLightIndex);

				for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
				{
					const int32 ScatterInstanceIndex = LightIndex - FirstLightIndex;
					const FLightSceneInfo* LightSceneInfo = GatheredLocalLights[LightIndex];

					FString LightNameWithLevel;
					FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightNameWithLevel);

					RenderDirectLightIntoLumenCards(
						GraphBuilder,
						Scene,
						MainView,
						ViewFamily.EngineShowFlags,
						VisibleLightInfos,
						FinalLightingAtlas,
						OpacityAtlas,
						LightSceneInfo,
						LightNameWithLevel,
						CardScatterContext,
						ScatterInstanceIndex,
						LumenDirectLightingHardwareRayTracingData);
				}
			}
		}
	}
}
