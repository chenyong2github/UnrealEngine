// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingLighting.h"
#include "RHI/Public/RHIDefinitions.h"
#include "RendererPrivate.h"

#if RHI_RAYTRACING

#include "SceneRendering.h"

static TAutoConsoleVariable<int32> CVarRayTracingLightingMissShader(
	TEXT("r.RayTracing.LightingMissShader"),
	1,
	TEXT("Whether evaluate lighting using a miss shader when rendering reflections and translucency instead of doing it in ray generation shader. (default = 1)"),
	ECVF_RenderThreadSafe);

bool CanUseRayTracingLightingMissShader(EShaderPlatform ShaderPlatform)
{
	return GRHISupportsRayTracingMissShaderBindings
		&& CVarRayTracingLightingMissShader.GetValueOnRenderThread() != 0
		&& FDataDrivenShaderPlatformInfo::GetInfo(ShaderPlatform).bSupportsRayTracingMissShaderBindings;
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRaytracingLightDataPacked, "RaytracingLightsDataPacked");

void SetupRaytracingLightDataPacked(
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const FViewInfo& View,
	FRaytracingLightDataPacked* LightData,
	TResourceArray<FRTLightingData>& LightDataArray)
{
	TMap<UTextureLightProfile*, int32> IESLightProfilesMap;
	TMap<FRHITexture*, uint32> RectTextureMap;

	LightData->Count = 0;
	LightData->LTCMatTexture = GSystemTextures.LTCMat->GetRenderTargetItem().ShaderResourceTexture;
	LightData->LTCMatSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	LightData->LTCAmpTexture = GSystemTextures.LTCAmp->GetRenderTargetItem().ShaderResourceTexture;
	LightData->LTCAmpSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FTextureRHIRef DymmyWhiteTexture = GWhiteTexture->TextureRHI;
	LightData->RectLightTexture0 = DymmyWhiteTexture;
	LightData->RectLightTexture1 = DymmyWhiteTexture;
	LightData->RectLightTexture2 = DymmyWhiteTexture;
	LightData->RectLightTexture3 = DymmyWhiteTexture;
	LightData->RectLightTexture4 = DymmyWhiteTexture;
	LightData->RectLightTexture5 = DymmyWhiteTexture;
	LightData->RectLightTexture6 = DymmyWhiteTexture;
	LightData->RectLightTexture7 = DymmyWhiteTexture;
	static constexpr uint32 MaxRectLightTextureSlos = 8;
	static constexpr uint32 InvalidTextureIndex = 99; // #dxr_todo: share this definition with ray tracing shaders

	{
		// IES profiles
		FRHITexture* IESTextureRHI = nullptr;
		float IESInvProfileCount = 1.0f;

		if (View.IESLightProfileResource && View.IESLightProfileResource->GetIESLightProfilesCount())
		{
			LightData->IESLightProfileTexture = View.IESLightProfileResource->GetTexture();

			uint32 ProfileCount = View.IESLightProfileResource->GetIESLightProfilesCount();
			IESInvProfileCount = ProfileCount ? 1.f / static_cast<float>(ProfileCount) : 0.f;
		}
		else
		{
			LightData->IESLightProfileTexture = GWhiteTexture->TextureRHI;
		}

		LightData->IESLightProfileInvCount = IESInvProfileCount;
		LightData->IESLightProfileTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}

	for (auto Light : Lights)
	{
		const bool bHasStaticLighting = Light.LightSceneInfo->Proxy->HasStaticLighting() && Light.LightSceneInfo->IsPrecomputedLightingValid();
		const bool bAffectReflection = Light.LightSceneInfo->Proxy->AffectReflection();
		if (bHasStaticLighting || !bAffectReflection) continue;

		FLightShaderParameters LightParameters;
		Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);

		if (Light.LightSceneInfo->Proxy->IsInverseSquared())
		{
			LightParameters.FalloffExponent = 0;
		}

		int32 IESLightProfileIndex = INDEX_NONE;
		if (View.Family->EngineShowFlags.TexturedLightProfiles)
		{
			UTextureLightProfile* IESLightProfileTexture = Light.LightSceneInfo->Proxy->GetIESTexture();
			if (IESLightProfileTexture)
			{
				int32* IndexFound = IESLightProfilesMap.Find(IESLightProfileTexture);
				if (!IndexFound)
				{
					IESLightProfileIndex = IESLightProfilesMap.Add(IESLightProfileTexture, IESLightProfilesMap.Num());
				}
				else
				{
					IESLightProfileIndex = *IndexFound;
				}
			}
		}

		FRTLightingData LightDataElement;

		LightDataElement.Type = Light.LightType;
		LightDataElement.LightProfileIndex = IESLightProfileIndex;
		LightDataElement.RectLightTextureIndex = InvalidTextureIndex;

		for (int32 Element = 0; Element < 3; Element++)
		{
			LightDataElement.Direction[Element] = LightParameters.Direction[Element];
			LightDataElement.LightPosition[Element] = LightParameters.Position[Element];
			LightDataElement.LightColor[Element] = LightParameters.Color[Element];
			LightDataElement.Tangent[Element] = LightParameters.Tangent[Element];
		}

		const FVector2D FadeParams = Light.LightSceneInfo->Proxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), Light.LightSceneInfo->IsPrecomputedLightingValid(), View.MaxShadowCascades);
		const FVector2D DistanceFadeMAD = { FadeParams.Y, -FadeParams.X * FadeParams.Y };

		for (int32 Element = 0; Element < 2; Element++)
		{
			LightDataElement.SpotAngles[Element] = LightParameters.SpotAngles[Element];
			LightDataElement.DistanceFadeMAD[Element] = DistanceFadeMAD[Element];
		}

		LightDataElement.InvRadius = LightParameters.InvRadius;
		LightDataElement.SpecularScale = LightParameters.SpecularScale;
		LightDataElement.FalloffExponent = LightParameters.FalloffExponent;
		LightDataElement.SourceRadius = LightParameters.SourceRadius;
		LightDataElement.SourceLength = LightParameters.SourceLength;
		LightDataElement.SoftSourceRadius = LightParameters.SoftSourceRadius;
		LightDataElement.RectLightBarnCosAngle = LightParameters.RectLightBarnCosAngle;
		LightDataElement.RectLightBarnLength = LightParameters.RectLightBarnLength;
		LightDataElement.Pad = 0;

		LightDataArray.Add(LightDataElement);

		const bool bRequireTexture = Light.LightType == ELightComponentType::LightType_Rect && LightParameters.SourceTexture;
		uint32 RectLightTextureIndex = InvalidTextureIndex;
		if (bRequireTexture)
		{
			const uint32* IndexFound = RectTextureMap.Find(LightParameters.SourceTexture);
			if (!IndexFound)
			{
				if (RectTextureMap.Num() < MaxRectLightTextureSlos)
				{
					RectLightTextureIndex = RectTextureMap.Num();
					RectTextureMap.Add(LightParameters.SourceTexture, RectLightTextureIndex);
				}
			}
			else
			{
				RectLightTextureIndex = *IndexFound;
			}
		}

		if (RectLightTextureIndex != InvalidTextureIndex)
		{
			LightDataArray[LightData->Count].RectLightTextureIndex = RectLightTextureIndex;
			switch (RectLightTextureIndex)
			{
			case 0: LightData->RectLightTexture0 = LightParameters.SourceTexture; break;
			case 1: LightData->RectLightTexture1 = LightParameters.SourceTexture; break;
			case 2: LightData->RectLightTexture2 = LightParameters.SourceTexture; break;
			case 3: LightData->RectLightTexture3 = LightParameters.SourceTexture; break;
			case 4: LightData->RectLightTexture4 = LightParameters.SourceTexture; break;
			case 5: LightData->RectLightTexture5 = LightParameters.SourceTexture; break;
			case 6: LightData->RectLightTexture6 = LightParameters.SourceTexture; break;
			case 7: LightData->RectLightTexture7 = LightParameters.SourceTexture; break;
			}
		}

		LightData->Count++;

		if (LightData->Count >= RAY_TRACING_LIGHT_COUNT_MAXIMUM) break;
	}

	// Update IES light profiles texture 
	// TODO (Move to a shared place)
	if (View.IESLightProfileResource != nullptr && IESLightProfilesMap.Num() > 0)
	{
		TArray<UTextureLightProfile*, SceneRenderingAllocator> IESProfilesArray;
		IESProfilesArray.AddUninitialized(IESLightProfilesMap.Num());
		for (auto It = IESLightProfilesMap.CreateIterator(); It; ++It)
		{
			IESProfilesArray[It->Value] = It->Key;
		}

		View.IESLightProfileResource->BuildIESLightProfilesTexture(IESProfilesArray);
	}
}

TUniformBufferRef<FRaytracingLightDataPacked> CreateLightDataPackedUniformBuffer(
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const class FViewInfo& View, EUniformBufferUsage Usage,
	FStructuredBufferRHIRef& OutLightDataBuffer,
	FShaderResourceViewRHIRef& OutLightDataSRV)
{
	FRaytracingLightDataPacked LightData;
	TResourceArray<FRTLightingData> LightDataArray;

	SetupRaytracingLightDataPacked(Lights, View, &LightData, LightDataArray);

	check(LightData.Count == LightDataArray.Num());

	// need at least one element
	if (LightDataArray.Num() == 0)
	{
		LightDataArray.AddZeroed(1);
	}

	// This buffer might be best placed as an element of the LightData uniform buffer
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.ResourceArray = &LightDataArray;

	OutLightDataBuffer = RHICreateStructuredBuffer(sizeof(FRTLightingData), LightDataArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
	OutLightDataSRV = RHICreateShaderResourceView(OutLightDataBuffer);

	LightData.LightDataBuffer = OutLightDataSRV;
	LightData.SSProfilesTexture = View.RayTracingSubSurfaceProfileSRV;

	return CreateUniformBufferImmediate(LightData, Usage);
}

class FRayTracingLightingMS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingLightingMS);
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingLightingMS, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform)
			&& FDataDrivenShaderPlatformInfo::GetInfo(Parameters.Platform).bSupportsRayTracingMissShaderBindings;
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingLightingMS, "/Engine/Private/RayTracing/RayTracingLightingMS.usf", "RayTracingLightingMS", SF_RayMiss);

FRHIRayTracingShader* FDeferredShadingSceneRenderer::GetRayTracingLightingMissShader(FViewInfo& View)
{
	return View.ShaderMap->GetShader<FRayTracingLightingMS>()->GetRayTracingShader();
}

template< typename ShaderClass>
static int32 BindParameters(ShaderClass* Shader, typename ShaderClass::FParameters & Parameters, int32 MaxParams, const FRHIUniformBuffer **OutUniformBuffers)
{
	FRayTracingShaderBindingsWriter ResourceBinder;

	auto &ParameterMap = Shader->GetParameterMapInfo();

	// all parameters should be in uniform buffers
	check(ParameterMap.LooseParameterBuffers.Num() == 0);
	check(ParameterMap.SRVs.Num() == 0);
	check(ParameterMap.TextureSamplers.Num() == 0);

	SetShaderParameters(ResourceBinder, Shader, Parameters);

	FMemory::Memzero(OutUniformBuffers, sizeof(FRHIUniformBuffer *)*MaxParams);

	const int32 NumUniformBuffers = ParameterMap.UniformBuffers.Num();

	int32 MaxUniformBufferUsed = -1;
	for (int32 UniformBufferIndex = 0; UniformBufferIndex < NumUniformBuffers; UniformBufferIndex++)
	{
		const FShaderParameterInfo &Parameter = ParameterMap.UniformBuffers[UniformBufferIndex];
		checkSlow(Parameter.BaseIndex < MaxParams);
		const FRHIUniformBuffer* UniformBuffer = ResourceBinder.UniformBuffers[UniformBufferIndex];
		if (Parameter.BaseIndex < MaxParams)
		{
			OutUniformBuffers[Parameter.BaseIndex] = UniformBuffer;
			MaxUniformBufferUsed = FMath::Max((int32)Parameter.BaseIndex, MaxUniformBufferUsed);
		}
	}

	return MaxUniformBufferUsed + 1;
}

void FDeferredShadingSceneRenderer::SetupRayTracingLightingMissShader(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FRayTracingLightingMS::FParameters MissParameters;
	MissParameters.LightDataPacked = View.RayTracingLightingDataUniformBuffer;
	MissParameters.ViewUniformBuffer = View.ViewUniformBuffer;

	static constexpr uint32 MaxUniformBuffers = UE_ARRAY_COUNT(FRayTracingShaderBindings::UniformBuffers);
	const FRHIUniformBuffer* MissData[MaxUniformBuffers] = {};
	auto MissShader = View.ShaderMap->GetShader<FRayTracingLightingMS>();

	int32 ParameterSlots = BindParameters(MissShader, MissParameters, MaxUniformBuffers, MissData);

	RHICmdList.SetRayTracingMissShader(View.RayTracingScene.RayTracingSceneRHI,
		RAY_TRACING_MISS_SHADER_SLOT_LIGHTING, // Shader slot in the scene
		View.RayTracingMaterialPipeline,
		RAY_TRACING_MISS_SHADER_SLOT_LIGHTING, // Miss shader index in the pipeline
		ParameterSlots, (FRHIUniformBuffer**)MissData, 0);
}

#endif // RHI_RAYTRACING
