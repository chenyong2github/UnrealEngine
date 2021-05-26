// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugProbeRendering.h"
#include "PixelShaderUtils.h"
#include "ShaderParameterStruct.h"


// Changing this causes a full shader recompile
static TAutoConsoleVariable<int32> CVarDebugProbes(
	TEXT("r.DebugProbes"),
	0,
	TEXT("Enables debug probes rendering to visualise diffuse/specular lighting on simple sphere scattered in the world.") \
	TEXT(" 0: disabled.\n")
	TEXT(" 1: camera probes only.\n")
	TEXT(" 2: world probes only.\n")
	TEXT(" 3: camera and world probes.\n")
	,
	ECVF_RenderThreadSafe);


DECLARE_GPU_STAT(StampDeferredDebugProbe);


class FStampDeferredDebugProbePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStampDeferredDebugProbePS);
	SHADER_USE_PARAMETER_STRUCT(FStampDeferredDebugProbePS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(int32, DebugProbesMode)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FStampDeferredDebugProbePS, "/Engine/Private/DebugProbes.usf", "MainPS", SF_Pixel);

 
template<bool bEnableDepthWrite>
static void CommonStampDeferredDebugProbeDrawCall(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	FStampDeferredDebugProbePS::FParameters* PassParameters)
{
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->DebugProbesMode = FMath::Clamp(CVarDebugProbes.GetValueOnRenderThread(), 0, 3);

	FStampDeferredDebugProbePS::FPermutationDomain PermutationVector;
	TShaderMapRef<FStampDeferredDebugProbePS> PixelShader(View.ShaderMap, PermutationVector);

	FPixelShaderUtils::AddFullscreenPass<FStampDeferredDebugProbePS>(
		GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("StampDeferredDebugProbePS"),
		PixelShader, PassParameters, View.ViewRect,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<FM_Solid, CM_None>::GetRHI(),
		TStaticDepthStencilState<bEnableDepthWrite, CF_DepthNearOrEqual>::GetRHI());
}

void StampDeferredDebugProbeDepthPS(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FRDGTextureRef SceneDepthTexture)
{
	RDG_EVENT_SCOPE(GraphBuilder, "StampDeferredDebugProbeDepth");
	RDG_GPU_STAT_SCOPE(GraphBuilder, StampDeferredDebugProbe);

	if (CVarDebugProbes.GetValueOnRenderThread() <= 0)
	{
		return;
	}

	for (const FViewInfo& View : Views)
	{
		FStampDeferredDebugProbePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStampDeferredDebugProbePS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

		CommonStampDeferredDebugProbeDrawCall<true>(GraphBuilder, View, PassParameters);
	}
}

void StampDeferredDebugProbeMaterialPS(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FRenderTargetBindingSlots& BasePassRenderTargets)
{
	RDG_EVENT_SCOPE(GraphBuilder, "StampDeferredDebugProbeMaterial");
	RDG_GPU_STAT_SCOPE(GraphBuilder, StampDeferredDebugProbe);

	if (CVarDebugProbes.GetValueOnRenderThread() <= 0)
	{
		return;
	}

	for (const FViewInfo& View : Views)
	{
		FStampDeferredDebugProbePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStampDeferredDebugProbePS::FParameters>();
		PassParameters->RenderTargets = BasePassRenderTargets;

		CommonStampDeferredDebugProbeDrawCall<false>(GraphBuilder, View, PassParameters);
	}
}

