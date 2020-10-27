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

static int32 GHairStrandsSkyAOSampleCount = 4;
static FAutoConsoleVariableRef CVarHairStrandsSkyAOSampleCount(TEXT("r.HairStrands.SkyAO.SampleCount"), GHairStrandsSkyAOSampleCount, TEXT("Number of samples used for evaluating hair AO (default is set to 16)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GHairStrandsTransmissionDensityScaleFactor = 10;
static FAutoConsoleVariableRef CVarHairStrandsTransmissionDensityScaleFactor(TEXT("r.HairStrands.SkyLighting.TransmissionDensityScale"), GHairStrandsTransmissionDensityScaleFactor, TEXT("Density scale for controlling how much sky lighting is transmitted."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyLightingUseHairCountTexture = 1;
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingUseHairCountTexture(TEXT("r.HairStrands.SkyLighting.UseViewHairCount"), GHairStrandsSkyLightingUseHairCountTexture, TEXT("Use the view hair count texture for estimating background transmitted light (enabled by default)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GHairStrandsSkyAODistanceThreshold = 10;
static float GHairStrandsSkyLightingDistanceThreshold = 10;
static FAutoConsoleVariableRef CVarHairStrandsSkyAOThreshold(TEXT("r.HairStrands.SkyAO.DistanceThreshold"), GHairStrandsSkyAODistanceThreshold, TEXT("Max distance for occlusion search."), ECVF_Scalability | ECVF_RenderThreadSafe);
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingDistanceThreshold(TEXT("r.HairStrands.SkyLighting.DistanceThreshold"), GHairStrandsSkyLightingDistanceThreshold, TEXT("Max distance for occlusion search."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyLighting_IntegrationType = 2;
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
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(uint32, Voxel_MacroGroupId)
		SHADER_PARAMETER(float, Voxel_TanConeAngle)
		SHADER_PARAMETER(float, AO_Power)
		SHADER_PARAMETER(float, AO_Intensity)
		SHADER_PARAMETER(uint32, AO_SampleCount)
		SHADER_PARAMETER(float, AO_DistanceThreshold)
		SHADER_PARAMETER(uint32, Output_bHalfRes)
		SHADER_PARAMETER(FVector2D, Output_InvResolution)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)

		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentAO, "/Engine/Private/HairStrands/HairStrandsEnvironmentAO.usf", "MainPS", SF_Pixel);

bool IsSSGIHalfRes();
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

	const FIntRect Viewport = View.ViewRect;
	const FIntRect HalfResViewport = FIntRect::DivideAndRoundUp(Viewport, 2);
	const bool bHalfRes = IsSSGIHalfRes() || Output->Desc.Extent.X == HalfResViewport.Width();

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
	PassParameters->Output_bHalfRes = bHalfRes;
	PassParameters->Output_InvResolution = FVector2D(1.f/Output->Desc.Extent.X, 1.f/Output->Desc.Extent.Y);

	FIntRect ViewRect;
	if (bHalfRes)
	{
		ViewRect.Min.X = 0;
		ViewRect.Min.Y = 0;
		ViewRect.Max = Output->Desc.Extent;
	}
	else
	{
		ViewRect = View.ViewRect;
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
	}

	FHairEnvironmentAO::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairEnvironmentAO::FSampleSet>(PassParameters->AO_SampleCount <= 16 ? 0 : 1);

	TShaderMapRef<FHairEnvironmentAO> PixelShader(View.ShaderMap, PermutationVector);
	ClearUnusedGraphResources(PixelShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsAO %dx%d", ViewRect.Width(), ViewRect.Height()),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, &View, PixelShader, ViewRect](FRHICommandList& InRHICmdList)
	{
		InRHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

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
		if (!IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform))
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
		SHADER_PARAMETER(float, Voxel_TanConeAngle)

		SHADER_PARAMETER(uint32, MaxVisibilityNodeCount)
		SHADER_PARAMETER(uint32, MultipleScatterSampleCount)

		SHADER_PARAMETER(uint32, HairComponents)
		SHADER_PARAMETER(float,  HairDualScatteringRoughnessOverride)
		SHADER_PARAMETER(float, TransmissionDensityScaleFactor)
		SHADER_PARAMETER(float, HairDistanceThreshold)

		SHADER_PARAMETER(FVector4, SkyLight_OcclusionTintAndMinOcclusion)

		SHADER_PARAMETER(uint32, SkyLight_OcclusionCombineMode)
		SHADER_PARAMETER(float, SkyLight_OcclusionExponent)
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
		SHADER_PARAMETER_RDG_TEXTURE(TEXTURE3D, HairEnergyLUTTexture)
		SHADER_PARAMETER_RDG_TEXTURE(TEXTURE3D, HairScatteringLUTTexture)
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
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
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
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentLightingPS, "/Engine/Private/HairStrands/HairStrandsEnvironmentLighting.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentLightingVS, "/Engine/Private/HairStrands/HairStrandsEnvironmentLighting.usf", "MainVS", SF_Vertex);

static void AddHairStrandsEnvironmentLightingPassPS(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
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

	float SkyLightContrast = 0.01f;
	float SkyLightOcclusionExponent = 1.0f;
	FVector4 SkyLightOcclusionTintAndMinOcclusion(0.0f, 0.0f, 0.0f, 0.0f);
	EOcclusionCombineMode SkyLightOcclusionCombineMode = EOcclusionCombineMode::OCM_MAX;
	if (FSkyLightSceneProxy* SkyLight = Scene->SkyLight)
	{
		SkyLightOcclusionExponent = SkyLight->OcclusionExponent;
		SkyLightOcclusionTintAndMinOcclusion = FVector4(SkyLight->OcclusionTint);
		SkyLightOcclusionTintAndMinOcclusion.W = SkyLight->MinOcclusion;
		SkyLightOcclusionCombineMode = SkyLight->OcclusionCombineMode;
	}

	PassParameters->SkyLight_OcclusionCombineMode = SkyLightOcclusionCombineMode == OCM_Minimum ? 0.0f : 1.0f;
	PassParameters->SkyLight_OcclusionExponent = SkyLightOcclusionExponent;
	PassParameters->SkyLight_OcclusionTintAndMinOcclusion = SkyLightOcclusionTintAndMinOcclusion;
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
	PassParameters->HairCountTexture = VisibilityData.ViewHairCountTexture ? VisibilityData.ViewHairCountTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
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
	const FScene* Scene,
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
			AddHairStrandsEnvironmentLightingPassPS(GraphBuilder, Scene, View, VisibilityData, MacroGroupDatas, SceneColorTexture, nullptr);
		}
	}
}

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
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
	const bool bRenderHairLighting = VisibilityData.NodeIndex && VisibilityData.NodeData && HairDatas->MacroGroupsPerViews.Views[ViewIndex].VirtualVoxelResources.IsValid() && VisibilityData.CategorizationTexture;
	if (!bRenderHairLighting)
	{
		return;
	}

	const bool bDebugSamplingEnable = GHairStrandsSkyLighting_DebugSample > 0;
	FHairStrandsDebugData::Data DebugData;
	if (bDebugSamplingEnable)
	{
		HairDatas->DebugData.Resources = FHairStrandsDebugData::CreateData(GraphBuilder);
	}

	const FViewInfo& View = Views[ViewIndex];
	const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];
	AddHairStrandsEnvironmentLightingPassPS(GraphBuilder, Scene, View, VisibilityData, MacroGroupDatas, nullptr, bDebugSamplingEnable ? &HairDatas->DebugData.Resources : nullptr);
}

void RenderHairStrandsAmbientOcclusion(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FHairStrandsRenderingData* HairDatas,
	const FRDGTextureRef& InAOTexture)
{
	if (!GetHairStrandsSkyAOEnable() || Views.Num() == 0 || !InAOTexture || !HairDatas)
		return;

	for (uint32 ViewIndex = 0; ViewIndex < uint32(Views.Num()); ++ViewIndex)
	{
		check(ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()));
		const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
		const bool bRenderHairLighting = VisibilityData.NodeIndex && VisibilityData.NodeData && HairDatas->MacroGroupsPerViews.Views[ViewIndex].VirtualVoxelResources.IsValid() && VisibilityData.CategorizationTexture;
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
			AddHairStrandsEnvironmentAOPass(GraphBuilder, View, VisibilityData, MacroGroupDatas, MacroGroupData, InAOTexture);
		}
	}
}
