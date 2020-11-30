// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenRadianceCache.cpp
=============================================================================*/

#include "LumenRadianceCache.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "LumenSceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "LumenScreenProbeGather.h"

int32 GLumenRadianceCache = 1;
FAutoConsoleVariableRef CVarRadianceCache(
	TEXT("r.Lumen.RadianceCache"),
	GLumenRadianceCache,
	TEXT("Whether to enable the Persistent world space Radiance Cache"),
	ECVF_RenderThreadSafe
	);

int32 GRadianceCacheUpdate = 1;
FAutoConsoleVariableRef CVarRadianceCacheUpdate(
	TEXT("r.Lumen.RadianceCache.Update"),
	GRadianceCacheUpdate,
	TEXT("Whether to update radiance cache every frame"),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheNumClipmaps = 4;
FAutoConsoleVariableRef CVarRadianceCacheNumClipmaps(
	TEXT("r.Lumen.RadianceCache.NumClipmaps"),
	GRadianceCacheNumClipmaps,
	TEXT("Number of radiance cache clipmaps."),
	ECVF_RenderThreadSafe
);

float GLumenRadianceCacheClipmapWorldExtent = 5000.0f;
FAutoConsoleVariableRef CVarLumenRadianceCacheClipmapWorldExtent(
	TEXT("r.Lumen.RadianceCache.ClipmapWorldExtent"),
	GLumenRadianceCacheClipmapWorldExtent,
	TEXT("World space extent of the first clipmap"),
	ECVF_RenderThreadSafe
);

float GLumenRadianceCacheClipmapDistributionBase = 2.0f;
FAutoConsoleVariableRef CVarLumenRadianceCacheClipmapDistributionBase(
	TEXT("r.Lumen.RadianceCache.ClipmapDistributionBase"),
	GLumenRadianceCacheClipmapDistributionBase,
	TEXT("Base of the Pow() that controls the size of each successive clipmap relative to the first."),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheGridResolution = 64;
FAutoConsoleVariableRef CVarRadianceCacheResolution(
	TEXT("r.Lumen.RadianceCache.GridResolution"),
	GRadianceCacheGridResolution,
	TEXT("Resolution of the probe placement grid within each clipmap"),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheProbeResolution = 32;
FAutoConsoleVariableRef CVarRadianceCacheProbeResolution(
	TEXT("r.Lumen.RadianceCache.ProbeResolution"),
	GRadianceCacheProbeResolution,
	TEXT("Resolution of the probe's 2d radiance layout.  The number of rays traced for the probe will be ProbeResolution ^ 2"),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheNumMipmaps = 1;
FAutoConsoleVariableRef CVarRadianceCacheNumMipmaps(
	TEXT("r.Lumen.RadianceCache.NumMipmaps"),
	GRadianceCacheNumMipmaps,
	TEXT("Number of radiance cache mipmaps."),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheProbeAtlasResolutionInProbes = 128;
FAutoConsoleVariableRef CVarRadianceCacheProbeAtlasResolutionInProbes(
	TEXT("r.Lumen.RadianceCache.ProbeAtlasResolutionInProbes"),
	GRadianceCacheProbeAtlasResolutionInProbes,
	TEXT("Number of probes along one dimension of the probe atlas cache texture.  This controls the memory usage of the cache.  Overflow currently results in incorrect rendering."),
	ECVF_RenderThreadSafe
);

float GRadianceCacheProbeRadiusScale = 1;
FAutoConsoleVariableRef CVarRadianceCacheProbeRadiusScale(
	TEXT("r.Lumen.RadianceCache.ProbeRadiusScale"),
	GRadianceCacheProbeRadiusScale,
	TEXT("Larger probes decrease parallax error, but cache less lighting."),
	ECVF_RenderThreadSafe
);

float GRadianceCacheReprojectionRadiusScale = 1.5f;
FAutoConsoleVariableRef CVarRadianceCacheProbeReprojectionRadiusScale(
	TEXT("r.Lumen.RadianceCache.ReprojectionRadiusScale"),
	GRadianceCacheReprojectionRadiusScale,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheForceFullUpdate = 0;
FAutoConsoleVariableRef CVarRadianceForceFullUpdate(
	TEXT("r.Lumen.RadianceCache.ForceFullUpdate"),
	GRadianceCacheForceFullUpdate,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheProbesUpdateEveryNFrames = 10;
FAutoConsoleVariableRef CVarRadianceCacheProbesUpdateEveryNFrames(
	TEXT("r.Lumen.RadianceCache.ProbesUpdateEveryNFrames"),
	GRadianceCacheProbesUpdateEveryNFrames,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheNumProbeTracesBudget = 200;
FAutoConsoleVariableRef CVarRadianceCacheNumProbeTracesBudget(
	TEXT("r.Lumen.RadianceCache.NumProbeTracesBudget"),
	GRadianceCacheNumProbeTracesBudget,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceNumFramesToKeepCachedProbes = 2;
FAutoConsoleVariableRef CVarRadianceCacheNumFramesToKeepCachedProbes(
	TEXT("r.Lumen.RadianceCache.NumFramesToKeepCachedProbes"),
	GRadianceNumFramesToKeepCachedProbes,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheOverrideCacheOcclusionLighting = 0;
FAutoConsoleVariableRef CVarRadianceCacheShowOnlyRadianceCacheLighting(
	TEXT("r.Lumen.RadianceCache.OverrideCacheOcclusionLighting"),
	GRadianceCacheOverrideCacheOcclusionLighting,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheShowBlackRadianceCacheLighting = 0;
FAutoConsoleVariableRef CVarRadianceCacheShowBlackRadianceCacheLighting(
	TEXT("r.Lumen.RadianceCache.ShowBlackRadianceCacheLighting"),
	GRadianceCacheShowBlackRadianceCacheLighting,
	TEXT(""),
	ECVF_RenderThreadSafe
);

DECLARE_GPU_STAT(LumenRadianceCache);

namespace LumenRadianceCache
{
	bool IsEnabled(const FViewInfo& View)
	{
		return GLumenRadianceCache != 0;
	}

	int32 GetNumClipmaps()
	{
		return FMath::Clamp(GRadianceCacheNumClipmaps, 1, LumenRadianceCache::MaxClipmaps);
	}

	int32 GetClipmapGridResolution()
	{
		const int32 GridResolution = GRadianceCacheGridResolution / (GLumenFastCameraMode ? 2 : 1);
		return FMath::Clamp(GridResolution, 1, 256);
	}

	int32 GetProbeResolution()
	{
		return GRadianceCacheProbeResolution / (GLumenFastCameraMode ? 2 : 1);
	}

	int32 GetFinalProbeResolution()
	{
		return GetProbeResolution() + 2 * (1 << (GRadianceCacheNumMipmaps - 1));
	}

	FIntVector GetProbeIndirectionTextureSize()
	{
		return FIntVector(GetClipmapGridResolution() * GRadianceCacheNumClipmaps, GetClipmapGridResolution(), GetClipmapGridResolution());
	}

	FIntPoint GetProbeAtlasTextureSize()
	{
		return FIntPoint(GRadianceCacheProbeAtlasResolutionInProbes * GetProbeResolution());
	}

	FIntPoint GetFinalRadianceAtlasTextureSize()
	{
		return FIntPoint(GRadianceCacheProbeAtlasResolutionInProbes * GetFinalProbeResolution(), GRadianceCacheProbeAtlasResolutionInProbes * GetFinalProbeResolution());
	}

	void GetParameters(const FViewInfo& View, FRDGBuilder& GraphBuilder, FRadianceCacheParameters& OutParameters)
	{
		OutParameters.NumRadianceProbeClipmaps = 0;

		if (View.ViewState && View.ViewState->RadianceCacheState.FinalRadianceAtlas.IsValid())
		{
			const FRadianceCacheState& RadianceCacheState = View.ViewState->RadianceCacheState;
			OutParameters.RadianceProbeIndirectionTexture = RadianceCacheState.RadianceProbeIndirectionTexture ? GraphBuilder.RegisterExternalTexture(RadianceCacheState.RadianceProbeIndirectionTexture, TEXT("RadianceCacheIndirectionTexture")) : nullptr;
			OutParameters.RadianceCacheFinalRadianceAtlas = GraphBuilder.RegisterExternalTexture(RadianceCacheState.FinalRadianceAtlas, TEXT("RadianceCacheFinalRadianceAtlas"));
			OutParameters.RadianceCacheDepthAtlas = GraphBuilder.RegisterExternalTexture(RadianceCacheState.DepthProbeAtlasTexture, TEXT("RadianceCacheDepthAtlas"));

			for (int32 ClipmapIndex = 0; ClipmapIndex < RadianceCacheState.Clipmaps.Num(); ++ClipmapIndex)
			{
				const FRadianceCacheClipmap& Clipmap = RadianceCacheState.Clipmaps[ClipmapIndex];

				OutParameters.RadianceProbeClipmapTMin[ClipmapIndex] = Clipmap.ProbeTMin;
				OutParameters.WorldPositionToRadianceProbeCoordScale[ClipmapIndex] = Clipmap.WorldPositionToProbeCoordScale;
				OutParameters.WorldPositionToRadianceProbeCoordBias[ClipmapIndex] = Clipmap.WorldPositionToProbeCoordBias;
				OutParameters.RadianceProbeCoordToWorldPositionScale[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterScale;
				OutParameters.RadianceProbeCoordToWorldPositionBias[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterBias;
			}

			OutParameters.ReprojectionRadiusScale = FMath::Clamp<float>(GRadianceCacheReprojectionRadiusScale, 1.0f, 10000.0f);
			OutParameters.FinalRadianceAtlasMaxMip = GRadianceCacheNumMipmaps - 1;
			OutParameters.InvProbeFinalRadianceAtlasResolution = FVector2D(1.0f, 1.0f) / FVector2D(LumenRadianceCache::GetFinalRadianceAtlasTextureSize());
			OutParameters.InvProbeDepthAtlasResolution = FVector2D(1.0f, 1.0f) / FVector2D(LumenRadianceCache::GetProbeAtlasTextureSize());

			OutParameters.RadianceProbeClipmapResolution = GetClipmapGridResolution();
			OutParameters.ProbeAtlasResolutionInProbes = FIntPoint(GRadianceCacheProbeAtlasResolutionInProbes, GRadianceCacheProbeAtlasResolutionInProbes);
			OutParameters.NumRadianceProbeClipmaps = GetNumClipmaps();
			OutParameters.RadianceProbeResolution = GetProbeResolution();
			OutParameters.FinalProbeResolution = GetFinalProbeResolution();
			OutParameters.OverrideCacheOcclusionLighting = GRadianceCacheOverrideCacheOcclusionLighting;
			OutParameters.ShowBlackRadianceCacheLighting = GRadianceCacheShowBlackRadianceCacheLighting;
		}
		else
		{
			OutParameters.RadianceProbeIndirectionTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
			OutParameters.RadianceCacheFinalRadianceAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
			OutParameters.RadianceCacheDepthAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		}
	}

	int32 GetNumProbeTracesBudget()
	{
		return GRadianceCacheForceFullUpdate ? 1000000 : GRadianceCacheNumProbeTracesBudget;
	}

	int32 GetMaxNumProbes()
	{
		return GRadianceCacheProbeAtlasResolutionInProbes * GRadianceCacheProbeAtlasResolutionInProbes;
	}
};

bool ShouldRenderRadianceCache(const FScene* Scene, const FViewInfo& View)
{
	return Lumen::ShouldRenderLumenForView(Scene, View) && GLumenRadianceCache && View.Family->EngineShowFlags.LumenDiffuseIndirect;
}

class FClearProbeFreeList : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearProbeFreeList)
	SHADER_USE_PARAMETER_STRUCT(FClearProbeFreeList, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeFreeList)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER(uint32, MaxNumProbes)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearProbeFreeList, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ClearProbeFreeListCS", SF_Compute);

class FClearProbeIndirectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearProbeIndirectionCS)
	SHADER_USE_PARAMETER_STRUCT(FClearProbeIndirectionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearProbeIndirectionCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ClearProbeIndirectionCS", SF_Compute);

class FSetupMarkRadianceProbesUsedByProbeHierarchyCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupMarkRadianceProbesUsedByProbeHierarchyCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupMarkRadianceProbesUsedByProbeHierarchyCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchParametersOutput)
	END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupMarkRadianceProbesUsedByProbeHierarchyCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "SetupMarkRadianceProbesUsedByProbeHierarchyCS", SF_Compute);


class FMarkRadianceProbesUsedByProbeHierarchyCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMarkRadianceProbesUsedByProbeHierarchyCS)
	SHADER_USE_PARAMETER_STRUCT(FMarkRadianceProbesUsedByProbeHierarchyCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER(uint32, VisualizeLumenScene)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyLevelParameters, HierarchyLevelParameters)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, DispatchParameters)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FMarkRadianceProbesUsedByProbeHierarchyCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "MarkRadianceProbesUsedByProbeHierarchyCS", SF_Compute);


class FMarkRadianceProbesUsedByScreenProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMarkRadianceProbesUsedByScreenProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FMarkRadianceProbesUsedByScreenProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER(uint32, VisualizeLumenScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FMarkRadianceProbesUsedByScreenProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "MarkRadianceProbesUsedByScreenProbesCS", SF_Compute);

void RadianceCacheMarkUsedProbes(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const LumenProbeHierarchy::FHierarchyParameters* ProbeHierarchyParameters,
	const FScreenProbeParameters* ScreenProbeParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	FRDGTextureUAVRef RadianceProbeIndirectionTextureUAV)
{
	// If the probe hierarchy is enabled, mark it's highest level probe positions as used
	// Otherwise mark positions around the GBuffer as used

	if (ProbeHierarchyParameters)
	{
		FRDGBufferRef DispatchParameters = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(LumenProbeHierarchy::kProbeMaxHierarchyDepth),
			TEXT("LumenVoxelTraceProbeDispatch"));

		const LumenProbeHierarchy::FHierarchyParameters& HierarchyParameters = *ProbeHierarchyParameters;
		{
			FSetupMarkRadianceProbesUsedByProbeHierarchyCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupMarkRadianceProbesUsedByProbeHierarchyCS::FParameters>();
			PassParameters->HierarchyParameters = HierarchyParameters;
			PassParameters->DispatchParametersOutput = GraphBuilder.CreateUAV(DispatchParameters);

			auto ComputeShader = View.ShaderMap->GetShader<FSetupMarkRadianceProbesUsedByProbeHierarchyCS>();
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SetupMarkRadianceProbesUsedByProbeHierarchy"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}

		if (HierarchyParameters.HierarchyDepth > 0)
		{
			const int32 HierarchyLevelId = HierarchyParameters.HierarchyDepth - 1;
			FMarkRadianceProbesUsedByProbeHierarchyCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMarkRadianceProbesUsedByProbeHierarchyCS::FParameters>();
			PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
			PassParameters->VisualizeLumenScene = View.Family->EngineShowFlags.VisualizeLumenScene != 0 ? 1 : 0;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			PassParameters->HierarchyParameters = HierarchyParameters;
			PassParameters->HierarchyLevelParameters = LumenProbeHierarchy::GetLevelParameters(HierarchyParameters, HierarchyLevelId);
			PassParameters->DispatchParameters = DispatchParameters;

			auto ComputeShader = View.ShaderMap->GetShader<FMarkRadianceProbesUsedByProbeHierarchyCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MarkRadianceProbesUsedByProbeHierarchy"),
				ComputeShader,
				PassParameters,
				DispatchParameters,
				sizeof(FRHIDispatchIndirectParameters) * HierarchyLevelId);
		}
	}
	else
	{
		check(ScreenProbeParameters);
		FMarkRadianceProbesUsedByScreenProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMarkRadianceProbesUsedByScreenProbesCS::FParameters>();
		PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::SceneDepth);
		PassParameters->ScreenProbeParameters = *ScreenProbeParameters;
		PassParameters->VisualizeLumenScene = View.Family->EngineShowFlags.VisualizeLumenScene != 0 ? 1 : 0;
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FMarkRadianceProbesUsedByScreenProbesCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MarkRadianceProbesUsedByScreenProbes %ux%u", ScreenProbeParameters->ScreenProbeAtlasViewSize.X, ScreenProbeParameters->ScreenProbeAtlasViewSize.Y),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters->ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::ThreadPerProbe * sizeof(FRHIDispatchIndirectParameters));
	}
}

class FUpdateCacheForUsedProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FUpdateCacheForUsedProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FUpdateCacheForUsedProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeFreeList)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, LastFrameRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_ARRAY(float, LastFrameRadianceProbeCoordToWorldPositionScale, [LumenRadianceCache::MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector, LastFrameRadianceProbeCoordToWorldPositionBias, [LumenRadianceCache::MaxClipmaps])
		SHADER_PARAMETER(uint32, FrameNumber)
		SHADER_PARAMETER(uint32, NumFramesToKeepCachedProbes)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FUpdateCacheForUsedProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "UpdateCacheForUsedProbesCS", SF_Compute);

class FAllocateUsedProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAllocateUsedProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FAllocateUsedProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, RWProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeFreeList)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NewProbeTraceAllocator)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, FrameNumber)
		SHADER_PARAMETER(uint32, ProbesUpdateEveryNFrames)
		SHADER_PARAMETER(uint32, NumProbeTracesBudget)
		SHADER_PARAMETER(uint32, MaxNumProbes)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FUpdateNewProbesPass : SHADER_PERMUTATION_BOOL("UPDATE_NEW_PROBES_PASS");
	class FPersistentCache : SHADER_PERMUTATION_BOOL("PERSISTENT_CACHE");
	using FPermutationDomain = TShaderPermutationDomain<FUpdateNewProbesPass, FPersistentCache>;

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FAllocateUsedProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "AllocateUsedProbesCS", SF_Compute);

class FClampProbeFreeListAllocatorCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClampProbeFreeListAllocatorCS)
	SHADER_USE_PARAMETER_STRUCT(FClampProbeFreeListAllocatorCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER(uint32, MaxNumProbes)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 1;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClampProbeFreeListAllocatorCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ClampProbeFreeListAllocatorCS", SF_Compute);

class FSetupTraceFromProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupTraceFromProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupTraceFromProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTraceProbesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTraceProbesOverbudgetIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFixupProbeBordersIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER(uint32, TraceFromProbesGroupSizeXY)
		SHADER_PARAMETER(uint32, NumProbeTracesBudget)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupTraceFromProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "SetupTraceFromProbesCS", SF_Compute);

class FRadianceCacheTraceFromProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRadianceCacheTraceFromProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FRadianceCacheTraceFromProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWDepthProbeAtlasTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, TraceProbesIndirectArgs)
		SHADER_PARAMETER(uint32, NumProbeTracesBudget)
	END_SHADER_PARAMETER_STRUCT()

	class FOverbudgetPass : SHADER_PERMUTATION_BOOL("OVERBUDGET_TRACING_PASS");
	class FDistantScene : SHADER_PERMUTATION_BOOL("TRACE_DISTANT_SCENE");
	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");

	using FPermutationDomain = TShaderPermutationDomain<FOverbudgetPass, FDistantScene, FDynamicSkyLight>;

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FRadianceCacheTraceFromProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "TraceFromProbesCS", SF_Compute);

class FCopyProbesAndFixupBordersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyProbesAndFixupBordersCS)
	SHADER_USE_PARAMETER_STRUCT(FCopyProbesAndFixupBordersCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWFinalRadianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, FixupProbeBordersIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FCopyProbesAndFixupBordersCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "CopyProbesAndFixupBordersCS", SF_Compute);


class FGenerateMipLevelCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateMipLevelCS)
	SHADER_USE_PARAMETER_STRUCT(FGenerateMipLevelCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWFinalRadianceAtlasMip)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, FinalRadianceAtlasParentMip)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER(uint32, MipLevel)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, FixupProbeBordersIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FGenerateMipLevelCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "GenerateMipLevelCS", SF_Compute);


bool UpdateRadianceCacheState(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	FRadianceCacheState& CacheState = View.ViewState->RadianceCacheState;

	bool bResetState = CacheState.ClipmapWorldExtent != GLumenRadianceCacheClipmapWorldExtent || CacheState.ClipmapDistributionBase != GLumenRadianceCacheClipmapDistributionBase;

	CacheState.ClipmapWorldExtent = GLumenRadianceCacheClipmapWorldExtent;
	CacheState.ClipmapDistributionBase = GLumenRadianceCacheClipmapDistributionBase;

	const int32 ClipmapResolution = LumenRadianceCache::GetClipmapGridResolution();
	const int32 NumClipmaps = LumenRadianceCache::GetNumClipmaps();

	const FVector NewViewOrigin = View.ViewMatrices.GetViewOrigin();

	CacheState.Clipmaps.SetNum(NumClipmaps);

	for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmaps; ++ClipmapIndex)
	{
		FRadianceCacheClipmap& Clipmap = CacheState.Clipmaps[ClipmapIndex];

		const float ClipmapExtent = GLumenRadianceCacheClipmapWorldExtent * FMath::Pow(GLumenRadianceCacheClipmapDistributionBase, ClipmapIndex);
		const float CellSize = (2.0f * ClipmapExtent) / LumenRadianceCache::GetClipmapGridResolution();

		FIntVector GridCenter;
		GridCenter.X = FMath::FloorToInt(NewViewOrigin.X / CellSize);
		GridCenter.Y = FMath::FloorToInt(NewViewOrigin.Y / CellSize);
		GridCenter.Z = FMath::FloorToInt(NewViewOrigin.Z / CellSize);

		const FVector SnappedCenter = FVector(GridCenter) * CellSize;

		Clipmap.Center = SnappedCenter;
		Clipmap.Extent = ClipmapExtent;
		Clipmap.VolumeUVOffset = FVector(0.0f, 0.0f, 0.0f);
		Clipmap.CellSize = CellSize;

		const FVector ClipmapMin = Clipmap.Center - Clipmap.Extent;

		Clipmap.ProbeCoordToWorldCenterBias = ClipmapMin + 0.5f * Clipmap.CellSize;
		Clipmap.ProbeCoordToWorldCenterScale = Clipmap.CellSize;

		Clipmap.WorldPositionToProbeCoordScale = 1.0f / CellSize;
		Clipmap.WorldPositionToProbeCoordBias = -ClipmapMin / CellSize;
		
		// Extend probe to at least cover bilinear sampling region
		const float ProbeRadiusScale = FMath::Clamp(GRadianceCacheProbeRadiusScale, 1.0f, 16.0f);
		Clipmap.ProbeTMin = ProbeRadiusScale * FVector(CellSize, CellSize, CellSize).Size();
	}

	return bResetState;
}

void FDeferredShadingSceneRenderer::RenderRadianceCache(
	FRDGBuilder& GraphBuilder, 
	const FLumenCardTracingInputs& TracingInputs, 
	const FViewInfo& View, 
	const LumenProbeHierarchy::FHierarchyParameters* ProbeHierarchyParameters,
	const FScreenProbeParameters* ScreenProbeParameters,
	LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters)
{
	if (ShouldRenderRadianceCache(Scene, View) && GRadianceCacheUpdate != 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenRadianceCache);
		RDG_EVENT_SCOPE(GraphBuilder, "RadianceCache");

		const TArray<FRadianceCacheClipmap> LastFrameClipmaps = View.ViewState->RadianceCacheState.Clipmaps;
		bool bResizedHistoryState = UpdateRadianceCacheState(GraphBuilder, View);

		FRadianceCacheState& RadianceCacheState = View.ViewState->RadianceCacheState;

		const FIntPoint RadianceProbeAtlasTextureSize = LumenRadianceCache::GetProbeAtlasTextureSize();
		FRDGTextureRef RadianceProbeAtlasTexture = nullptr;

		if (RadianceCacheState.RadianceProbeAtlasTexture.IsValid()
			&& RadianceCacheState.RadianceProbeAtlasTexture->GetDesc().Extent == RadianceProbeAtlasTextureSize)
		{
			RadianceProbeAtlasTexture = GraphBuilder.RegisterExternalTexture(RadianceCacheState.RadianceProbeAtlasTexture);
		}
		else
		{
			FRDGTextureDesc ProbeAtlasDesc = FRDGTextureDesc::Create2D(
				RadianceProbeAtlasTextureSize,
				PF_FloatRGB,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			RadianceProbeAtlasTexture = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("RadianceProbeAtlasTexture"));
		}

		FRDGTextureRef DepthProbeAtlasTexture = nullptr;

		if (RadianceCacheState.DepthProbeAtlasTexture.IsValid()
			&& RadianceCacheState.DepthProbeAtlasTexture->GetDesc().Extent == RadianceProbeAtlasTextureSize)
		{
			DepthProbeAtlasTexture = GraphBuilder.RegisterExternalTexture(RadianceCacheState.DepthProbeAtlasTexture);
		}
		else
		{
			FRDGTextureDesc ProbeAtlasDesc = FRDGTextureDesc::Create2D(
				RadianceProbeAtlasTextureSize,
				PF_R16F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			DepthProbeAtlasTexture = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("DepthProbeAtlasTexture"));
		}

		const FIntPoint FinalRadianceAtlasSize = LumenRadianceCache::GetFinalRadianceAtlasTextureSize();
		FRDGTextureRef FinalRadianceAtlas = nullptr;

		if (RadianceCacheState.FinalRadianceAtlas.IsValid()
			&& RadianceCacheState.FinalRadianceAtlas->GetDesc().Extent == FinalRadianceAtlasSize
			&& RadianceCacheState.FinalRadianceAtlas->GetDesc().NumMips == GRadianceCacheNumMipmaps)
		{
			FinalRadianceAtlas = GraphBuilder.RegisterExternalTexture(RadianceCacheState.FinalRadianceAtlas);
		}
		else
		{
			FRDGTextureDesc FinalRadianceAtlasDesc = FRDGTextureDesc::Create2D(
				FinalRadianceAtlasSize,
				PF_FloatRGB,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV,
				GRadianceCacheNumMipmaps);

			FinalRadianceAtlas = GraphBuilder.CreateTexture(FinalRadianceAtlasDesc, TEXT("RadianceCacheFinalRadianceAtlas"));
			bResizedHistoryState = true;
		}

		LumenRadianceCache::GetParameters(View, GraphBuilder, RadianceCacheParameters);
		
		RadianceCacheParameters.RadianceProbeIndirectionTexture = nullptr;
		RadianceCacheParameters.RadianceCacheFinalRadianceAtlas = nullptr;
		RadianceCacheParameters.RadianceCacheDepthAtlas = nullptr;

		const FIntVector RadianceProbeIndirectionTextureSize = LumenRadianceCache::GetProbeIndirectionTextureSize();

		FRDGTextureDesc ProbeIndirectionDesc = FRDGTextureDesc::Create3D(
			RadianceProbeIndirectionTextureSize,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling);

		FRDGTextureRef RadianceProbeIndirectionTexture = GraphBuilder.CreateTexture(FRDGTextureDesc(ProbeIndirectionDesc), TEXT("RadianceProbeIndirectionTexture"));
		FRDGTextureUAVRef RadianceProbeIndirectionTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadianceProbeIndirectionTexture));

		// Clear each clipmap indirection entry to invalid probe index
		{
			FClearProbeIndirectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearProbeIndirectionCS::FParameters>();
			PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;

			auto ComputeShader = View.ShaderMap->GetShader<FClearProbeIndirectionCS>(0);

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FClearProbeIndirectionCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearProbeIndirectionCS"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		// Mark indirection entries around positions that will be sampled by dependent features as used
		RadianceCacheMarkUsedProbes(GraphBuilder, View, ProbeHierarchyParameters, ScreenProbeParameters, RadianceCacheParameters, RadianceProbeIndirectionTextureUAV);

		const bool bPersistentCache = !GRadianceCacheForceFullUpdate 
			&& View.ViewState 
			&& IsValidRef(RadianceCacheState.RadianceProbeIndirectionTexture)
			&& RadianceCacheState.RadianceProbeIndirectionTexture->GetDesc().GetSize() == RadianceProbeIndirectionTextureSize
			&& !bResizedHistoryState;

		FRDGBufferRef ProbeFreeListAllocator = nullptr;
		FRDGBufferRef ProbeFreeList = nullptr;
		FRDGBufferRef ProbeLastUsedFrame = nullptr;
		const int32 MaxNumProbes = LumenRadianceCache::GetMaxNumProbes();

		if (IsValidRef(RadianceCacheState.ProbeFreeList) && RadianceCacheState.ProbeFreeList->Desc.NumElements == MaxNumProbes)
		{
			ProbeFreeListAllocator = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeFreeListAllocator);
			ProbeFreeList = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeFreeList);
			ProbeLastUsedFrame = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeLastUsedFrame);
		}
		else
		{
			ProbeFreeListAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(int32), 1), TEXT("RadianceCacheProbeFreeListAllocator"));
			ProbeFreeList = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumProbes), TEXT("RadianceCacheProbeFreeList"));
			ProbeLastUsedFrame = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumProbes), TEXT("ProbeLastUsedFrame"));
		}

		FRDGBufferUAVRef ProbeFreeListAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeFreeListAllocator, PF_R32_SINT));
		FRDGBufferUAVRef ProbeFreeListUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeFreeList, PF_R32_UINT));
		FRDGBufferUAVRef ProbeLastUsedFrameUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeLastUsedFrame, PF_R32_UINT));

		if (!bPersistentCache || !IsValidRef(RadianceCacheState.ProbeFreeListAllocator))
		{
			FClearProbeFreeList::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearProbeFreeList::FParameters>();
			PassParameters->RWProbeFreeListAllocator = ProbeFreeListAllocatorUAV;
			PassParameters->RWProbeFreeList = ProbeFreeListUAV;
			PassParameters->RWProbeLastUsedFrame = ProbeLastUsedFrameUAV;
			PassParameters->MaxNumProbes = MaxNumProbes;

			auto ComputeShader = View.ShaderMap->GetShader<FClearProbeFreeList>();

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(MaxNumProbes, FClearProbeFreeList::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearProbeFreeList"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}
		
		// Propagate probes from last frame to the new frame's indirection
		if (bPersistentCache)
		{
			FRDGTextureRef LastFrameRadianceProbeIndirectionTexture = GraphBuilder.RegisterExternalTexture(RadianceCacheState.RadianceProbeIndirectionTexture);

			{
				FUpdateCacheForUsedProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdateCacheForUsedProbesCS::FParameters>();
				PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
				PassParameters->RWProbeFreeListAllocator = ProbeFreeListAllocatorUAV;
				PassParameters->RWProbeFreeList = ProbeFreeListUAV;
				PassParameters->RWProbeLastUsedFrame = ProbeLastUsedFrameUAV;
				PassParameters->LastFrameRadianceProbeIndirectionTexture = LastFrameRadianceProbeIndirectionTexture;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->FrameNumber = View.ViewState->GetFrameIndex();
				PassParameters->NumFramesToKeepCachedProbes = GRadianceNumFramesToKeepCachedProbes;

				for (int32 ClipmapIndex = 0; ClipmapIndex < LastFrameClipmaps.Num(); ++ClipmapIndex)
				{
					const FRadianceCacheClipmap& Clipmap = LastFrameClipmaps[ClipmapIndex];
					PassParameters->LastFrameRadianceProbeCoordToWorldPositionScale[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterScale;
					PassParameters->LastFrameRadianceProbeCoordToWorldPositionBias[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterBias;
				}

				auto ComputeShader = View.ShaderMap->GetShader<FUpdateCacheForUsedProbesCS>(0);

				const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FUpdateCacheForUsedProbesCS::GetGroupSize());

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("UpdateCacheForUsedProbes"),
					ComputeShader,
					PassParameters,
					GroupSize);
			}
		}

		FRDGTextureUAVRef FinalRadianceAtlasUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalRadianceAtlas));
		FRDGTextureUAVRef RadianceProbeTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadianceProbeAtlasTexture));
		FRDGTextureUAVRef DepthProbeTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DepthProbeAtlasTexture));
		
		FRDGBufferRef ProbeAllocator = nullptr;

		if (IsValidRef(RadianceCacheState.ProbeAllocator))
		{
			ProbeAllocator = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeAllocator, TEXT("ProbeAllocator"));
		}
		else
		{
			ProbeAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("RadianceCacheProbeAllocator"));
		}

		FRDGBufferUAVRef ProbeAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeAllocator, PF_R32_UINT));

		if (!bPersistentCache || !IsValidRef(RadianceCacheState.ProbeAllocator))
		{
			FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, ProbeAllocatorUAV, 0);
		}

		FRDGBufferRef ProbeTraceData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4), MaxNumProbes), TEXT("RadianceCacheProbeTraceData"));
		FRDGBufferRef UpdateNewProbesTraceAllocator = nullptr;

		// Update probe lighting in two passes:
		// The first operates on new probes (cache misses) which trace at a lower resolution when over budget.
		// The second operates on existing probes which need retracing to propagate lighting changes. These trace less often when new probe traces are over budget, but always full resolution.

		for (int32 UpdatePassIndex = 0; UpdatePassIndex < 2; UpdatePassIndex++)
		{
			const bool bUpdateNewProbes = UpdatePassIndex == 0;
			const bool bUpdateExistingProbes = UpdatePassIndex == 1;

			FRDGBufferRef ProbeTraceAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("ProbeTraceAllocator"));
			FRDGBufferUAVRef ProbeTraceAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceAllocator, PF_R32_UINT));
			FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, ProbeTraceAllocatorUAV, 0);

			if (bUpdateNewProbes)
			{
				UpdateNewProbesTraceAllocator = ProbeTraceAllocator;
			}

			{
				FAllocateUsedProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateUsedProbesCS::FParameters>();
				PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
				PassParameters->RWProbeLastUsedFrame = ProbeLastUsedFrameUAV;
				PassParameters->RWProbeAllocator = ProbeAllocatorUAV;
				PassParameters->RWProbeTraceAllocator = ProbeTraceAllocatorUAV;
				PassParameters->RWProbeTraceData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RWProbeFreeListAllocator = bPersistentCache ? ProbeFreeListAllocatorUAV : nullptr;
				PassParameters->NewProbeTraceAllocator = bUpdateExistingProbes ? GraphBuilder.CreateSRV(FRDGBufferSRVDesc(UpdateNewProbesTraceAllocator, PF_R32_UINT)) : nullptr;
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->ProbeFreeList = bPersistentCache ? GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeFreeList, PF_R32_UINT)) : nullptr;
				PassParameters->FrameNumber = View.ViewState->GetFrameIndex();
				PassParameters->ProbesUpdateEveryNFrames = GRadianceCacheProbesUpdateEveryNFrames;
				PassParameters->NumProbeTracesBudget = LumenRadianceCache::GetNumProbeTracesBudget();
				PassParameters->MaxNumProbes = MaxNumProbes;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;

				FAllocateUsedProbesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FAllocateUsedProbesCS::FUpdateNewProbesPass>(bUpdateNewProbes);
				PermutationVector.Set<FAllocateUsedProbesCS::FPersistentCache>(bPersistentCache);
				auto ComputeShader = View.ShaderMap->GetShader<FAllocateUsedProbesCS>(PermutationVector);

				const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FAllocateUsedProbesCS::GetGroupSize());

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					bUpdateNewProbes ? RDG_EVENT_NAME("AllocateNewProbeTraces") : RDG_EVENT_NAME("AllocateExistingProbeTraces"),
					ComputeShader,
					PassParameters,
					GroupSize);
			}

			FRDGBufferRef TraceProbesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("TraceProbesIndirectArgs"));
			FRDGBufferRef TraceProbesOverbudgetIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("TraceProbesOverbudgetIndirectArgs"));
			FRDGBufferRef FixupProbeBordersIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("FixupProbeBordersIndirectArgs"));

			{
				FSetupTraceFromProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupTraceFromProbesCS::FParameters>();
				PassParameters->RWTraceProbesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(TraceProbesIndirectArgs, PF_R32_UINT));
				PassParameters->RWTraceProbesOverbudgetIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(TraceProbesOverbudgetIndirectArgs, PF_R32_UINT));
				PassParameters->RWFixupProbeBordersIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(FixupProbeBordersIndirectArgs, PF_R32_UINT));
				PassParameters->ProbeTraceAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceAllocator, PF_R32_UINT));
				PassParameters->TraceFromProbesGroupSizeXY = FRadianceCacheTraceFromProbesCS::GetGroupSize();
				PassParameters->NumProbeTracesBudget = bUpdateNewProbes ? LumenRadianceCache::GetNumProbeTracesBudget() : LumenRadianceCache::GetMaxNumProbes();
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				auto ComputeShader = View.ShaderMap->GetShader<FSetupTraceFromProbesCS>(0);

				const FIntVector GroupSize = FIntVector(1);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("SetupTraceFromProbes"),
					ComputeShader,
					PassParameters,
					GroupSize);
			}

			for (int32 TracePassIndex = 0; TracePassIndex < 2; TracePassIndex++)
			{
				FRadianceCacheTraceFromProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRadianceCacheTraceFromProbesCS::FParameters>();
				GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
				SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, -1.0f);
				PassParameters->RWRadianceProbeAtlasTexture = RadianceProbeTextureUAV;
				PassParameters->RWDepthProbeAtlasTexture = DepthProbeTextureUAV;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->TraceProbesIndirectArgs = TracePassIndex == 0 ? TraceProbesIndirectArgs : TraceProbesOverbudgetIndirectArgs;
				PassParameters->NumProbeTracesBudget = bUpdateNewProbes ? LumenRadianceCache::GetNumProbeTracesBudget() : LumenRadianceCache::GetMaxNumProbes();

				FRadianceCacheTraceFromProbesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FRadianceCacheTraceFromProbesCS::FOverbudgetPass>(TracePassIndex == 1);
				PermutationVector.Set<FRadianceCacheTraceFromProbesCS::FDistantScene>(Scene->LumenSceneData->DistantCardIndices.Num() > 0);
				PermutationVector.Set<FRadianceCacheTraceFromProbesCS::FDynamicSkyLight>(ShouldRenderDynamicSkyLight(Scene, ViewFamily));
				auto ComputeShader = View.ShaderMap->GetShader<FRadianceCacheTraceFromProbesCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("TraceFromProbes Res=%ux%u", LumenRadianceCache::GetProbeResolution() / (TracePassIndex + 1), LumenRadianceCache::GetProbeResolution() / (TracePassIndex + 1)),
					ComputeShader,
					PassParameters,
					PassParameters->TraceProbesIndirectArgs,
					0);
			}

			{
				FCopyProbesAndFixupBordersCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCopyProbesAndFixupBordersCS::FParameters>();
				PassParameters->RWFinalRadianceAtlas = FinalRadianceAtlasUAV;
				PassParameters->RadianceProbeAtlasTexture = RadianceProbeAtlasTexture;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->FixupProbeBordersIndirectArgs = FixupProbeBordersIndirectArgs;

				auto ComputeShader = View.ShaderMap->GetShader<FCopyProbesAndFixupBordersCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CopyProbesAndFixupBorders"),
					ComputeShader,
					PassParameters,
					FixupProbeBordersIndirectArgs,
					0);
			}

			for (int32 MipLevel = 1; MipLevel < GRadianceCacheNumMipmaps; MipLevel++)
			{
				FGenerateMipLevelCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateMipLevelCS::FParameters>();
				PassParameters->RWFinalRadianceAtlasMip = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalRadianceAtlas, MipLevel));
				PassParameters->FinalRadianceAtlasParentMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(FinalRadianceAtlas, MipLevel - 1));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->MipLevel = MipLevel;
				PassParameters->FixupProbeBordersIndirectArgs = FixupProbeBordersIndirectArgs;
				PassParameters->View = View.ViewUniformBuffer;

				auto ComputeShader = View.ShaderMap->GetShader<FGenerateMipLevelCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GenerateMipLevel"),
					ComputeShader,
					PassParameters,
					FixupProbeBordersIndirectArgs, //@todo - dispatch the right number of threads for this mip instead of mip0
					0);
			}
		}

		if (bPersistentCache)
		{
			FClampProbeFreeListAllocatorCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClampProbeFreeListAllocatorCS::FParameters>();
			PassParameters->RWProbeFreeListAllocator = ProbeFreeListAllocatorUAV;
			PassParameters->MaxNumProbes = MaxNumProbes;
			auto ComputeShader = View.ShaderMap->GetShader<FClampProbeFreeListAllocatorCS>(0);

			const FIntVector GroupSize = FIntVector(1);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClampProbeFreeListAllocator"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		ConvertToExternalBuffer(GraphBuilder, ProbeFreeListAllocator, RadianceCacheState.ProbeFreeListAllocator);
		ConvertToExternalBuffer(GraphBuilder, ProbeFreeList, RadianceCacheState.ProbeFreeList);
		ConvertToExternalBuffer(GraphBuilder, ProbeAllocator, RadianceCacheState.ProbeAllocator);
		ConvertToExternalBuffer(GraphBuilder, ProbeLastUsedFrame, RadianceCacheState.ProbeLastUsedFrame);
		ConvertToExternalTexture(GraphBuilder, RadianceProbeIndirectionTexture, RadianceCacheState.RadianceProbeIndirectionTexture);
		ConvertToExternalTexture(GraphBuilder, RadianceProbeAtlasTexture, RadianceCacheState.RadianceProbeAtlasTexture);
		ConvertToExternalTexture(GraphBuilder, DepthProbeAtlasTexture, RadianceCacheState.DepthProbeAtlasTexture);
		ConvertToExternalTexture(GraphBuilder, FinalRadianceAtlas, RadianceCacheState.FinalRadianceAtlas);
	
		RadianceCacheParameters.RadianceProbeIndirectionTexture = RadianceProbeIndirectionTexture;
		RadianceCacheParameters.RadianceCacheFinalRadianceAtlas = FinalRadianceAtlas;
		RadianceCacheParameters.RadianceCacheDepthAtlas = DepthProbeAtlasTexture;
	}
	else // GRadianceCacheUpdate != 0
	{
		LumenRadianceCache::GetParameters(View, GraphBuilder, RadianceCacheParameters);
	}
}
