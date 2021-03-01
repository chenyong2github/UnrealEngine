// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneDirectShadowingHardwareRayTracing.cpp
=============================================================================*/

#include "RHIDefinitions.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"

#if RHI_RAYTRACING
#include "SceneRendering.h"
#include "RayGenShaderUtils.h"
#include "RayTracing/RaytracingOptions.h"
#include "BuiltInRayTracingShaders.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"

static TAutoConsoleVariable<int32> CVarLumenDirectLightingHardwareRaytracing(
	TEXT("r.Lumen.DirectLighting.HardwareRayTracing"),
	0,
	TEXT("Enable RTX for direct lighting (Default = 0)"),
	ECVF_RenderThreadSafe
);

int32 GHardwareRaytracingEnableTwoSidedGeometry = 1;
FAutoConsoleVariableRef CVarHardwareRayTracingTwoSidedGeometryEnabled(
	TEXT("r.Lumen.DirectLighting.HardwareRayTracing.EnableTwoSidedGeometry"),
	GHardwareRaytracingEnableTwoSidedGeometry,
	TEXT("Enables two-sided geometry when tracing shadow rays (default = 1)"),
	ECVF_RenderThreadSafe
);

float GHardwareRayTracingShadowingSurfaceBias = 1;
FAutoConsoleVariableRef CVarHardwareRayTracingShadowingSurfaceBias(
	TEXT("r.Lumen.DirectLighting.HardwareRayTracing.ShadowingSurfaceBias"),
	GHardwareRayTracingShadowingSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GHardwareRayTracingShadowingSlopeScaledSurfaceBias = 1;
FAutoConsoleVariableRef CVarHardwarRayTracingShadowingSlopeScaledSurfaceBias(
	TEXT("r.Lumen.DirectLighting.HardwareRayTracing.ShadowingSlopeScaledSurfaceBias"),
	GHardwareRayTracingShadowingSlopeScaledSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
);

bool GetHardwareRayTracingShadowsEnableMaterials()
{
	static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows.EnableMaterials"));
	return CVar ? (CVar->GetInt() != 0) : false;
}

float GetHardwareRayTracingShadowingSurfaceBias()
{
	static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.DirectLighting.HardwareRayTracing.ShadowingSurfaceBias"));
	return CVar ? CVar->GetFloat() : 1.0f;
}

float GetHardwareRayTracingShadowingSlopeScaledSurfaceBias()
{
	static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.DirectLighting.HardwareRayTracing.ShadowingSlopeScaledSurfaceBias"));
	return CVar ? CVar->GetFloat() : 1.0f;
}

float GetHardwareRaytracingMaxNormalBias()
{
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.NormalBias"));
	return FMath::Max(0.01f, CVar ? CVar->GetFloat() : 0.1f);// Default 0.1f used in RayTracingShadows.cpp
}
#endif

namespace Lumen
{
	bool UseHardwareRayTracedShadows(const FViewInfo& View)
	{
		bool bUseHardwareRayTracedShadows = false;

#if RHI_RAYTRACING
		static IConsoleVariable* CVarRayTracingShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows"));
		const bool bRayTracingShadows = CVarRayTracingShadows ? (CVarRayTracingShadows->GetInt() != 0) : false;
		
		if (bRayTracingShadows)
		{
			// Force to use raytracing shadows if r.RayTracing.Shadows = 1
			bUseHardwareRayTracedShadows = IsRayTracingEnabled();
		}
		else
		{   // When hardware raytracing shadows is turned off, our direct lighting pass can still use hardware raytraced shadow 
			// if Lumen direct lighting pass has turned on hardware raytracing. In this configuration, we could use CSM for other
			// rendering passes.
			bUseHardwareRayTracedShadows = IsRayTracingEnabled()
				&& Lumen::UseHardwareRayTracing()
				&& CVarLumenDirectLightingHardwareRaytracing.GetValueOnRenderThread() != 0;
		}

		// Turn raytracing off if no rendering feature is enabled.
		if (!View.RayTracingScene.RayTracingSceneRHI)
		{
			bUseHardwareRayTracedShadows = false;
		}
#endif
		return bUseHardwareRayTracedShadows;
	}
}

FLumenDirectLightingHardwareRayTracingData::FLumenDirectLightingHardwareRayTracingData()
{
	LightId = 0;
	bSouldClearLightMask = true;
	bIsInterpolantsTextureCreated = false;
}

void FLumenDirectLightingHardwareRayTracingData::BeginLumenDirectLightingUpdate()
{
	int NewLightUniqueId = (LightId + 1) % 255;
	if (NewLightUniqueId < LightId)
	{
		bSouldClearLightMask = true;
	}
	LightId = NewLightUniqueId;
}

void FLumenDirectLightingHardwareRayTracingData::EndLumenDirectLightingUpdate()
{
	bSouldClearLightMask = false;
	bIsInterpolantsTextureCreated = true;
}

int FLumenDirectLightingHardwareRayTracingData::GetLightId()
{
	return 1 + LightId;
}

bool FLumenDirectLightingHardwareRayTracingData::ShouldClearLightMask()
{
	return bSouldClearLightMask;
}

bool FLumenDirectLightingHardwareRayTracingData::IsInterpolantsTextureCreated()
{
	return bIsInterpolantsTextureCreated;
}

void FLumenDirectLightingHardwareRayTracingData::Initialize(FRDGBuilder& GraphBuilder, const FScene* Scene)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FIntPoint MaxAtlasSize = LumenSceneData.MaxAtlasSize;
	auto AtlasElementCount = MaxAtlasSize.X * MaxAtlasSize.Y;

	const FRDGTextureDesc LightMaskTextureDescriptor = FRDGTextureDesc::Create2D(
		MaxAtlasSize,
		PF_R8_UINT,
		FClearValueBinding(),
		TexCreate_ShaderResource | TexCreate_UAV
	);

	const FRDGTextureDesc CardInterpolantsTextureDescriptor = FRDGTextureDesc::Create2D(
		MaxAtlasSize,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV
	);

	const FRDGTextureDesc ShadowMaskAtlasTextureDescriptor = FRDGTextureDesc::Create2D(
		MaxAtlasSize,
		PF_R16F,
		FClearValueBinding(),
		TexCreate_ShaderResource | TexCreate_UAV
	);

	LightMaskTexture = GraphBuilder.CreateTexture(LightMaskTextureDescriptor, TEXT("LightMaskTexture"));

	//Combined to record the FCardVS2PS information.
	CardInterpolantsTexture = GraphBuilder.CreateTexture(CardInterpolantsTextureDescriptor, TEXT("CardIndexer1"));
	CardInterpolantsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, AtlasElementCount),
		TEXT("CardIndexer2"));

	ShadowMaskAtlas = GraphBuilder.CreateTexture(ShadowMaskAtlasTextureDescriptor, TEXT("ShadowMaskAtlas"));
}

#if RHI_RAYTRACING

struct FCardIndexer
{
	FVector2D AtlasCoord;
	FVector2D CardUV;
	uint32   CardId;
	uint32   QuadIndex;
};

class FClearUAVTextureUintCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearUAVTextureUintCS)
	SHADER_USE_PARAMETER_STRUCT(FClearUAVTextureUintCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWUintTexture)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Height)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LUMEN_COMPUTE"), 1);
	}

	static FIntPoint GetGroupSize()
	{
		return FIntPoint(8, 8);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearUAVTextureUintCS, "/Engine/Private/Lumen/LumenSceneDirectShadowingHardwareRaytracing.usf", "ClearUAVTextureUintCS", SF_Compute);

void ClearUAVUintTexture(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef Texture, FIntPoint TextureSize)
{
	typedef FClearUAVTextureUintCS SHADER;
	SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
	PassParameters->RWUintTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Texture));
	PassParameters->Width = TextureSize.X;
	PassParameters->Height = TextureSize.Y;
	TShaderMapRef<SHADER> ComputeShader(View.ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ClearUAV"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(TextureSize, SHADER::GetGroupSize())
	);
}

/*setup pass to redirect card mapping to raytracing*/
class FLumenCardRayGenSetupPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardRayGenSetupPS)
	SHADER_USE_PARAMETER_STRUCT(FLumenCardRayGenSetupPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FVector4, AtlasSizeAndInvSize)
		SHADER_PARAMETER(int, LightUniqueId)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FCardIndexer>, CardInterpolantsUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWLightMask)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

};

IMPLEMENT_GLOBAL_SHADER(FLumenCardRayGenSetupPS, "/Engine/Private/Lumen/LumenSceneDirectShadowingHardwareRaytracing.usf", "LumenCardRayGenSetupPS", SF_Pixel);

class FLumenCardDirectLightingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardDirectLightingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenCardDirectLightingRGS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_CLOSEST_HIT_SHADER"), 0);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_ANY_HIT_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_MISS_SHADER"), 0);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FLightType>() == ELumenLightType::Directional &&
			PermutationVector.Get<FLightType>() == static_cast<ELumenLightType>(0))
		{
			OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_COHERENT_RAYS"), 1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_COHERENT_RAYS"), 0);
		}

		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_RAYTRACING"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)
		SHADER_PARAMETER(FVector4, AtlasSizeAndInvSize)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CardInterpolantsUAV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CardInterpolantsTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LightMask)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT(FLightShaderParameters, Light)
		SHADER_PARAMETER(float, SamplesPerPixel)
		SHADER_PARAMETER(float, NormalBias)
		SHADER_PARAMETER(int, LightUniqueId)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWShadowMaskAtlas)
		END_SHADER_PARAMETER_STRUCT()

	class FShadowed : SHADER_PERMUTATION_BOOL("SHADOWED_LIGHT");
	class FEnableTwoSidedGeometry : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FLightType : SHADER_PERMUTATION_ENUM_CLASS("LIGHT_TYPE", ELumenLightType);
	class FEnableMultipleSamplesPerPixel : SHADER_PERMUTATION_BOOL("ENABLE_MULTIPLE_SAMPLES_PER_PIXEL");

	using FPermutationDomain = TShaderPermutationDomain<FLightType, FEnableTwoSidedGeometry,FShadowed, FEnableMultipleSamplesPerPixel>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardDirectLightingRGS, "/Engine/Private/Lumen/LumenSceneDirectShadowingHardwareRaytracing.usf", "LumenCardDirectLightingRGS", SF_RayGen);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardDirectLightingRaySetup, )
SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardRayGenSetupPS::FParameters, PS)
RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::PrepareRayTracingLumenDirectLighting(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	int32 LightType_MAX = static_cast<int32>(ELumenLightType::MAX);
	typedef FLumenCardDirectLightingRGS FRGShader;

	for (int32 MultiSPP = 0; MultiSPP < 2; ++MultiSPP)
	{
		for (int32 Shadowed = 0; Shadowed < 2; ++Shadowed)
		{
			for (int32 EnableTwoSidedGeometry = 0; EnableTwoSidedGeometry < 2; ++EnableTwoSidedGeometry)
			{
				for (int32 LightType = 0; LightType < LightType_MAX; ++LightType)
				{
					FRGShader::FPermutationDomain PermutationVector;
					PermutationVector.Set<FRGShader::FShadowed>(Shadowed == 1);
					PermutationVector.Set<FRGShader::FEnableTwoSidedGeometry>(EnableTwoSidedGeometry == 1);
					PermutationVector.Set<FRGShader::FLightType>(static_cast<ELumenLightType>(LightType));
					PermutationVector.Set<FRGShader::FEnableMultipleSamplesPerPixel>(MultiSPP != 0);

					auto RayGenerationShader = View.ShaderMap->GetShader<FRGShader>(PermutationVector);
					OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
				}
			}
		}
	}
}

#endif

void RenderHardwareRayTracedShadowIntoLumenCards(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FRDGTextureRef OpacityAtlas,
	const FLightSceneInfo* LightSceneInfo,
	const FString& LightName,
	const FLumenCardScatterContext& CardScatterContext,
	int32 ScatterInstanceIndex,
	FLumenDirectLightingHardwareRayTracingData& LumenDirectLightingHardwareRayTracingData,
	bool bDynamicallyShadowed,
	ELumenLightType LumenLightType)
#if RHI_RAYTRACING
{
	const bool bShadowed = LightSceneInfo->Proxy->CastsDynamicShadow();
	const bool bLumenUseHardwareRayTracedShadow = Lumen::UseHardwareRayTracedShadows(View);
	check(bShadowed);
	check(bLumenUseHardwareRayTracedShadow);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
	const FIntPoint MaxAtlasSize = LumenSceneData.MaxAtlasSize;
	const FVector4 AtlasSizeAndInvSize = FVector4(MaxAtlasSize.X, MaxAtlasSize.Y, 1.0f / MaxAtlasSize.X, 1.0f / MaxAtlasSize.Y);
	ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

	// Set a different light id to minimize ray tracing calls.
	LumenDirectLightingHardwareRayTracingData.BeginLumenDirectLightingUpdate();

	/*Pass Zero: Clear the light mask to zero for every 255 light draw calls.*/
	const bool bShouldClearLightMaskTexture = LumenDirectLightingHardwareRayTracingData.ShouldClearLightMask();

	if (bShouldClearLightMaskTexture)
	{
		ClearUAVUintTexture(GraphBuilder, View, LumenDirectLightingHardwareRayTracingData.LightMaskTexture, MaxAtlasSize);
	}

	/*Pass One: Fetch the CardId, QuadIndex, CardUV, and AtlasCoord from rasterizer for ray tracing*/
	{
		auto AtlasElementCount = MaxAtlasSize.X * MaxAtlasSize.Y;
		FLumenCardDirectLightingRaySetup* SetupPassParameters = GraphBuilder.AllocParameters<FLumenCardDirectLightingRaySetup>();

		ERenderTargetLoadAction Action = LumenDirectLightingHardwareRayTracingData.IsInterpolantsTextureCreated() ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction;
		SetupPassParameters->RenderTargets[0] = FRenderTargetBinding(LumenDirectLightingHardwareRayTracingData.CardInterpolantsTexture, Action);
		SetupPassParameters->VS.InfluenceSphere = FVector4(LightBounds.Center, LightBounds.W);
		SetupPassParameters->VS.LumenCardScene = LumenCardSceneUniformBuffer;
		SetupPassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		SetupPassParameters->VS.ScatterInstanceIndex = ScatterInstanceIndex;
		SetupPassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;

		SetupPassParameters->PS.AtlasSizeAndInvSize = FVector4(MaxAtlasSize.X, MaxAtlasSize.Y, 1.0f / MaxAtlasSize.X, 1.0f / MaxAtlasSize.Y);
		SetupPassParameters->PS.CardInterpolantsUAV = GraphBuilder.CreateUAV(LumenDirectLightingHardwareRayTracingData.CardInterpolantsBuffer, PF_R32_UINT);
		SetupPassParameters->PS.OpacityAtlas = OpacityAtlas;
		SetupPassParameters->PS.RWLightMask = GraphBuilder.CreateUAV(LumenDirectLightingHardwareRayTracingData.LightMaskTexture);
		SetupPassParameters->PS.ViewUniformBuffer = View.ViewUniformBuffer;
		SetupPassParameters->PS.LumenCardScene = LumenCardSceneUniformBuffer;
		SetupPassParameters->PS.LightUniqueId = LumenDirectLightingHardwareRayTracingData.GetLightId();

		FRasterizeToCardsVS::FPermutationDomain VSPermutationVector;
		VSPermutationVector.Set< FRasterizeToCardsVS::FClampToInfluenceSphere >(LumenLightType != ELumenLightType::Directional);
		auto VertexShader = View.ShaderMap->GetShader<FRasterizeToCardsVS>(VSPermutationVector);
		auto PixelShader = View.ShaderMap->GetShader<FLumenCardRayGenSetupPS>();

		const uint32 CardIndirectArgOffset = CardScatterContext.GetIndirectArgOffset(ScatterInstanceIndex);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RayGenSetup"),
			SetupPassParameters,
			ERDGPassFlags::Raster,
			[MaxAtlasSize, SetupPassParameters, VertexShader, PixelShader, GlobalShaderMap = View.ShaderMap, &View, CardIndirectArgOffset](FRHICommandListImmediate& RHICmdList)
		{
			DrawQuadsToAtlas(
				MaxAtlasSize,
				VertexShader,
				PixelShader,
				SetupPassParameters,
				GlobalShaderMap,
				TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero>::GetRHI(), // we replace the render target results
				RHICmdList,
				[](FRHICommandListImmediate& RHICmdList, TShaderRefBase<FLumenCardRayGenSetupPS, FShaderMapPointerTable> Shader, FRHIPixelShader* ShaderRHI, const FLumenCardRayGenSetupPS::FParameters& Parameters)
				{},
				CardIndirectArgOffset);
		});
	} // End Pass One.

	/*Pass Two: Fill the shadow mask atlas texture*/
	FLightSceneProxy* LightSceneProxy = LightSceneInfo->Proxy;
	check(LightSceneProxy);
	int32 SamplesPerPixel = LightSceneProxy->GetSamplesPerPixel();

	FLumenCardDirectLightingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardDirectLightingRGS::FParameters>();
	{
		{
			FDeferredLightUniformStruct DeferredLightUniforms = GetDeferredLightParameters(View, *LightSceneInfo);

			if (LightSceneInfo->Proxy->IsInverseSquared())
			{
				DeferredLightUniforms.LightParameters.FalloffExponent = 0;
			}
			DeferredLightUniforms.LightParameters.Color *= LightSceneInfo->Proxy->GetIndirectLightingScale();
			PassParameters->DeferredLightUniforms = CreateUniformBufferImmediate(DeferredLightUniforms, UniformBuffer_SingleDraw);
		}

		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->SurfaceBias = FMath::Clamp(GetHardwareRayTracingShadowingSurfaceBias(), .01f, 100.0f);// alternative for normal bias
		PassParameters->SlopeScaledSurfaceBias = FMath::Clamp(GetHardwareRayTracingShadowingSlopeScaledSurfaceBias(), .01f, 100.0f);
		PassParameters->SamplesPerPixel = SamplesPerPixel;
		PassParameters->NormalBias = GetHardwareRaytracingMaxNormalBias();

		LightSceneProxy->GetLightShaderParameters(PassParameters->Light);
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();

		PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;

		PassParameters->RWShadowMaskAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(LumenDirectLightingHardwareRayTracingData.ShadowMaskAtlas));
		PassParameters->CardInterpolantsUAV = GraphBuilder.CreateUAV(LumenDirectLightingHardwareRayTracingData.CardInterpolantsBuffer, PF_R32_UINT);
		PassParameters->CardInterpolantsTexture = LumenDirectLightingHardwareRayTracingData.CardInterpolantsTexture;
		PassParameters->LightMask = LumenDirectLightingHardwareRayTracingData.LightMaskTexture;
		PassParameters->AtlasSizeAndInvSize = AtlasSizeAndInvSize;
		PassParameters->LightUniqueId = LumenDirectLightingHardwareRayTracingData.GetLightId();
		PassParameters->MaxTraceDistance = Lumen::GetMaxTraceDistance();
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
	}

	typedef FLumenCardDirectLightingRGS SHADER;
	SHADER::FPermutationDomain PermutationVector;
	PermutationVector.Set< SHADER::FLightType >(LumenLightType);
	PermutationVector.Set< SHADER::FShadowed >(bShadowed);
	PermutationVector.Set< SHADER::FEnableTwoSidedGeometry>(GHardwareRaytracingEnableTwoSidedGeometry == 1);
	PermutationVector.Set< SHADER::FEnableMultipleSamplesPerPixel>(SamplesPerPixel > 1);
	PermutationVector = SHADER::RemapPermutation(PermutationVector);

	TShaderMapRef<SHADER> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);

	bool bRayTracingShadowsEnableMateris = GetHardwareRayTracingShadowsEnableMaterials();

	ClearUnusedGraphResources(RayGenerationShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s %s (RTX, ssp=%d) %dx%d", *LightName, TEXT("Shadow pass"), SamplesPerPixel, MaxAtlasSize.X, MaxAtlasSize.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[&View, RayGenerationShader, PassParameters, MaxAtlasSize, bRayTracingShadowsEnableMateris](FRHICommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

			if (bRayTracingShadowsEnableMateris)
			{
				RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader
					.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, MaxAtlasSize.X, MaxAtlasSize.Y);
			}
			else
			{
				FRayTracingPipelineStateInitializer Initializer;

				Initializer.MaxPayloadSizeInBytes = 60; // sizeof(FPackedMaterialClosestHitPayload)

				FRHIRayTracingShader* RayGenShaderTable[] = { RayGenerationShader.GetRayTracingShader() };
				Initializer.SetRayGenShaderTable(RayGenShaderTable);

				FRHIRayTracingShader* HitGroupTable[] = { View.ShaderMap->GetShader<FOpaqueShadowHitGroup>().GetRayTracingShader() };
				Initializer.SetHitGroupTable(HitGroupTable);
				Initializer.bAllowHitGroupIndexing = false; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

				FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);

				RHICmdList.RayTraceDispatch(Pipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, MaxAtlasSize.X, MaxAtlasSize.Y);
			}
		}
	);// End Pass Two (ray traced shadow mask calculation)

	LumenDirectLightingHardwareRayTracingData.EndLumenDirectLightingUpdate();
}
#else // !RHI_RAYTRACING
{
	unimplemented();
}
#endif

