// Copyright Epic Games, Inc. All Rights Reserved.


#include "HairStrandsEnvironment.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "SceneRenderTargetParameters.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "PixelShaderUtils.h"
#include "SystemTextures.h"
#include "HairStrandsRendering.h"
#include "ReflectionEnvironment.h"
#include "ScenePrivate.h"
#include "RenderGraphEvent.h"
#include "PostProcess/PostProcessing.h"
#include "GpuDebugRendering.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairScatterSceneLighting = 1;
static FAutoConsoleVariableRef CVarHairScatterSceneLighting(TEXT("r.HairStrands.ScatterSceneLighting"), GHairScatterSceneLighting, TEXT("Enable scene color lighting scattering into hair (valid for short hair only)."));

static int32 GHairSkylightingEnable = 1;
static FAutoConsoleVariableRef CVarHairSkylightingEnable(TEXT("r.HairStrands.SkyLighting"), GHairSkylightingEnable, TEXT("Enable sky lighting on hair."));

static int32 GHairSkyAOEnable = 1;
static FAutoConsoleVariableRef CVarHairSkyAOEnable(TEXT("r.HairStrands.SkyAO"), GHairSkyAOEnable, TEXT("Enable (sky) AO on hair."));

static float GHairSkylightingConeAngle = 3;
static FAutoConsoleVariableRef CVarHairSkylightingConeAngle(TEXT("r.HairStrands.SkyLighting.ConeAngle"), GHairSkylightingConeAngle, TEXT("Cone angle for tracing sky lighting on hair."));

static int32 GHairStrandsSkyLightingSampleCount = 16;
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingSampleCount(TEXT("r.HairStrands.SkyLighting.SampleCount"), GHairStrandsSkyLightingSampleCount, TEXT("Number of samples used for evaluating multiple scattering and visible area (default is set to 16)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyAOSampleCount = 16;
static FAutoConsoleVariableRef CVarHairStrandsSkyAOSampleCount(TEXT("r.HairStrands.SkyAO.SampleCount"), GHairStrandsSkyAOSampleCount, TEXT("Number of samples used for evaluating hair AO (default is set to 16)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GHairStrandsTransmissionDensityScaleFactor = 4;
static FAutoConsoleVariableRef CVarHairStrandsTransmissionDensityScaleFactor(TEXT("r.HairStrands.SkyLighting.TransmissionDensityScale"), GHairStrandsTransmissionDensityScaleFactor, TEXT("Density scale for controlling how much sky lighting is transmitted."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyLightingUseHairCountTexture = 1;
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingUseHairCountTexture(TEXT("r.HairStrands.SkyLighting.UseViewHairCount"), GHairStrandsSkyLightingUseHairCountTexture, TEXT("Use the view hair count texture for estimating background transmitted light (enabled by default)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GHairStrandsSkyAODistanceThreshold = 10;
static float GHairStrandsSkyLightingDistanceThreshold = 10;
static FAutoConsoleVariableRef CVarHairStrandsSkyAOThreshold(TEXT("r.HairStrands.SkyAO.DistanceThreshold"), GHairStrandsSkyAODistanceThreshold, TEXT("Max distance for occlusion search."), ECVF_Scalability | ECVF_RenderThreadSafe);
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingDistanceThreshold(TEXT("r.HairStrands.SkyLighting.DistanceThreshold"), GHairStrandsSkyLightingDistanceThreshold, TEXT("Max distance for occlusion search."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyLighting_IntegrationType = 0;
static FAutoConsoleVariableRef CVarHairStrandsSkyLighting_IntegrationType(TEXT("r.HairStrands.SkyLighting.IntegrationType"), GHairStrandsSkyLighting_IntegrationType, TEXT("Hair env. lighting integration type (0:Adhoc, 1:Uniform."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyLighting_DebugSample = 0;
static FAutoConsoleVariableRef CVarHairStrandsSkyLighting_DebugSample(TEXT("r.HairStrands.SkyLighting.DebugSample"), GHairStrandsSkyLighting_DebugSample, TEXT("Enable debug view for visualizing sample used for the sky integration"), ECVF_Scalability | ECVF_RenderThreadSafe);

///////////////////////////////////////////////////////////////////////////////////////////////////

enum class EHairLightingIntegrationType : uint8
{
	SceneColor = 0,
	AdHoc = 1,
	Uniform = 2,
	SH = 3
};

bool GetHairStrandsSkyLightingEnable() { return GHairSkylightingEnable > 0; }
static bool GetHairStrandsSkyAOEnable() { return GHairSkyAOEnable > 0; }
static float GetHairStrandsSkyLightingConeAngle() { return FMath::Max(0.f, GHairSkylightingConeAngle); }

DECLARE_GPU_STAT_NAMED(HairStrandsReflectionEnvironment, TEXT("Hair Strands Reflection Environment"));

///////////////////////////////////////////////////////////////////////////////////////////////////
// AO
class FHairEnvironmentAO : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairEnvironmentAO);
	SHADER_USE_PARAMETER_STRUCT(FHairEnvironmentAO, FGlobalShader)

	class FSampleSet : SHADER_PERMUTATION_INT("PERMUTATION_SAMPLESET", 2);
	using FPermutationDomain = TShaderPermutationDomain<FSampleSet>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(uint32, Voxel_MacroGroupId)
		SHADER_PARAMETER(float, Voxel_TanConeAngle)
		SHADER_PARAMETER(float, AO_Power)
		SHADER_PARAMETER(float, AO_Intensity)
		SHADER_PARAMETER(uint32, AO_SampleCount)
		SHADER_PARAMETER(float, AO_DistanceThreshold)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)

		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentAO, "/Engine/Private/HairStrands/HairStrandsEnvironmentAO.usf", "MainPS", SF_Pixel);

static void AddHairStrandsEnvironmentAOPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FHairStrandsMacroGroupData& MacroGroupData,
	FRDGTextureRef Output)
{
	check(Output);
	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	FHairEnvironmentAO::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairEnvironmentAO::FParameters>();
	PassParameters->Voxel_MacroGroupId = MacroGroupData.MacroGroupId;
	PassParameters->Voxel_TanConeAngle = FMath::Tan(FMath::DegreesToRadians(GetHairStrandsSkyLightingConeAngle()));
	PassParameters->SceneTextures = SceneTextures;
	PassParameters->VirtualVoxel = MacroGroupDatas.VirtualVoxelResources.UniformBuffer;

	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->HairCategorizationTexture = VisibilityData.CategorizationTexture;
	const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;
	PassParameters->AO_Power = Settings.AmbientOcclusionPower;
	PassParameters->AO_Intensity = Settings.AmbientOcclusionIntensity;
	PassParameters->AO_SampleCount = FMath::Max(uint32(GHairStrandsSkyAOSampleCount), 1u);
	PassParameters->AO_DistanceThreshold = FMath::Max(GHairStrandsSkyAODistanceThreshold, 1.f);
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ELoad);

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
	}

	FHairEnvironmentAO::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairEnvironmentAO::FSampleSet>(PassParameters->AO_SampleCount <= 16 ? 0 : 1);

	TShaderMapRef<FHairEnvironmentAO> PixelShader(View.ShaderMap, PermutationVector);
	ClearUnusedGraphResources(PixelShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsAO %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, &View, PixelShader](FRHICommandList& InRHICmdList)
	{
		InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, PixelShader, GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Min, BF_SourceColor, BF_DestColor, BO_Add, BF_Zero, BF_DestAlpha>::GetRHI();
		SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
		SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
		FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairEnvironmentLighting
{
public:
	class FIntegrationType	: SHADER_PERMUTATION_INT("PERMUTATION_INTEGRATION_TYPE", 4);
	class FDebug			: SHADER_PERMUTATION_INT("PERMUTATION_DEBUG", 2); 
	using FPermutationDomain = TShaderPermutationDomain<FIntegrationType, FDebug>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsHairStrandsSupported(Parameters.Platform))
		{
			return false;
		}

		return true;
	}

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(uint32, Voxel_MacroGroupId)
		SHADER_PARAMETER(float, Voxel_TanConeAngle)

		SHADER_PARAMETER(uint32, MaxVisibilityNodeCount)
		SHADER_PARAMETER(uint32, MultipleScatterSampleCount)
		SHADER_PARAMETER(uint32, HairComponents)
		SHADER_PARAMETER(float,  HairDualScatteringRoughnessOverride)
		SHADER_PARAMETER(float, TransmissionDensityScaleFactor)
		SHADER_PARAMETER(float, HairDistanceThreshold)
		SHADER_PARAMETER(uint32, bHairUseViewHairCount)
		SHADER_PARAMETER(FIntPoint, MaxViewportResolution)

		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityNodeCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeCoord)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairEnergyLUTTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairScatteringLUTTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HairLUTSampler)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutLightingBuffer)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)
	END_SHADER_PARAMETER_STRUCT()
};

class FHairEnvironmentLightingVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairEnvironmentLightingVS);
	SHADER_USE_PARAMETER_STRUCT(FHairEnvironmentLightingVS, FGlobalShader)
	//using FPermutationDomain = FHairEnvironmentLighting::FPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairEnvironmentLighting::FParameters, Common)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FHairEnvironmentLighting::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LIGHTING_VS"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

class FHairEnvironmentLightingPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairEnvironmentLightingPS);
	SHADER_USE_PARAMETER_STRUCT(FHairEnvironmentLightingPS, FGlobalShader)
	using FPermutationDomain = FHairEnvironmentLighting::FPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairEnvironmentLighting::FParameters, Common)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairStrandsDebugData::FWriteParameters, DebugData)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FHairEnvironmentLighting::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LIGHTING_PS"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentLightingPS, "/Engine/Private/HairStrands/HairStrandsEnvironmentLighting.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentLightingVS, "/Engine/Private/HairStrands/HairStrandsEnvironmentLighting.usf", "MainVS", SF_Vertex);

static void AddHairStrandsEnvironmentLightingPassPS(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FHairStrandsMacroGroupData& MacroGroupData,
	const FRDGTextureRef SceneColorTexture,
	FHairStrandsDebugData::Data* DebugData)
{
	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	check(MacroGroupDatas.VirtualVoxelResources.IsValid());

	// Render the reflection environment with tiled deferred culling
	const bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
	const bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);
	FHairEnvironmentLightingPS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FHairEnvironmentLightingPS::FParameters>();
	FHairEnvironmentLighting::FParameters* PassParameters = &ParametersPS->Common;

	const FHairLUT InHairLUT = GetHairLUT(GraphBuilder, View);
	PassParameters->HairEnergyLUTTexture = InHairLUT.Textures[HairLUTType_MeanEnergy];
	PassParameters->HairScatteringLUTTexture = InHairLUT.Textures[HairLUTType_DualScattering];
	PassParameters->HairLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->Voxel_MacroGroupId = MacroGroupData.MacroGroupId;

	EHairLightingIntegrationType IntegrationType = EHairLightingIntegrationType::AdHoc;
	const bool bUseSceneColor = SceneColorTexture != nullptr;
	if (bUseSceneColor)
	{
		IntegrationType = EHairLightingIntegrationType::SceneColor;
		PassParameters->SceneColorTexture = SceneColorTexture;
		PassParameters->HairCategorizationTexture = VisibilityData.CategorizationTexture;
	}
	else
	{
		switch (GHairStrandsSkyLighting_IntegrationType)
		{
			case 0: IntegrationType = EHairLightingIntegrationType::AdHoc; break;
			case 1: IntegrationType = EHairLightingIntegrationType::Uniform; break;
			case 2: IntegrationType = EHairLightingIntegrationType::SH; break;
		}
	}

	PassParameters->MaxViewportResolution = VisibilityData.SampleLightingViewportResolution;
	PassParameters->HairVisibilityNodeCount = VisibilityData.NodeCount;
	PassParameters->Voxel_TanConeAngle = FMath::Tan(FMath::DegreesToRadians(GetHairStrandsSkyLightingConeAngle()));
	PassParameters->HairDistanceThreshold = FMath::Max(GHairStrandsSkyLightingDistanceThreshold, 1.f);
	PassParameters->bHairUseViewHairCount = VisibilityData.ViewHairCountTexture && GHairStrandsSkyLightingUseHairCountTexture ? 1 : 0;
	PassParameters->MaxVisibilityNodeCount = VisibilityData.NodeData->Desc.NumElements;
	PassParameters->MultipleScatterSampleCount = FMath::Max(uint32(GHairStrandsSkyLightingSampleCount), 1u);
	PassParameters->HairComponents = ToBitfield(GetHairComponents());
	PassParameters->HairDualScatteringRoughnessOverride = GetHairDualScatteringRoughnessOverride();
	PassParameters->TransmissionDensityScaleFactor = FMath::Max(0.f, GHairStrandsTransmissionDensityScaleFactor);
	PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->HairCountTexture = VisibilityData.ViewHairCountTexture;
	PassParameters->SceneTextures = SceneTextures;
	PassParameters->VirtualVoxel = MacroGroupDatas.VirtualVoxelResources.UniformBuffer;
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
	{
		FReflectionUniformParameters ReflectionUniformParameters;
		SetupReflectionUniformParameters(View, ReflectionUniformParameters);
		PassParameters->ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
	}
	PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
	PassParameters->OutLightingBuffer = nullptr;

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, ParametersPS->ShaderDrawParameters);
	}

	if (DebugData)
	{
		FHairStrandsDebugData::SetParameters(GraphBuilder, *DebugData, ParametersPS->DebugData);
	}

	// Bind hair data
	PassParameters->HairVisibilityNodeData = GraphBuilder.CreateSRV(VisibilityData.NodeData);
	PassParameters->HairVisibilityNodeCoord = GraphBuilder.CreateSRV(VisibilityData.NodeCoord);

	FHairEnvironmentLightingPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairEnvironmentLighting::FIntegrationType>(uint32(IntegrationType));
	PermutationVector.Set<FHairEnvironmentLighting::FDebug>(DebugData ? 1 : 0);
	PermutationVector = FHairEnvironmentLighting::RemapPermutation(PermutationVector);

	FIntPoint ViewportResolution = VisibilityData.SampleLightingViewportResolution;
	const FViewInfo* CapturedView = &View;
	TShaderMapRef<FHairEnvironmentLightingVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairEnvironmentLightingPS> PixelShader(View.ShaderMap, PermutationVector);

	check(VisibilityData.SampleLightingBuffer);
	ParametersPS->RenderTargets[0] = FRenderTargetBinding(VisibilityData.SampleLightingBuffer, ERenderTargetLoadAction::ELoad);

	GraphBuilder.AddPass(
		bUseSceneColor ? RDG_EVENT_NAME("HairEnvSceneScatterPS") : RDG_EVENT_NAME("HairEnvLightingPS"),
		ParametersPS,
		ERDGPassFlags::Raster,
		[ParametersPS, VertexShader, PixelShader, ViewportResolution, CapturedView](FRHICommandList& RHICmdList)
	{
		FHairEnvironmentLightingVS::FParameters ParametersVS;
		ParametersVS.Common = ParametersPS->Common;

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);

		RHICmdList.SetViewport(0, 0, 0.0f, ViewportResolution.X, ViewportResolution.Y, 1.0f);
		RHICmdList.SetStreamSource(0, nullptr, 0);
		RHICmdList.DrawPrimitive(0, 1, 1);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void RenderHairStrandsSceneColorScattering(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	TArrayView<const FViewInfo> Views,
	FHairStrandsRenderingData* HairDatas)
{
	if (Views.Num() == 0 || !HairDatas || GHairScatterSceneLighting <= 0)
		return;

	for (uint32 ViewIndex = 0; ViewIndex < uint32(Views.Num()); ++ViewIndex)
	{
		check(ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()));
		const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
		const bool bRenderHairLighting = VisibilityData.NodeIndex && VisibilityData.NodeData && HairDatas->MacroGroupsPerViews.Views[ViewIndex].VirtualVoxelResources.IsValid();
		if (!bRenderHairLighting)
		{
			continue;
		}

		if (ViewIndex > uint32(HairDatas->MacroGroupsPerViews.Views.Num()))
			continue;

		const FViewInfo& View = Views[ViewIndex];
		const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];

		bool bNeedScatterSceneLighting = false;
		for (const FHairStrandsMacroGroupData& MacroGroupData : HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas)
		{
			if (MacroGroupData.bNeedScatterSceneLighting)
			{
				bNeedScatterSceneLighting = true;
				break;
			}
		}

		if (bNeedScatterSceneLighting)
		{
			for (const FHairStrandsMacroGroupData& MacroGroupData : HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas)
			{
				if (MacroGroupData.bNeedScatterSceneLighting)
				{
					AddHairStrandsEnvironmentLightingPassPS(GraphBuilder, View, VisibilityData, MacroGroupDatas, MacroGroupData, SceneColorTexture, nullptr);
				}
			}
		}
	}
}

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const uint32 ViewIndex,
	TArrayView<const FViewInfo> Views,
	FHairStrandsRenderingData* HairDatas)
{
	if (!GetHairStrandsSkyLightingEnable() || !HairDatas)
		return;

	struct FHairEnvDebugData
	{
		FRDGBufferRef ShadingPointBuffer = nullptr;
		FRDGBufferRef SampleBuffer = nullptr;
		FRDGBufferRef ShadingPointCounter = nullptr;

		FRDGBufferUAVRef ShadingPointBufferUAV = nullptr;
		FRDGBufferUAVRef SampleBufferUAV = nullptr;
		FRDGBufferUAVRef ShadingPointCounterUAV = nullptr;
	};


	check(ViewIndex < uint32(Views.Num()));
	check(ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()));	
	const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
	const bool bRenderHairLighting = VisibilityData.NodeIndex && VisibilityData.NodeData && HairDatas->MacroGroupsPerViews.Views[ViewIndex].VirtualVoxelResources.IsValid();
	if (!bRenderHairLighting)
	{
		return;
	}

	const bool bDebugSamplingEnable = GHairStrandsSkyLighting_DebugSample > 0;
	FHairStrandsDebugData::Data DebugData;
	if (bDebugSamplingEnable)
	{
		DebugData = FHairStrandsDebugData::CreateData(GraphBuilder);
	}

	const FViewInfo& View = Views[ViewIndex];
	const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];
	for (const FHairStrandsMacroGroupData& MacroGroupData : HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas)
	{
		AddHairStrandsEnvironmentLightingPassPS(GraphBuilder, View, VisibilityData, MacroGroupDatas, MacroGroupData, nullptr, bDebugSamplingEnable ? &DebugData : nullptr);
	}

	if (bDebugSamplingEnable)
	{
		FHairStrandsDebugData::ExtractData(GraphBuilder, DebugData, HairDatas->DebugData);
	}
}

void RenderHairStrandsAmbientOcclusion(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FHairStrandsRenderingData* HairDatas,
	const TRefCountPtr<IPooledRenderTarget>& InAOTexture)
{
	if (!GetHairStrandsSkyAOEnable() || Views.Num() == 0 || !InAOTexture || !HairDatas)
		return;

	FRDGTextureRef AOTexture = InAOTexture ? GraphBuilder.RegisterExternalTexture(InAOTexture, TEXT("AOTexture")) : nullptr;

	for (uint32 ViewIndex = 0; ViewIndex < uint32(Views.Num()); ++ViewIndex)
	{
		check(ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()));
		const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
		const bool bRenderHairLighting = VisibilityData.NodeIndex && VisibilityData.NodeData && HairDatas->MacroGroupsPerViews.Views[ViewIndex].VirtualVoxelResources.IsValid();
		if (!bRenderHairLighting)
		{
			continue;
		}

		if (ViewIndex > uint32(HairDatas->MacroGroupsPerViews.Views.Num()))
			continue;

		const FViewInfo& View = Views[ViewIndex];
		const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];
		for (const FHairStrandsMacroGroupData& MacroGroupData : HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas)
		{
			AddHairStrandsEnvironmentAOPass(GraphBuilder, View, VisibilityData, MacroGroupDatas, MacroGroupData, AOTexture);
		}
	}
}
