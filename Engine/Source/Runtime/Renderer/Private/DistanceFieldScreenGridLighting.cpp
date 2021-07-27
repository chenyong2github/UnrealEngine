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

class FConeTraceScreenGridObjectOcclusionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FConeTraceScreenGridObjectOcclusionCS);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, DistanceFieldCulledObjectBuffers)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlas)
		SHADER_PARAMETER_STRUCT_INCLUDE(FTileIntersectionParameters, TileIntersectionParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAOScreenGridParameters, AOScreenGridParameters)
		RDG_BUFFER_ACCESS(ObjectTilesIndirectArguments, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVCompute)
	END_SHADER_PARAMETER_STRUCT()

	class FUseGlobalDistanceField : SHADER_PERMUTATION_BOOL("USE_GLOBAL_DISTANCE_FIELD");
	using FPermutationDomain = TShaderPermutationDomain<FUseGlobalDistanceField>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileDistanceFieldShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		TileIntersectionModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);

		// To reduce shader compile time of compute shaders with shared memory, doesn't have an impact on generated code with current compiler (June 2010 DX SDK)
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	FConeTraceScreenGridObjectOcclusionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
		AOParameters.Bind(Initializer.ParameterMap);
		ScreenGridParameters.Bind(Initializer.ParameterMap);
		GlobalDistanceFieldParameters.Bind(Initializer.ParameterMap);
		TanConeHalfAngle.Bind(Initializer.ParameterMap, TEXT("TanConeHalfAngle"));
		BentNormalNormalizeFactor.Bind(Initializer.ParameterMap, TEXT("BentNormalNormalizeFactor"));
	}

	FConeTraceScreenGridObjectOcclusionCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		FRHITexture* DistanceFieldNormal,  
		const FDistanceFieldAOParameters& Parameters,
		bool bUseGlobalDistanceField,
		const FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

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

		extern float GAOConeHalfAngle;
		SetShaderValue(RHICmdList, ShaderRHI, TanConeHalfAngle, FMath::Tan(GAOConeHalfAngle));

		FVector UnoccludedVector(0);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			UnoccludedVector += SampleDirections[SampleIndex];
		}

		// LWC_TODO: Was float BentNormalNormalizeFactorValue = 1.0f / (UnoccludedVector / NumConeSampleDirections).Size();
		// This causes "error C4723: potential divide by 0" in msvc, implying the compiler has managed to evaluate (UnoccludedVector / NumConeSampleDirections).Size() as 0 at compile time.
		// Ensuring Size() is called in a seperate line stops the warning, but this needs investigating. Clang seems happy with it. Possible compiler bug?
		float ConeSampleAverageSize = (UnoccludedVector / NumConeSampleDirections).Size();
		float BentNormalNormalizeFactorValue = ConeSampleAverageSize ? 1.0f / ConeSampleAverageSize : 0.f;
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalNormalizeFactor, BentNormalNormalizeFactorValue);
	}

private:
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FScreenGridParameters, ScreenGridParameters);
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
	LAYOUT_FIELD(FShaderParameter, TanConeHalfAngle);
	LAYOUT_FIELD(FShaderParameter, BentNormalNormalizeFactor);
};

IMPLEMENT_GLOBAL_SHADER(FConeTraceScreenGridObjectOcclusionCS, "/Engine/Private/DistanceFieldScreenGridLighting.usf", "ConeTraceObjectOcclusionCS", SF_Compute);

const int32 GConeTraceGlobalDFTileSize = 8;

class FConeTraceScreenGridGlobalOcclusionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FConeTraceScreenGridGlobalOcclusionCS);
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, DistanceFieldCulledObjectBuffers)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlas)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAOScreenGridParameters, AOScreenGridParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<FVector4>, TileConeDepthRanges)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVCompute)
	END_SHADER_PARAMETER_STRUCT()

	class FConeTraceObjects : SHADER_PERMUTATION_BOOL("CONE_TRACE_OBJECTS");
	using FPermutationDomain = TShaderPermutationDomain<FConeTraceObjects>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileDistanceFieldShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CONE_TRACE_GLOBAL_DISPATCH_SIZEX"), GConeTraceGlobalDFTileSize);
		OutEnvironment.SetDefine(TEXT("OUTPUT_VISIBILITY_DIRECTLY"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("TRACE_DOWNSAMPLE_FACTOR"), GConeTraceDownsampleFactor);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_DISTANCE_FIELD"), TEXT("1"));

		// To reduce shader compile time of compute shaders with shared memory, doesn't have an impact on generated code with current compiler (June 2010 DX SDK)
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	FConeTraceScreenGridGlobalOcclusionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
		AOParameters.Bind(Initializer.ParameterMap);
		ScreenGridParameters.Bind(Initializer.ParameterMap);
		GlobalDistanceFieldParameters.Bind(Initializer.ParameterMap);
		TileListGroupSize.Bind(Initializer.ParameterMap, TEXT("TileListGroupSize"));
		TanConeHalfAngle.Bind(Initializer.ParameterMap, TEXT("TanConeHalfAngle"));
		BentNormalNormalizeFactor.Bind(Initializer.ParameterMap, TEXT("BentNormalNormalizeFactor"));
	}

	FConeTraceScreenGridGlobalOcclusionCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View,
		FIntPoint TileListGroupSizeValue, 
		FRHITexture* DistanceFieldNormal, 
		const FDistanceFieldAOParameters& Parameters,
		const FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

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

		SetShaderValue(RHICmdList, ShaderRHI, TileListGroupSize, TileListGroupSizeValue);

		extern float GAOConeHalfAngle;
		SetShaderValue(RHICmdList, ShaderRHI, TanConeHalfAngle, FMath::Tan(GAOConeHalfAngle));

		FVector UnoccludedVector(0);

		for (int32 SampleIndex = 0; SampleIndex < NumConeSampleDirections; SampleIndex++)
		{
			UnoccludedVector += SampleDirections[SampleIndex];
		}

		float ConeSampleAverageSize = (UnoccludedVector / NumConeSampleDirections).Size();
		float BentNormalNormalizeFactorValue = ConeSampleAverageSize ? 1.0f / ConeSampleAverageSize : 0.f;
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalNormalizeFactor, BentNormalNormalizeFactorValue);
	}

private:
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FScreenGridParameters, ScreenGridParameters);
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
	LAYOUT_FIELD(FShaderParameter, TileListGroupSize);
	LAYOUT_FIELD(FShaderParameter, TanConeHalfAngle);
	LAYOUT_FIELD(FShaderParameter, BentNormalNormalizeFactor);
};

IMPLEMENT_GLOBAL_SHADER(FConeTraceScreenGridGlobalOcclusionCS, "/Engine/Private/DistanceFieldScreenGridLighting.usf", "ConeTraceGlobalOcclusionCS", SF_Compute);

const int32 GCombineConesSizeX = 8;

class FCombineConeVisibilityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCombineConeVisibilityCS);
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FAOScreenGridParameters, AOScreenGridParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWDistanceFieldBentNormal)
		RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVCompute)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileDistanceFieldShaders(Parameters.Platform);
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
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
		ScreenGridParameters.Bind(Initializer.ParameterMap);
		BentNormalNormalizeFactor.Bind(Initializer.ParameterMap, TEXT("BentNormalNormalizeFactor"));
		ConeBufferMax.Bind(Initializer.ParameterMap, TEXT("ConeBufferMax"));
		DFNormalBufferUVMax.Bind(Initializer.ParameterMap, TEXT("DFNormalBufferUVMax"));
	}

	FCombineConeVisibilityCS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FRHITexture* DistanceFieldNormal)
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

		float ConeSampleAverageSize = (UnoccludedVector / NumConeSampleDirections).Size();
		float BentNormalNormalizeFactorValue = ConeSampleAverageSize ? 1.0f / ConeSampleAverageSize : 0.f;
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalNormalizeFactor, BentNormalNormalizeFactorValue);

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
};

IMPLEMENT_GLOBAL_SHADER(FCombineConeVisibilityCS, "/Engine/Private/DistanceFieldScreenGridLighting.usf", "CombineConeVisibilityCS", SF_Compute);

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

void FDeferredShadingSceneRenderer::RenderDistanceFieldAOScreenGrid(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FViewInfo& View,
	const FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
	FRDGBufferRef ObjectTilesIndirectArguments,
	const FTileIntersectionParameters& TileIntersectionParameters,
	const FDistanceFieldAOParameters& Parameters,
	FRDGTextureRef DistanceFieldNormal,
	FRDGTextureRef& OutDynamicBentNormalAO)
{
	const bool bUseGlobalDistanceField = UseGlobalDistanceField(Parameters) && Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0;
	const bool bUseObjectDistanceField = UseAOObjectDistanceField();

	const FIntPoint ConeTraceBufferSize = GetBufferSizeForConeTracing();
	const FIntPoint TileListGroupSize = GetTileListGroupSizeForView(View);

	FAOScreenGridParameters AOScreenGridParameters;

	{
		AOScreenGridParameters.ScreenGridConeVisibilitySize = ConeTraceBufferSize;
		//@todo - 2d textures
		//@todo - FastVRamFlag
		// @todo - ScreenGridConeVisibility should probably be R32_FLOAT format.
		FRDGBufferRef ScreenGridConeVisibility = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumConeSampleDirections * ConeTraceBufferSize.X * ConeTraceBufferSize.Y), TEXT("ScreenGridConeVisibility"));
		AOScreenGridParameters.RWScreenGridConeVisibility = GraphBuilder.CreateUAV(ScreenGridConeVisibility, PF_R32_UINT);
		AOScreenGridParameters.ScreenGridConeVisibility = GraphBuilder.CreateSRV(ScreenGridConeVisibility, PF_R32_UINT);
	}

	float ConeVisibilityClearValue = 1.0f;
	AddClearUAVPass(GraphBuilder, AOScreenGridParameters.RWScreenGridConeVisibility, *(uint32*)&ConeVisibilityClearValue);

	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	// Note: no transition, want to overlap object cone tracing and global DF cone tracing since both shaders use atomics to ScreenGridConeVisibility

	if (bUseGlobalDistanceField)
	{
		check(View.GlobalDistanceFieldInfo.Clipmaps.Num() > 0);

		auto* PassParameters = GraphBuilder.AllocParameters<FConeTraceScreenGridGlobalOcclusionCS::FParameters>();
		PassParameters->DistanceFieldCulledObjectBuffers = CulledObjectBufferParameters;
		PassParameters->DistanceFieldAtlas = DistanceField::SetupAtlasParameters(DistanceFieldSceneData);
		PassParameters->AOScreenGridParameters = AOScreenGridParameters;
		PassParameters->TileConeDepthRanges = TileIntersectionParameters.TileConeDepthRanges;
		PassParameters->SceneTextures = SceneTextures.UniformBuffer;
		PassParameters->DistanceFieldNormal = DistanceFieldNormal;

		FConeTraceScreenGridGlobalOcclusionCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FConeTraceScreenGridGlobalOcclusionCS::FConeTraceObjects>(bUseObjectDistanceField);

		auto ComputeShader = View.ShaderMap->GetShader<FConeTraceScreenGridGlobalOcclusionCS>(PermutationVector);

		ClearUnusedGraphResources(ComputeShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ConeTraceGlobal"),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShader, &View, Parameters, DistanceFieldNormal, TileListGroupSize, bUseObjectDistanceField](FRHICommandList& RHICmdList)
			{
				const uint32 GroupSizeX = FMath::DivideAndRoundUp(View.ViewRect.Size().X / GAODownsampleFactor / GConeTraceDownsampleFactor, GConeTraceGlobalDFTileSize);
				const uint32 GroupSizeY = FMath::DivideAndRoundUp(View.ViewRect.Size().Y / GAODownsampleFactor / GConeTraceDownsampleFactor, GConeTraceGlobalDFTileSize);

				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

				ComputeShader->SetParameters(RHICmdList, View, TileListGroupSize, DistanceFieldNormal->GetRHI(), Parameters, View.GlobalDistanceFieldInfo);
				SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), *PassParameters);

				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);

				UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());
			});
	}

	if (bUseObjectDistanceField)
	{
		check(!bUseGlobalDistanceField || View.GlobalDistanceFieldInfo.Clipmaps.Num() > 0);

		auto* PassParameters = GraphBuilder.AllocParameters<FConeTraceScreenGridObjectOcclusionCS::FParameters>();
		PassParameters->DistanceFieldCulledObjectBuffers = CulledObjectBufferParameters;
		PassParameters->DistanceFieldAtlas = DistanceField::SetupAtlasParameters(DistanceFieldSceneData);
		PassParameters->TileIntersectionParameters = TileIntersectionParameters;
		PassParameters->AOScreenGridParameters = AOScreenGridParameters;
		PassParameters->ObjectTilesIndirectArguments = ObjectTilesIndirectArguments;
		PassParameters->SceneTextures = SceneTextures.UniformBuffer;
		PassParameters->DistanceFieldNormal = DistanceFieldNormal;

		FConeTraceScreenGridObjectOcclusionCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FConeTraceScreenGridObjectOcclusionCS::FUseGlobalDistanceField>(bUseGlobalDistanceField);

		auto ComputeShader = View.ShaderMap->GetShader<FConeTraceScreenGridObjectOcclusionCS>(PermutationVector);

		ClearUnusedGraphResources(ComputeShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ConeTraceObjects"),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShader, &View, Parameters, DistanceFieldNormal, bUseGlobalDistanceField, ObjectTilesIndirectArguments](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

				ComputeShader->SetParameters(RHICmdList, View, DistanceFieldNormal->GetRHI(), Parameters, bUseGlobalDistanceField, View.GlobalDistanceFieldInfo);
				SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), *PassParameters);

				DispatchIndirectComputeShader(RHICmdList, ComputeShader.GetShader(), ObjectTilesIndirectArguments->GetIndirectRHICallBuffer(), 0);

				UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());
			});
	}

	// Compute heightfield occlusion after heightfield GI, otherwise it self-shadows incorrectly
	View.HeightfieldLightingViewInfo.ComputeOcclusionForScreenGrid(GraphBuilder, View, SceneTextures, DistanceFieldNormal, AOScreenGridParameters, Parameters);

	FRDGTextureRef DownsampledBentNormal = nullptr;

	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(ConeTraceBufferSize, PF_FloatRGBA, FClearValueBinding::None, GFastVRamConfig.DistanceFieldAODownsampledBentNormal | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ShaderResource);
		DownsampledBentNormal = GraphBuilder.CreateTexture(Desc, TEXT("DownsampledBentNormal"));
	}

	{
		const uint32 GroupSizeX = FMath::DivideAndRoundUp(ConeTraceBufferSize.X, GCombineConesSizeX);
		const uint32 GroupSizeY = FMath::DivideAndRoundUp(ConeTraceBufferSize.Y, GCombineConesSizeX);

		auto* PassParameters = GraphBuilder.AllocParameters<FCombineConeVisibilityCS::FParameters>();
		PassParameters->AOScreenGridParameters = AOScreenGridParameters;
		PassParameters->RWDistanceFieldBentNormal = GraphBuilder.CreateUAV(DownsampledBentNormal);
		PassParameters->DistanceFieldNormal = DistanceFieldNormal;

		auto ComputeShader = View.ShaderMap->GetShader<FCombineConeVisibilityCS>();

		ClearUnusedGraphResources(ComputeShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CombineCones"),
			PassParameters,
			ERDGPassFlags::Compute,
			[&View, ComputeShader, PassParameters, GroupSizeX, GroupSizeY](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

				ComputeShader->SetParameters(RHICmdList, View, PassParameters->DistanceFieldNormal->GetRHI());
				SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), *PassParameters);

				DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);

				UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());
			});
	}

	PostProcessBentNormalAOScreenGrid(
		GraphBuilder,
		Parameters,
		View,
		SceneTextures.UniformBuffer,
		SceneTextures.Velocity,
		DownsampledBentNormal,
		DistanceFieldNormal,
		OutDynamicBentNormalAO);
}
