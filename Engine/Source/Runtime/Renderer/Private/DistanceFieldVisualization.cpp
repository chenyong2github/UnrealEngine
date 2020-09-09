// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldVisualization.cpp
=============================================================================*/

#include "DistanceFieldAmbientOcclusion.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "DistanceFieldLightingShared.h"
#include "ScreenRendering.h"
#include "DistanceFieldLightingPost.h"
#include "OneColorShader.h"
#include "GlobalDistanceField.h"
#include "FXSystem.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PipelineStateCache.h"

class FVisualizeMeshDistanceFieldCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisualizeMeshDistanceFieldCS);

	class FUseGlobalDistanceFieldDim : SHADER_PERMUTATION_BOOL("USE_GLOBAL_DISTANCE_FIELD");
	using FPermutationDomain = TShaderPermutationDomain<FUseGlobalDistanceFieldDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWVisualizeMeshDistanceFields)
		SHADER_PARAMETER(FVector2D, NumGroups)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDistanceFieldAOTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDistanceFieldAOTileSizeY);
	}

	FVisualizeMeshDistanceFieldCS() = default;
	FVisualizeMeshDistanceFieldCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap);
		ObjectParameters.Bind(Initializer.ParameterMap);
		AOParameters.Bind(Initializer.ParameterMap);
		GlobalDistanceFieldParameters.Bind(Initializer.ParameterMap);
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		const FDistanceFieldAOParameters& Parameters,
		const FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

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

		if (GlobalDistanceFieldParameters.IsBound())
		{
			GlobalDistanceFieldParameters.Set(RHICmdList, ShaderRHI, GlobalDistanceFieldInfo.ParameterData);
		}
	}

private:
	LAYOUT_FIELD((TDistanceFieldCulledObjectBufferParameters<DFPT_SignedDistanceField>), ObjectParameters);
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeMeshDistanceFieldCS, "/Engine/Private/DistanceFieldVisualization.usf", "VisualizeMeshDistanceFieldCS", SF_Compute);

class FVisualizeDistanceFieldUpsamplePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisualizeDistanceFieldUpsamplePS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeDistanceFieldUpsamplePS, FGlobalShader);
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisualizeDistanceFieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisualizeDistanceFieldSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeDistanceFieldUpsamplePS, "/Engine/Private/DistanceFieldVisualization.usf", "VisualizeDistanceFieldUpsamplePS", SF_Pixel);

void FDeferredShadingSceneRenderer::RenderMeshDistanceFieldVisualization(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FDistanceFieldAOParameters& Parameters)
{
	const FViewInfo& FirstView = Views[0];

	if (!UseDistanceFieldAO()
		|| FeatureLevel < ERHIFeatureLevel::SM5
		|| !DoesPlatformSupportDistanceFieldAO(FirstView.GetShaderPlatform())
		|| Views.Num() != 1
		|| !GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI
		|| Scene->DistanceFieldSceneData.NumObjectsInBuffer == 0)
	{
		return;
	}

	check(!Scene->DistanceFieldSceneData.HasPendingOperations());

	QUICK_SCOPE_CYCLE_COUNTER(STAT_AOIssueGPUWork);

	const bool bUseGlobalDistanceField = UseGlobalDistanceField(Parameters) && FirstView.Family->EngineShowFlags.VisualizeGlobalDistanceField;

	RDG_EVENT_SCOPE(GraphBuilder, "VisualizeMeshDistanceFields");

	CullObjectsToView(GraphBuilder, Scene, FirstView, Parameters, GAOCulledObjectBuffers);

	FRDGTextureRef VisualizeResultTexture = nullptr;

	{
		const FIntPoint BufferSize = GetBufferSizeForAO();
		const FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(BufferSize, PF_FloatRGBA, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_UAV));
		VisualizeResultTexture = GraphBuilder.CreateTexture(Desc, TEXT("VisualizeDistanceField"));
	}

	FVisualizeMeshDistanceFieldCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FVisualizeMeshDistanceFieldCS::FUseGlobalDistanceFieldDim>(bUseGlobalDistanceField);
	TShaderMapRef<FVisualizeMeshDistanceFieldCS> ComputeShader(FirstView.ShaderMap, PermutationVector);

	for (const FViewInfo& View : Views)
	{
		uint32 GroupSizeX = FMath::DivideAndRoundUp(View.ViewRect.Size().X / GAODownsampleFactor, GDistanceFieldAOTileSizeX);
		uint32 GroupSizeY = FMath::DivideAndRoundUp(View.ViewRect.Size().Y / GAODownsampleFactor, GDistanceFieldAOTileSizeY);

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		auto* PassParameters = GraphBuilder.AllocParameters<FVisualizeMeshDistanceFieldCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->NumGroups = FVector2D(GroupSizeX, GroupSizeY);
		PassParameters->SceneTextures = SceneTexturesUniformBuffer;
		PassParameters->RWVisualizeMeshDistanceFields = GraphBuilder.CreateUAV(VisualizeResultTexture);

		check(!bUseGlobalDistanceField || View.GlobalDistanceFieldInfo.Clipmaps.Num() > 0);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("VisualizeMeshDistanceFieldCS"),
			PassParameters,
			ERDGPassFlags::Compute,
			[&View, Parameters, ComputeShader, PassParameters, GroupSizeX, GroupSizeY](FRHICommandList& RHICmdList)
		{
			FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
			RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
			SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *PassParameters);
			ComputeShader->SetParameters(RHICmdList, Parameters, View.GlobalDistanceFieldInfo);
			DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSizeX, GroupSizeY, 1);
			UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
		});
	}

	if (IsTransientResourceBufferAliasingEnabled())
	{
		AddPass(GraphBuilder, [](FRHICommandList&)
		{
			GAOCulledObjectBuffers.Buffers.DiscardTransientResource();
		});
	}

	TShaderMapRef<FVisualizeDistanceFieldUpsamplePS> PixelShader(FirstView.ShaderMap);

	for (const FViewInfo& View : Views)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FVisualizeDistanceFieldUpsamplePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->VisualizeDistanceFieldTexture = VisualizeResultTexture;
		PassParameters->VisualizeDistanceFieldSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilWrite);

		const FScreenPassTextureViewport InputViewport(VisualizeResultTexture, GetDownscaledRect(View.ViewRect, GAODownsampleFactor));
		const FScreenPassTextureViewport OutputViewport(SceneColorTexture, View.ViewRect);

		AddDrawScreenPass(GraphBuilder, {}, View, OutputViewport, InputViewport, PixelShader, PassParameters);
	}
}