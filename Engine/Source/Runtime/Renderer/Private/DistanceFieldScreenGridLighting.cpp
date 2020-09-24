// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldScreenGridLighting.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "RHIStaticStates.h"
#include "GlobalDistanceFieldParameters.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "DistanceFieldLightingShared.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "DistanceFieldLightingPost.h"
#include "GlobalDistanceField.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "VisualizeTexture.h"

int32 GAOUseJitter = 1;
FAutoConsoleVariableRef CVarAOUseJitter(
	TEXT("r.AOUseJitter"),
	GAOUseJitter,
	TEXT("Whether to use 4x temporal supersampling with Screen Grid DFAO.  When jitter is disabled, a shorter history can be used but there will be more spatial aliasing."),
	ECVF_RenderThreadSafe
	);

int32 GConeTraceDownsampleFactor = 4;

FIntPoint GetBufferSizeForConeTracing()
{
	return FIntPoint::DivideAndRoundDown(GetBufferSizeForAO(), GConeTraceDownsampleFactor);
}

FVector2D JitterOffsets[4] = 
{
	FVector2D(.25f, 0),
	FVector2D(.75f, .25f),
	FVector2D(.5f, .75f),
	FVector2D(0, .5f)
};

extern int32 GAOUseHistory;

FVector2D GetJitterOffset(int32 SampleIndex)
{
	if (GAOUseJitter && GAOUseHistory)
	{
		return JitterOffsets[SampleIndex] * GConeTraceDownsampleFactor;
	}

	return FVector2D(0, 0);
}

void FAOScreenGridResources::InitDynamicRHI()
{
	//@todo - 2d textures
	const uint32 FastVRamFlag = GFastVRamConfig.DistanceFieldAOScreenGridResources | (IsTransientResourceBufferAliasingEnabled() ? BUF_Transient : BUF_None);
	ScreenGridConeVisibility.Initialize(sizeof(uint32), NumConeSampleDirections * ScreenGridDimensions.X * ScreenGridDimensions.Y, PF_R32_UINT, BUF_Static | FastVRamFlag, TEXT("ScreenGridConeVisibility"));
}

template<bool bUseGlobalDistanceField>
class TConeTraceScreenGridObjectOcclusionCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TConeTraceScreenGridObjectOcclusionCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FTileIntersectionParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_DISTANCE_FIELD"), bUseGlobalDistanceField);

		// To reduce shader compile time of compute shaders with shared memory, doesn't have an impact on generated code with current compiler (June 2010 DX SDK)
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	TConeTraceScreenGridObjectOcclusionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ObjectParameters.Bind(Initializer.ParameterMap);
		AOParameters.Bind(Initializer.ParameterMap);
		ScreenGridParameters.Bind(Initializer.ParameterMap);
		GlobalDistanceFieldParameters.Bind(Initializer.ParameterMap);
		TileConeDepthRanges.Bind(Initializer.ParameterMap, TEXT("TileConeDepthRanges"));
		TileIntersectionParameters.Bind(Initializer.ParameterMap);
		TanConeHalfAngle.Bind(Initializer.ParameterMap, TEXT("TanConeHalfAngle"));
		BentNormalNormalizeFactor.Bind(Initializer.ParameterMap, TEXT("BentNormalNormalizeFactor"));
		ScreenGridConeVisibility.Bind(Initializer.ParameterMap, TEXT("ScreenGridConeVisibility"));
		ConeDepthVisibilityFunction.Bind(Initializer.ParameterMap, TEXT("ConeDepthVisibilityFunction"));
	}

	TConeTraceScreenGridObjectOcclusionCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		FSceneRenderTargetItem& DistanceFieldNormal, 
		const FDistanceFieldAOParameters& Parameters,
		const FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
		AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
		AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
		AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();

		ObjectParameters.Set(RHICmdList, ShaderRHI, GAOCulledObjectBuffers.Buffers, TextureAtlas, FIntVector(AtlasSizeX, AtlasSizeY, AtlasSizeZ));

		AOParameters.Set(RHICmdList, ShaderRHI, Parameters);
		ScreenGridParameters.Set(RHICmdList, ShaderRHI, View, DistanceFieldNormal);

		if (bUseGlobalDistanceField)
		{
			GlobalDistanceFieldParameters.Set(RHICmdList, ShaderRHI, GlobalDistanceFieldInfo.ParameterData);
		}

		FAOSampleData2 AOSampleData;

		TArray<FVector, TInlineAllocator<9> > SampleDirections;
		GetSpacedVectors(View.Family->FrameNumber, SampleDirections);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			AOSampleData.SampleDirections[SampleIndex] = FVector4(SampleDirections[SampleIndex]);
		}

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, GetUniformBufferParameter<FAOSampleData2>(), AOSampleData);

		FTileIntersectionResources* TileIntersectionResources = View.ViewState->AOTileIntersectionResources;
		SetSRVParameter(RHICmdList, ShaderRHI, TileConeDepthRanges, TileIntersectionResources->TileConeDepthRanges.SRV);

		TileIntersectionParameters.Set(RHICmdList, ShaderRHI, *TileIntersectionResources);

		extern float GAOConeHalfAngle;
		SetShaderValue(RHICmdList, ShaderRHI, TanConeHalfAngle, FMath::Tan(GAOConeHalfAngle));

		FVector UnoccludedVector(0);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			UnoccludedVector += SampleDirections[SampleIndex];
		}

		float BentNormalNormalizeFactorValue = 1.0f / (UnoccludedVector / NumConeSampleDirections).Size();
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalNormalizeFactor, BentNormalNormalizeFactorValue);

		FAOScreenGridResources* ScreenGridResources = View.ViewState->AOScreenGridResources;

		ScreenGridConeVisibility.SetBuffer(RHICmdList, ShaderRHI, ScreenGridResources->ScreenGridConeVisibility);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		ScreenGridConeVisibility.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
		ConeDepthVisibilityFunction.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
	}

private:
	LAYOUT_FIELD((TDistanceFieldCulledObjectBufferParameters<DFPT_SignedDistanceField>), ObjectParameters);
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FScreenGridParameters, ScreenGridParameters);
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
	LAYOUT_FIELD(FShaderResourceParameter, TileConeDepthRanges);
	LAYOUT_FIELD(FTileIntersectionParameters, TileIntersectionParameters);
	LAYOUT_FIELD(FShaderParameter, TanConeHalfAngle);
	LAYOUT_FIELD(FShaderParameter, BentNormalNormalizeFactor);
	LAYOUT_FIELD(FRWShaderParameter, ScreenGridConeVisibility);
	LAYOUT_FIELD(FRWShaderParameter, ConeDepthVisibilityFunction);
};

#define IMPLEMENT_CONETRACE_CS_TYPE(bUseGlobalDistanceField) \
	typedef TConeTraceScreenGridObjectOcclusionCS<bUseGlobalDistanceField> TConeTraceScreenGridObjectOcclusionCS##bUseGlobalDistanceField; \
	IMPLEMENT_SHADER_TYPE(template<>,TConeTraceScreenGridObjectOcclusionCS##bUseGlobalDistanceField,TEXT("/Engine/Private/DistanceFieldScreenGridLighting.usf"),TEXT("ConeTraceObjectOcclusionCS"),SF_Compute);

IMPLEMENT_CONETRACE_CS_TYPE(true)
IMPLEMENT_CONETRACE_CS_TYPE(false)

const int32 GConeTraceGlobalDFTileSize = 8;

template<bool bConeTraceObjects>
class TConeTraceScreenGridGlobalOcclusionCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TConeTraceScreenGridGlobalOcclusionCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CONE_TRACE_OBJECTS"), bConeTraceObjects);
		OutEnvironment.SetDefine(TEXT("CONE_TRACE_GLOBAL_DISPATCH_SIZEX"), GConeTraceGlobalDFTileSize);
		OutEnvironment.SetDefine(TEXT("OUTPUT_VISIBILITY_DIRECTLY"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("TRACE_DOWNSAMPLE_FACTOR"), GConeTraceDownsampleFactor);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_DISTANCE_FIELD"), TEXT("1"));

		// To reduce shader compile time of compute shaders with shared memory, doesn't have an impact on generated code with current compiler (June 2010 DX SDK)
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	TConeTraceScreenGridGlobalOcclusionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ObjectParameters.Bind(Initializer.ParameterMap);
		AOParameters.Bind(Initializer.ParameterMap);
		ScreenGridParameters.Bind(Initializer.ParameterMap);
		GlobalDistanceFieldParameters.Bind(Initializer.ParameterMap);
		TileConeDepthRanges.Bind(Initializer.ParameterMap, TEXT("TileConeDepthRanges"));
		TileListGroupSize.Bind(Initializer.ParameterMap, TEXT("TileListGroupSize"));
		TanConeHalfAngle.Bind(Initializer.ParameterMap, TEXT("TanConeHalfAngle"));
		BentNormalNormalizeFactor.Bind(Initializer.ParameterMap, TEXT("BentNormalNormalizeFactor"));
		ScreenGridConeVisibility.Bind(Initializer.ParameterMap, TEXT("ScreenGridConeVisibility"));
	}

	TConeTraceScreenGridGlobalOcclusionCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		FIntPoint TileListGroupSizeValue, 
		FSceneRenderTargetItem& DistanceFieldNormal, 
		const FDistanceFieldAOParameters& Parameters,
		const FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
		AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
		AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
		AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();

		ObjectParameters.Set(RHICmdList, ShaderRHI, GAOCulledObjectBuffers.Buffers, TextureAtlas, FIntVector(AtlasSizeX, AtlasSizeY, AtlasSizeZ));

		AOParameters.Set(RHICmdList, ShaderRHI, Parameters);
		ScreenGridParameters.Set(RHICmdList, ShaderRHI, View, DistanceFieldNormal);
		GlobalDistanceFieldParameters.Set(RHICmdList, ShaderRHI, GlobalDistanceFieldInfo.ParameterData);

		FAOSampleData2 AOSampleData;

		TArray<FVector, TInlineAllocator<9> > SampleDirections;
		GetSpacedVectors(View.Family->FrameNumber, SampleDirections);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			AOSampleData.SampleDirections[SampleIndex] = FVector4(SampleDirections[SampleIndex]);
		}

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, GetUniformBufferParameter<FAOSampleData2>(), AOSampleData);

		if (UseAOObjectDistanceField())
		{
			FTileIntersectionResources* TileIntersectionResources = View.ViewState->AOTileIntersectionResources;
			SetSRVParameter(RHICmdList, ShaderRHI, TileConeDepthRanges, TileIntersectionResources->TileConeDepthRanges.SRV);
		}

		SetShaderValue(RHICmdList, ShaderRHI, TileListGroupSize, TileListGroupSizeValue);

		extern float GAOConeHalfAngle;
		SetShaderValue(RHICmdList, ShaderRHI, TanConeHalfAngle, FMath::Tan(GAOConeHalfAngle));

		FVector UnoccludedVector(0);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			UnoccludedVector += SampleDirections[SampleIndex];
		}

		float BentNormalNormalizeFactorValue = 1.0f / (UnoccludedVector / NumConeSampleDirections).Size();
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalNormalizeFactor, BentNormalNormalizeFactorValue);

		FAOScreenGridResources* ScreenGridResources = View.ViewState->AOScreenGridResources;
				
		// Note: no transition, want to overlap object cone tracing and global DF cone tracing since both shaders use atomics to ScreenGridConeVisibility
		RHICmdList.Transition(FRHITransitionInfo(ScreenGridResources->ScreenGridConeVisibility.UAV, ERHIAccess::Unknown, ERHIAccess::ERWNoBarrier));

		ScreenGridConeVisibility.SetBuffer(RHICmdList, ShaderRHI, ScreenGridResources->ScreenGridConeVisibility);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		ScreenGridConeVisibility.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
	}

private:
	LAYOUT_FIELD((TDistanceFieldCulledObjectBufferParameters<DFPT_SignedDistanceField>), ObjectParameters);
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FScreenGridParameters, ScreenGridParameters);
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
	LAYOUT_FIELD(FShaderResourceParameter, TileConeDepthRanges);
	LAYOUT_FIELD(FShaderParameter, TileListGroupSize);
	LAYOUT_FIELD(FShaderParameter, TanConeHalfAngle);
	LAYOUT_FIELD(FShaderParameter, BentNormalNormalizeFactor);
	LAYOUT_FIELD(FRWShaderParameter, ScreenGridConeVisibility);
};

#define IMPLEMENT_CONETRACE_GLOBAL_CS_TYPE(bConeTraceObjects) \
	typedef TConeTraceScreenGridGlobalOcclusionCS<bConeTraceObjects> TConeTraceScreenGridGlobalOcclusionCS##bConeTraceObjects; \
	IMPLEMENT_SHADER_TYPE(template<>,TConeTraceScreenGridGlobalOcclusionCS##bConeTraceObjects,TEXT("/Engine/Private/DistanceFieldScreenGridLighting.usf"),TEXT("ConeTraceGlobalOcclusionCS"),SF_Compute);

IMPLEMENT_CONETRACE_GLOBAL_CS_TYPE(true)
IMPLEMENT_CONETRACE_GLOBAL_CS_TYPE(false)



const int32 GCombineConesSizeX = 8;

class FCombineConeVisibilityCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCombineConeVisibilityCS,Global)
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMBINE_CONES_SIZEX"), GCombineConesSizeX);
		OutEnvironment.SetDefine(TEXT("TRACE_DOWNSAMPLE_FACTOR"), GConeTraceDownsampleFactor);

		// To reduce shader compile time of compute shaders with shared memory, doesn't have an impact on generated code with current compiler (June 2010 DX SDK)
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	FCombineConeVisibilityCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ScreenGridParameters.Bind(Initializer.ParameterMap);
		BentNormalNormalizeFactor.Bind(Initializer.ParameterMap, TEXT("BentNormalNormalizeFactor"));
		ScreenGridConeVisibility.Bind(Initializer.ParameterMap, TEXT("ScreenGridConeVisibility"));
		DistanceFieldBentNormal.Bind(Initializer.ParameterMap, TEXT("DistanceFieldBentNormal"));
		ConeBufferMax.Bind(Initializer.ParameterMap, TEXT("ConeBufferMax"));
		DFNormalBufferUVMax.Bind(Initializer.ParameterMap, TEXT("DFNormalBufferUVMax"));
	}

	FCombineConeVisibilityCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		FSceneRenderTargetItem& DistanceFieldNormal, 
		FSceneRenderTargetItem& DownsampledBentNormal)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		ScreenGridParameters.Set(RHICmdList, ShaderRHI, View, DistanceFieldNormal);

		FAOSampleData2 AOSampleData;

		TArray<FVector, TInlineAllocator<9> > SampleDirections;
		GetSpacedVectors(View.Family->FrameNumber, SampleDirections);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			AOSampleData.SampleDirections[SampleIndex] = FVector4(SampleDirections[SampleIndex]);
		}

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, GetUniformBufferParameter<FAOSampleData2>(), AOSampleData);

		FVector UnoccludedVector(0);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			UnoccludedVector += SampleDirections[SampleIndex];
		}

		float BentNormalNormalizeFactorValue = 1.0f / (UnoccludedVector / NumConeSampleDirections).Size();
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalNormalizeFactor, BentNormalNormalizeFactorValue);

		FAOScreenGridResources* ScreenGridResources = View.ViewState->AOScreenGridResources;

		DistanceFieldBentNormal.SetTexture(RHICmdList, ShaderRHI, DownsampledBentNormal.ShaderResourceTexture, DownsampledBentNormal.UAV);

		SetSRVParameter(RHICmdList, ShaderRHI, ScreenGridConeVisibility, ScreenGridResources->ScreenGridConeVisibility.SRV);

		FIntPoint const ConeBufferMaxValue(View.ViewRect.Width() / GAODownsampleFactor / GConeTraceDownsampleFactor - 1, View.ViewRect.Height() / GAODownsampleFactor / GConeTraceDownsampleFactor - 1);
		SetShaderValue(RHICmdList, ShaderRHI, ConeBufferMax, ConeBufferMaxValue);

		FIntPoint const DFNormalBufferSize = GetBufferSizeForAO();
		FVector2D const DFNormalBufferUVMaxValue(
			(View.ViewRect.Width()  / GAODownsampleFactor - 0.5f) / DFNormalBufferSize.X,
			(View.ViewRect.Height() / GAODownsampleFactor - 0.5f) / DFNormalBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, DFNormalBufferUVMax, DFNormalBufferUVMaxValue);
	}


private:
	LAYOUT_FIELD(FScreenGridParameters, ScreenGridParameters);
	LAYOUT_FIELD(FShaderParameter, BentNormalNormalizeFactor);
	LAYOUT_FIELD(FShaderParameter, DFNormalBufferUVMax);
	LAYOUT_FIELD(FShaderParameter, ConeBufferMax);
	LAYOUT_FIELD(FShaderResourceParameter, ScreenGridConeVisibility);
	LAYOUT_FIELD(FRWShaderParameter, DistanceFieldBentNormal);
};

IMPLEMENT_SHADER_TYPE(,FCombineConeVisibilityCS,TEXT("/Engine/Private/DistanceFieldScreenGridLighting.usf"),TEXT("CombineConeVisibilityCS"),SF_Compute);

void PostProcessBentNormalAOScreenGrid(
	FRDGBuilder& GraphBuilder, 
	const FDistanceFieldAOParameters& Parameters, 
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef VelocityTexture,
	FRDGTextureRef BentNormalInterpolation,
	FRDGTextureRef DistanceFieldNormal,
	FRDGTextureRef& BentNormalOutput)
{
	FSceneViewState* ViewState = (FSceneViewState*)View.State;
	FIntRect* DistanceFieldAOHistoryViewRect = ViewState ? &ViewState->DistanceFieldAOHistoryViewRect : nullptr;
	TRefCountPtr<IPooledRenderTarget>* BentNormalHistoryState = ViewState ? &ViewState->DistanceFieldAOHistoryRT : NULL;

	UpdateHistory(
		GraphBuilder,
		View,
		TEXT("DistanceFieldAOHistory"),
		SceneTexturesUniformBuffer,
		VelocityTexture,
		DistanceFieldNormal,
		BentNormalInterpolation,
		DistanceFieldAOHistoryViewRect,
		BentNormalHistoryState,
		BentNormalOutput,
		Parameters);
}

// TODO(RDG) Replace with individual passes.
BEGIN_SHADER_PARAMETER_STRUCT(FDistanceFieldAOScreenGridParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVCompute)
END_SHADER_PARAMETER_STRUCT()

// TODO(RDG) Replace with individual passes.
BEGIN_SHADER_PARAMETER_STRUCT(FDistanceFieldAOScreenGridCombineConeVisibilityParameters, )
	RDG_TEXTURE_ACCESS(DownsampledBentNormal, ERHIAccess::UAVCompute)
	RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVCompute)
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderDistanceFieldAOScreenGrid(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FDistanceFieldAOParameters& Parameters,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef VelocityTexture,
	FRDGTextureRef DistanceFieldNormal,
	FRDGTextureRef& OutDynamicBentNormalAO)
{
	const bool bUseGlobalDistanceField = UseGlobalDistanceField(Parameters) && Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0;
	const bool bUseObjectDistanceField = UseAOObjectDistanceField();

	const FIntPoint ConeTraceBufferSize = GetBufferSizeForConeTracing();
	const FIntPoint TileListGroupSize = GetTileListGroupSizeForView(View);

	FAOScreenGridResources*& ScreenGridResources = View.ViewState->AOScreenGridResources;

	if (!ScreenGridResources
		|| ScreenGridResources->ScreenGridDimensions != ConeTraceBufferSize
		|| !ScreenGridResources->IsInitialized()
		|| GFastVRamConfig.bDirty)
	{
		if (ScreenGridResources)
		{
			ScreenGridResources->ReleaseResource();
		}
		else
		{
			ScreenGridResources = new FAOScreenGridResources();
		}

		ScreenGridResources->ScreenGridDimensions = ConeTraceBufferSize;

		ScreenGridResources->InitResource();
	}

	{
		FDistanceFieldAOScreenGridParameters* PassParameters = GraphBuilder.AllocParameters<FDistanceFieldAOScreenGridParameters>();
		PassParameters->SceneTextures = SceneTexturesUniformBuffer;
		PassParameters->DistanceFieldNormal = DistanceFieldNormal;

		GraphBuilder.AddPass(
			{},
			PassParameters,
			ERDGPassFlags::Compute | ERDGPassFlags::UntrackedAccess | ERDGPassFlags::NeverCull,
			[&View, &ScreenGridResources, Parameters, DistanceFieldNormal, TileListGroupSize, bUseGlobalDistanceField, bUseObjectDistanceField]
			(FRHICommandListImmediate& RHICmdList)
		{
			ScreenGridResources->AcquireTransientResource();

			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

			float ConeVisibilityClearValue = 1.0f;
			RHICmdList.Transition(FRHITransitionInfo(ScreenGridResources->ScreenGridConeVisibility.UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));
			RHICmdList.ClearUAVUint(ScreenGridResources->ScreenGridConeVisibility.UAV, FUintVector4(*(uint32*)&ConeVisibilityClearValue, 0, 0, 0)); // @todo - ScreenGridConeVisibility should probably be R32_FLOAT format.

			if (bUseGlobalDistanceField)
			{
				SCOPED_DRAW_EVENT(RHICmdList, ConeTraceGlobal);

				const uint32 GroupSizeX = FMath::DivideAndRoundUp(View.ViewRect.Size().X / GAODownsampleFactor / GConeTraceDownsampleFactor, GConeTraceGlobalDFTileSize);
				const uint32 GroupSizeY = FMath::DivideAndRoundUp(View.ViewRect.Size().Y / GAODownsampleFactor / GConeTraceDownsampleFactor, GConeTraceGlobalDFTileSize);

				check(View.GlobalDistanceFieldInfo.Clipmaps.Num() > 0);

				if (bUseObjectDistanceField)
				{
					TShaderMapRef<TConeTraceScreenGridGlobalOcclusionCS<true> > ComputeShader(View.ShaderMap);

					RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
					ComputeShader->SetParameters(RHICmdList, View, TileListGroupSize, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), Parameters, View.GlobalDistanceFieldInfo);
					DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);
					ComputeShader->UnsetParameters(RHICmdList, View);
				}
				else
				{
					TShaderMapRef<TConeTraceScreenGridGlobalOcclusionCS<false> > ComputeShader(View.ShaderMap);

					RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
					ComputeShader->SetParameters(RHICmdList, View, TileListGroupSize, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), Parameters, View.GlobalDistanceFieldInfo);
					DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);
					ComputeShader->UnsetParameters(RHICmdList, View);
				}
			}

			if (bUseObjectDistanceField)
			{
				SCOPED_DRAW_EVENT(RHICmdList, ConeTraceObjects);
				FTileIntersectionResources* TileIntersectionResources = ((FSceneViewState*)View.State)->AOTileIntersectionResources;

				RHICmdList.Transition(FRHITransitionInfo(ScreenGridResources->ScreenGridConeVisibility.UAV, ERHIAccess::ERWBarrier, ERHIAccess::ERWBarrier));

				if (bUseGlobalDistanceField)
				{
					check(View.GlobalDistanceFieldInfo.Clipmaps.Num() > 0);

					{
						TShaderMapRef<TConeTraceScreenGridObjectOcclusionCS<true> > ComputeShader(View.ShaderMap);

						RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
						ComputeShader->SetParameters(RHICmdList, View, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), Parameters, View.GlobalDistanceFieldInfo);
						DispatchIndirectComputeShader(RHICmdList, ComputeShader.GetShader(), TileIntersectionResources->ObjectTilesIndirectArguments.Buffer, 0);
						ComputeShader->UnsetParameters(RHICmdList, View);
					}
				}
				else
				{
					{
						TShaderMapRef<TConeTraceScreenGridObjectOcclusionCS<false> > ComputeShader(View.ShaderMap);

						RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
						ComputeShader->SetParameters(RHICmdList, View, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), Parameters, View.GlobalDistanceFieldInfo);
						DispatchIndirectComputeShader(RHICmdList, ComputeShader.GetShader(), TileIntersectionResources->ObjectTilesIndirectArguments.Buffer, 0);
						ComputeShader->UnsetParameters(RHICmdList, View);
					}
				}
			}

			// Compute heightfield occlusion after heightfield GI, otherwise it self-shadows incorrectly
			View.HeightfieldLightingViewInfo.ComputeOcclusionForScreenGrid(View, RHICmdList, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), *ScreenGridResources, Parameters);

			RHICmdList.Transition(FRHITransitionInfo(ScreenGridResources->ScreenGridConeVisibility.UAV, ERHIAccess::ERWBarrier, ERHIAccess::SRVCompute));
		});
	}

	FRDGTextureRef DownsampledBentNormal = nullptr;

	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(ConeTraceBufferSize, PF_FloatRGBA, FClearValueBinding::None, GFastVRamConfig.DistanceFieldAODownsampledBentNormal | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ShaderResource);
		DownsampledBentNormal = GraphBuilder.CreateTexture(Desc, TEXT("DownsampledBentNormal"));
	}

	{
		const uint32 GroupSizeX = FMath::DivideAndRoundUp(ConeTraceBufferSize.X, GCombineConesSizeX);
		const uint32 GroupSizeY = FMath::DivideAndRoundUp(ConeTraceBufferSize.Y, GCombineConesSizeX);

		TShaderMapRef<FCombineConeVisibilityCS> ComputeShader(View.ShaderMap);

		auto* PassParameters = GraphBuilder.AllocParameters<FDistanceFieldAOScreenGridCombineConeVisibilityParameters>();
		PassParameters->DownsampledBentNormal = DownsampledBentNormal;
		PassParameters->DistanceFieldNormal = DistanceFieldNormal;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CombineCones"),
			PassParameters,
			ERDGPassFlags::Compute,
			[&View, ComputeShader, DistanceFieldNormal, DownsampledBentNormal, GroupSizeX, GroupSizeY](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
			ComputeShader->SetParameters(RHICmdList, View, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), DownsampledBentNormal->GetPooledRenderTarget()->GetRenderTargetItem());
			DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);
		});
	}

	if (IsTransientResourceBufferAliasingEnabled())
	{
		AddPass(GraphBuilder, [&ScreenGridResources](FRHICommandList&)
		{
			ScreenGridResources->DiscardTransientResource();
		});
	}

	PostProcessBentNormalAOScreenGrid(
		GraphBuilder,
		Parameters,
		View,
		SceneTexturesUniformBuffer,
		VelocityTexture,
		DownsampledBentNormal,
		DistanceFieldNormal,
		OutDynamicBentNormalAO);
}
