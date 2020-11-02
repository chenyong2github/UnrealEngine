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

static TAutoConsoleVariable<int32> CVarRayTracingLightingCells(
	TEXT("r.RayTracing.LightCulling.Cells"),
	16,
	TEXT("Number of cells in each dimension for lighting grid (default 16)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarRayTracingLightingCellSize(
	TEXT("r.RayTracing.LightCulling.CellSize"),
	200.0f,
	TEXT("Minimum size of light cell (default 200 units)"),
	ECVF_RenderThreadSafe
);

bool CanUseRayTracingLightingMissShader(EShaderPlatform ShaderPlatform)
{
	return CVarRayTracingLightingMissShader.GetValueOnRenderThread() != 0;
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRaytracingLightDataPacked, "RaytracingLightsDataPacked");


class FSetupRayTracingLightCullData : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupRayTracingLightCullData);
	SHADER_USE_PARAMETER_STRUCT(FSetupRayTracingLightCullData, FGlobalShader)

		static int32 GetGroupSize()
	{
		return 32;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, RankedLights)
		SHADER_PARAMETER(FVector, WorldPos)
		SHADER_PARAMETER(uint32, NumLightsToUse)
		SHADER_PARAMETER(uint32, CellCount)
		SHADER_PARAMETER(float, CellScale)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, LightIndices)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint4>, LightCullingVolume)
		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSetupRayTracingLightCullData, "/Engine/Private/RayTracing/GenerateCulledLightListCS.usf", "GenerateCulledLightListCS", SF_Compute);
DECLARE_GPU_STAT_NAMED(LightCullingVolumeCompute, TEXT("RT Light Culling Volume Compute"));


static void SelectRaytracingLights(
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const FViewInfo& View,
	TArray<int32>& OutSelectedLights
)
{
	OutSelectedLights.Empty();

	for (auto Light : Lights)
	{
		const bool bHasStaticLighting = Light.LightSceneInfo->Proxy->HasStaticLighting() && Light.LightSceneInfo->IsPrecomputedLightingValid();
		const bool bAffectReflection = Light.LightSceneInfo->Proxy->AffectReflection();
		if (bHasStaticLighting || !bAffectReflection) continue;

		OutSelectedLights.Add(Light.LightSceneInfo->Id);

		if (OutSelectedLights.Num() >= RAY_TRACING_LIGHT_COUNT_MAXIMUM) break;
	}
}

static int32 GetCellsPerDim()
{
	// round to next even as the structure relies on symmetry for address computations
	return (FMath::Max(2, CVarRayTracingLightingCells.GetValueOnRenderThread()) + 1) & (~1); 
}

static void CreateRaytracingLightCullingStructure(
	FRHICommandListImmediate& RHICmdList,
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const FViewInfo& View,
	const TArray<int32>& LightIndices,
	FRayTracingLightData& OutLightingData)
{
	const int32 NumLightsToUse = LightIndices.Num();

	struct FCullRecord
	{
		uint32 NumLights;
		uint32 Offset;
		uint32 Unused1;
		uint32 Unused2;
	};

	const int32 CellsPerDim = GetCellsPerDim();

	TResourceArray<VectorRegister> RankedLights;
	RankedLights.Reserve(NumLightsToUse);

	// setup light vector array sorted by rank
	for (int32 LightIndex = 0; LightIndex < NumLightsToUse; LightIndex++)
	{
		RankedLights.Push(Lights[LightIndices[LightIndex]].BoundingSphereVector);
	}

	// push null vector to prevent failure in RHICreateStructuredBuffer due to requesting a zero sized allocation
	if (RankedLights.Num() == 0)
	{
		RankedLights.Push(VectorRegister{});
	}

	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.ResourceArray = &RankedLights;

	FStructuredBufferRHIRef RayTracingCullLights = RHICreateStructuredBuffer(sizeof(RankedLights[0]),
		RankedLights.GetResourceDataSize(),
		BUF_Static | BUF_ShaderResource,
		CreateInfo);
	FShaderResourceViewRHIRef RankedLightsSRV = RHICreateShaderResourceView(RayTracingCullLights);

	// Structured buffer version
	FRHIResourceCreateInfo CullStructureCreateInfo;
	CullStructureCreateInfo.DebugName = TEXT("RayTracingLightCullVolume");
	OutLightingData.LightCullVolume = RHICreateStructuredBuffer(sizeof(FUintVector4), CellsPerDim*CellsPerDim*CellsPerDim* sizeof(FUintVector4), BUF_UnorderedAccess | BUF_ShaderResource, CullStructureCreateInfo);
	OutLightingData.LightCullVolumeSRV = RHICmdList.CreateShaderResourceView(OutLightingData.LightCullVolume);
	FUnorderedAccessViewRHIRef LightCullVolumeUAV = RHICmdList.CreateUnorderedAccessView(OutLightingData.LightCullVolume, false, false);

	// ensure zero sized texture isn't requested  to prevent failure in Initialize
	OutLightingData.LightIndices.Initialize(
		sizeof(uint16),
		FMath::Max(NumLightsToUse, 1) * CellsPerDim * CellsPerDim * CellsPerDim,
		EPixelFormat::PF_R16_UINT,
		BUF_UnorderedAccess,
		TEXT("RayTracingLightIndices"));

	{
		auto* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FSetupRayTracingLightCullData> Shader(GlobalShaderMap);

		{
			FSetupRayTracingLightCullData::FParameters Params;
			Params.RankedLights = RankedLightsSRV;

			Params.WorldPos = View.ViewMatrices.GetViewOrigin(); // View.ViewLocation;
			Params.NumLightsToUse = NumLightsToUse;
			Params.LightCullingVolume = LightCullVolumeUAV;
			Params.LightIndices = OutLightingData.LightIndices.UAV;
			Params.CellCount = CellsPerDim;
			Params.CellScale = CVarRayTracingLightingCellSize.GetValueOnRenderThread() / 2.0f; // cells are based on pow2, and initial cell is 2^1, so scale is half min cell size
			{
				SCOPED_GPU_STAT(RHICmdList, LightCullingVolumeCompute);
				FComputeShaderUtils::Dispatch(RHICmdList, Shader, Params, FIntVector(CellsPerDim, CellsPerDim, CellsPerDim));
			}
		}
	}

	{
		FRHITransitionInfo Transitions[] =
		{
			FRHITransitionInfo(LightCullVolumeUAV.GetReference(), ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			FRHITransitionInfo(OutLightingData.LightIndices.UAV.GetReference(), ERHIAccess::UAVCompute, ERHIAccess::SRVMask)
		};
		RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));
	}

}


static void SetupRaytracingLightDataPacked(
	FRHICommandListImmediate& RHICmdList,
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const TArray<int32>& LightIndices,
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

	for (auto LightIndex : LightIndices)
	{
		auto Light = Lights[LightIndex];
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

		// Ray tracing should compute fade parameters ignoring lightmaps
		const FVector2D FadeParams = Light.LightSceneInfo->Proxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), false, View.MaxShadowCascades);
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

		// Stuff directional light's shadow angle factor into a RectLight parameter
		if (Light.LightType == LightType_Directional)
		{
			LightDataElement.RectLightBarnCosAngle = Light.LightSceneInfo->Proxy->GetShadowSourceAngleFactor();
		}

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

		View.IESLightProfileResource->BuildIESLightProfilesTexture(RHICmdList, IESProfilesArray);
	}
}


FRayTracingLightData CreateRayTracingLightData(
	FRHICommandListImmediate& RHICmdList,
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const FViewInfo& View, EUniformBufferUsage Usage)
{
	FRayTracingLightData LightingData;
	FRaytracingLightDataPacked LightData;
	TResourceArray<FRTLightingData> LightDataArray;
	TArray<int32> LightIndices;

	SelectRaytracingLights(Lights, View, LightIndices);

	// Create light culling volume
	CreateRaytracingLightCullingStructure(RHICmdList, Lights, View, LightIndices, LightingData);
	LightData.CellCount = GetCellsPerDim();
	LightData.CellScale = CVarRayTracingLightingCellSize.GetValueOnRenderThread() / 2.0f;
	LightData.LightCullingVolume = LightingData.LightCullVolumeSRV;
	LightData.LightIndices = LightingData.LightIndices.SRV;

	SetupRaytracingLightDataPacked(RHICmdList, Lights, LightIndices, View, &LightData, LightDataArray);

	check(LightData.Count == LightDataArray.Num());

	// need at least one element
	if (LightDataArray.Num() == 0)
	{
		LightDataArray.AddZeroed(1);
	}

	// This buffer might be best placed as an element of the LightData uniform buffer
	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.ResourceArray = &LightDataArray;

	LightingData.LightBuffer = RHICreateStructuredBuffer(sizeof(FUintVector4), LightDataArray.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
	LightingData.LightBufferSRV = RHICreateShaderResourceView(LightingData.LightBuffer);

	LightData.LightDataBuffer = LightingData.LightBufferSRV;
	LightData.SSProfilesTexture = View.RayTracingSubSurfaceProfileSRV;

	LightingData.UniformBuffer = CreateUniformBufferImmediate(LightData, Usage);

	return LightingData;
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
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingLightingMS, "/Engine/Private/RayTracing/RayTracingLightingMS.usf", "RayTracingLightingMS", SF_RayMiss);

FRHIRayTracingShader* FDeferredShadingSceneRenderer::GetRayTracingLightingMissShader(FViewInfo& View)
{
	return View.ShaderMap->GetShader<FRayTracingLightingMS>().GetRayTracingShader();
}

template< typename ShaderClass>
static int32 BindParameters(const TShaderRef<ShaderClass>& Shader, typename ShaderClass::FParameters & Parameters, int32 MaxParams, const FRHIUniformBuffer **OutUniformBuffers)
{
	FRayTracingShaderBindingsWriter ResourceBinder;

	auto &ParameterMap = Shader->ParameterMapInfo;

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
	MissParameters.LightDataPacked = View.RayTracingLightData.UniformBuffer;
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
