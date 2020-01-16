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

static int32 GHairSkylightingEnable = 1;
static FAutoConsoleVariableRef CVarHairSkylightingEnable(TEXT("r.HairStrands.SkyLighting"), GHairSkylightingEnable, TEXT("Enable sky lighting on hair."));

static int32 GHairSkyAOEnable = 1;
static FAutoConsoleVariableRef CVarHairSkyAOEnable(TEXT("r.HairStrands.SkyAO"), GHairSkyAOEnable, TEXT("Enable (sky) AO on hair."));

static int32 GHairSkyAOVirtualVoxel = 4;
static FAutoConsoleVariableRef CVarHairSkyAOVirtualVoxel(TEXT("r.HairStrands.SkyAO.VirtualVoxel"), GHairSkyAOVirtualVoxel, TEXT("Use virtual voxel for visibility/hair tracing/counting if available."));

static float GHairSkylightingConeAngle = 3;
static FAutoConsoleVariableRef CVarHairSkylightingConeAngle(TEXT("r.HairStrands.SkyLighting.ConeAngle"), GHairSkylightingConeAngle, TEXT("Cone angle for tracing sky lighting on hair."));

static int GHairSkylightingVirtualVoxel = 4;
static FAutoConsoleVariableRef CVarHairSkylightingVirtualVoxel(TEXT("r.HairStrands.SkyLighting.VirtualVoxel"), GHairSkylightingVirtualVoxel, TEXT("Use virtual voxel for visibility/hair tracing/counting if available."));

static int32 GHairStrandsSkyLightingSampleCount = 16;
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingSampleCount(TEXT("r.HairStrands.SkyLighting.SampleCount"), GHairStrandsSkyLightingSampleCount, TEXT("Number of samples used for evaluating multiple scattering and visible area (default is set to 16)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyLightingJitterSphericalIntegration = 0;
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingJitterSphericalIntegration(TEXT("r.HairStrands.SkyLighting.JitterIntegration"), GHairStrandsSkyLightingJitterSphericalIntegration, TEXT("Jitter the sphereical integration for the multiple scattering term. The result is more correct, but noiser as well."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsSkyAOSampleCount = 16;
static FAutoConsoleVariableRef CVarHairStrandsSkyAOSampleCount(TEXT("r.HairStrands.SkyAO.SampleCount"), GHairStrandsSkyAOSampleCount, TEXT("Number of samples used for evaluating hair AO (default is set to 16)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GHairStrandsSkyAODistanceThreshold = 10;
static float GHairStrandsSkyLightingDistanceThreshold = 10;
static FAutoConsoleVariableRef CVarHairStrandsSkyAOThreshold(TEXT("r.HairStrands.SkyAO.DistanceThreshold"), GHairStrandsSkyAODistanceThreshold, TEXT("Max distance for occlusion search."), ECVF_Scalability | ECVF_RenderThreadSafe);
static FAutoConsoleVariableRef CVarHairStrandsSkyLightingDistanceThreshold(TEXT("r.HairStrands.SkyLighting.DistanceThreshold"), GHairStrandsSkyLightingDistanceThreshold, TEXT("Max distance for occlusion search."), ECVF_Scalability | ECVF_RenderThreadSafe);

static bool GetHairStrandsSkyLightingEnable() { return GHairSkylightingEnable > 0; }
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
	class FVirtualVoxel : SHADER_PERMUTATION_INT("VOXEL_TRAVERSAL_TYPE", 5);
	using FPermutationDomain = TShaderPermutationDomain<FSampleSet, FVirtualVoxel>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(FVector, Voxel_MinAABB)
		SHADER_PARAMETER(uint32, Voxel_MacroGroupId)
		SHADER_PARAMETER(FVector, Voxel_MaxAABB)
		SHADER_PARAMETER(uint32, Voxel_Resolution)
		SHADER_PARAMETER(float, Voxel_DensityScale)
		SHADER_PARAMETER(float, Voxel_DepthBiasScale)
		SHADER_PARAMETER(float, Voxel_TanConeAngle)
		SHADER_PARAMETER(float, AO_Power)
		SHADER_PARAMETER(float, AO_Intensity)
		SHADER_PARAMETER(uint32, AO_SampleCount)
		SHADER_PARAMETER(float, AO_DistanceThreshold)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_STRUCT_REF(FVirtualVoxelParameters, VirtualVoxel)

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
	const bool bUseVirtualVoxel = MacroGroupDatas.VirtualVoxelResources.IsValid() && GHairSkyAOVirtualVoxel > 0;
	const int32 VoxelTraversalType = FMath::Clamp(GHairSkyAOVirtualVoxel, 0, 4);

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	FHairEnvironmentAO::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairEnvironmentAO::FParameters>();
	PassParameters->Voxel_MacroGroupId = MacroGroupData.MacroGroupId;
	if (!bUseVirtualVoxel)
	{
		PassParameters->Voxel_MinAABB = MacroGroupData.GetMinBound();
		PassParameters->Voxel_MaxAABB = MacroGroupData.GetMaxBound();
		PassParameters->Voxel_Resolution = MacroGroupData.GetResolution();
		PassParameters->Voxel_DensityTexture = GraphBuilder.RegisterExternalTexture(MacroGroupData.VoxelResources.DensityTexture);
	}
	PassParameters->Voxel_DensityScale = GetHairStrandsVoxelizationDensityScale();
	PassParameters->Voxel_DepthBiasScale = GetHairStrandsVoxelizationDepthBiasScale();
	PassParameters->Voxel_TanConeAngle = FMath::Tan(FMath::DegreesToRadians(GetHairStrandsSkyLightingConeAngle()));
	PassParameters->SceneTextures = SceneTextures;
	PassParameters->VirtualVoxel = MacroGroupDatas.VirtualVoxelResources.UniformBuffer;
	SetupSceneTextureSamplers(&PassParameters->SceneTextureSamplers);

	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->HairCategorizationTexture = GraphBuilder.RegisterExternalTexture(VisibilityData.CategorizationTexture);
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
	PermutationVector.Set<FHairEnvironmentAO::FVirtualVoxel>(bUseVirtualVoxel ? VoxelTraversalType : 0);

	TShaderMapRef<FHairEnvironmentAO> PixelShader(View.ShaderMap, PermutationVector);
	ClearUnusedGraphResources(*PixelShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsAO %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, &View, PixelShader](FRHICommandList& InRHICmdList)
	{
		InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, *PixelShader, GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Min, BF_SourceColor, BF_DestColor, BO_Add, BF_Zero, BF_DestAlpha>::GetRHI();
		SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
		SetShaderParameters(InRHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);
		FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairEnvironmentLightingComposePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairEnvironmentLightingComposePS);
	SHADER_USE_PARAMETER_STRUCT(FHairEnvironmentLightingComposePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityNodeOffsetAndCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairLightingSampleBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
};

IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentLightingComposePS, "/Engine/Private/HairStrands/HairStrandsEnvironmentLightingCompose.usf", "MainPS", SF_Pixel);

static void AddHairEnvironmentLightingComposePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FHairStrandsMacroGroupData& MacroGroupData,
	const FRDGBufferRef SampleLightingBuffer,
	FRDGTextureRef& OutColorTexture,
	FRDGTextureRef& OutSubColorTexture)
{
	FHairEnvironmentLightingComposePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairEnvironmentLightingComposePS::FParameters>();
	Parameters->HairCategorizationTexture = GraphBuilder.RegisterExternalTexture(VisibilityData.CategorizationTexture);
	Parameters->HairVisibilityNodeOffsetAndCount = GraphBuilder.RegisterExternalTexture(VisibilityData.NodeIndex);
	Parameters->HairLightingSampleBuffer  = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SampleLightingBuffer, EPixelFormat::PF_FloatRGBA));
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets[1] = FRenderTargetBinding(OutSubColorTexture, ERenderTargetLoadAction::ELoad);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairEnvironmentLightingComposePS> PixelShader(View.ShaderMap);
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutColorTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(*PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsEnvironmentLightingCompose"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);
		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(), Viewport.Height(),
			Viewport.Min.X,   Viewport.Min.Y,
			Viewport.Width(), Viewport.Height(),
			Viewport.Size(),
			Resolution,
			*VertexShader,
			EDRF_UseTriangleOptimization);
	});
}


///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairEnvironmentLightingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairEnvironmentLightingCS);
	SHADER_USE_PARAMETER_STRUCT(FHairEnvironmentLightingCS, FGlobalShader)

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	class FSampleSet : SHADER_PERMUTATION_INT("PERMUTATION_SAMPLESET", 2);
	class FVirtualVoxel : SHADER_PERMUTATION_INT("VOXEL_TRAVERSAL_TYPE", 5);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FSampleSet, FVirtualVoxel>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

		SHADER_PARAMETER(FVector, Voxel_MinAABB)
		SHADER_PARAMETER(uint32, Voxel_MacroGroupId)
		SHADER_PARAMETER(FVector, Voxel_MaxAABB)
		SHADER_PARAMETER(uint32, Voxel_Resolution)
		SHADER_PARAMETER(float, Voxel_DensityScale)
		SHADER_PARAMETER(float, Voxel_DepthBiasScale)
		SHADER_PARAMETER(float, Voxel_TanConeAngle)

		SHADER_PARAMETER(uint32, MaxVisibilityNodeCount)
		SHADER_PARAMETER(uint32, MultipleScatterSampleCount)
		SHADER_PARAMETER(uint32, HairComponents)
		SHADER_PARAMETER(float,  HairDualScatteringRoughnessOverride)
		SHADER_PARAMETER(uint32, JitterSphericalIntegration)
		SHADER_PARAMETER(float, HairDistanceThreshold)
		SHADER_PARAMETER(uint32, bHairUseViewHairCount)

		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeCoord)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE(TEXTURE3D, HairEnergyLUTTexture)
		SHADER_PARAMETER_RDG_TEXTURE(TEXTURE3D, HairScatteringLUTTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HairLUTSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture)
		SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer, IndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutLightingBuffer)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)

		SHADER_PARAMETER_STRUCT_REF(FVirtualVoxelParameters, VirtualVoxel)

		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)

		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairEnvironmentLightingCS, "/Engine/Private/HairStrands/HairStrandsEnvironmentLighting.usf", "MainCS", SF_Compute);

static FRDGBufferRef AddHairStrandsEnvironmentLightingPassCS(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FHairStrandsMacroGroupData& MacroGroupData,
	const uint32 NodeGroupSize,
	FRDGBufferRef IndirectArgsBuffer)
{
	FRDGBufferRef OutBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(
		4 * sizeof(uint16),
		VisibilityData.NodeData->Desc.NumElements),
		TEXT("HairSkyLightingNodeData"));

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	const bool bUseVirtualVoxel = MacroGroupDatas.VirtualVoxelResources.IsValid();
	const int32 VoxelTraversalType = FMath::Clamp(GHairSkylightingVirtualVoxel, 0, 4);

	// Render the reflection environment with tiled deferred culling
	const bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
	const bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);
	FHairEnvironmentLightingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairEnvironmentLightingCS::FParameters>();

	const FHairLUT InHairLUT = GetHairLUT(GraphBuilder.RHICmdList, View);
	PassParameters->HairEnergyLUTTexture = GraphBuilder.RegisterExternalTexture(InHairLUT.Textures[HairLUTType_MeanEnergy], TEXT("HairMeanEnergyLUTTexture"));;
	PassParameters->HairScatteringLUTTexture = GraphBuilder.RegisterExternalTexture(InHairLUT.Textures[HairLUTType_DualScattering], TEXT("HairScatteringEnergyLUTTexture"));
	PassParameters->HairLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->Voxel_MacroGroupId = MacroGroupData.MacroGroupId;
	if (!bUseVirtualVoxel)
	{
		PassParameters->Voxel_MinAABB = MacroGroupData.GetMinBound();
		PassParameters->Voxel_MaxAABB = MacroGroupData.GetMaxBound();
		PassParameters->Voxel_Resolution = MacroGroupData.GetResolution();
		PassParameters->Voxel_DensityTexture = GraphBuilder.RegisterExternalTexture(MacroGroupData.VoxelResources.DensityTexture);
	}
	PassParameters->Voxel_DensityScale = GetHairStrandsVoxelizationDensityScale();
	PassParameters->Voxel_DepthBiasScale = GetHairStrandsVoxelizationDepthBiasScale();
	PassParameters->Voxel_TanConeAngle = FMath::Tan(FMath::DegreesToRadians(GetHairStrandsSkyLightingConeAngle()));
	PassParameters->HairDistanceThreshold = FMath::Max(GHairStrandsSkyLightingDistanceThreshold, 1.f);
	PassParameters->bHairUseViewHairCount = VisibilityData.ViewHairCountTexture ? 1 : 0;
	PassParameters->MaxVisibilityNodeCount = VisibilityData.NodeData->Desc.NumElements;
	PassParameters->MultipleScatterSampleCount = FMath::Max(uint32(GHairStrandsSkyLightingSampleCount), 1u);
	PassParameters->JitterSphericalIntegration = GHairStrandsSkyLightingJitterSphericalIntegration ? 1 : 0;
	PassParameters->HairComponents = ToBitfield(GetHairComponents());
	PassParameters->HairDualScatteringRoughnessOverride = GetHairDualScatteringRoughnessOverride();
	PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->HairCountTexture = GraphBuilder.RegisterExternalTexture(VisibilityData.ViewHairCountTexture ? VisibilityData.ViewHairCountTexture : GSystemTextures.BlackDummy);
	PassParameters->SceneTextures = SceneTextures;
	PassParameters->VirtualVoxel = MacroGroupDatas.VirtualVoxelResources.UniformBuffer;
	SetupSceneTextureSamplers(&PassParameters->SceneTextureSamplers);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
	{
		FReflectionUniformParameters ReflectionUniformParameters;
		SetupReflectionUniformParameters(View, ReflectionUniformParameters);
		PassParameters->ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
	}
	PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
	PassParameters->IndirectArgsBuffer = IndirectArgsBuffer;
	PassParameters->OutLightingBuffer = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutBuffer, PF_FloatRGBA));

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
	}

	// Bind hair data
	FRDGBufferRef InHairVisibilityNodeData = GraphBuilder.RegisterExternalBuffer(VisibilityData.NodeData, TEXT("HairVisibilityNodeData"));
	FRDGBufferRef InHairVisibilityNodeCoord = GraphBuilder.RegisterExternalBuffer(VisibilityData.NodeCoord, TEXT("HairVisibilityNodeCoord"));
	PassParameters->HairVisibilityNodeData = GraphBuilder.CreateSRV(InHairVisibilityNodeData);
	PassParameters->HairVisibilityNodeCoord = GraphBuilder.CreateSRV(InHairVisibilityNodeCoord);

	check(NodeGroupSize == 64 || NodeGroupSize == 32);
	FHairEnvironmentLightingCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairEnvironmentLightingCS::FGroupSize>(NodeGroupSize == 64 ? 0 : (NodeGroupSize == 32 ? 1 : 2));
	PermutationVector.Set<FHairEnvironmentLightingCS::FSampleSet>(PassParameters->MultipleScatterSampleCount <= 16 ? 0 : 1);
	PermutationVector.Set<FHairEnvironmentLightingCS::FVirtualVoxel>(bUseVirtualVoxel ? VoxelTraversalType : 0);

	TShaderMapRef<FHairEnvironmentLightingCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsEnvironmentCS %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
		*ComputeShader,
		PassParameters,
		IndirectArgsBuffer,
		0);

	return OutBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const uint32 ViewIndex,
	const TArray<FViewInfo>& Views, 
	const FHairStrandsDatas* HairDatas,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneColorSubPixelTexture)
{
	if (!GetHairStrandsSkyLightingEnable() || !HairDatas)
		return;

	check(ViewIndex < uint32(Views.Num()));
	check(ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()));	
	const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
	const bool bRenderHairLighting = VisibilityData.NodeIndex && VisibilityData.NodeDataSRV;
	if (!bRenderHairLighting)
	{
		return;
	}
	
	FRDGBufferRef NodeIndirectArgBuffer = GraphBuilder.RegisterExternalBuffer(VisibilityData.NodeIndirectArg, TEXT("HairNodeIndirectArgBuffer"));

	const FViewInfo& View = Views[ViewIndex];
	const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];
	for (const FHairStrandsMacroGroupData& MacroGroupData : HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas)
	{
		FRDGBufferRef SampleLightingBuffer = AddHairStrandsEnvironmentLightingPassCS(GraphBuilder, View, VisibilityData, MacroGroupDatas, MacroGroupData, VisibilityData.NodeGroupSize, NodeIndirectArgBuffer);
		AddHairEnvironmentLightingComposePass(GraphBuilder, View, VisibilityData, MacroGroupData, SampleLightingBuffer, SceneColorTexture, SceneColorSubPixelTexture);
	}
}

void RenderHairStrandsAmbientOcclusion(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const FHairStrandsDatas* HairDatas,
	const TRefCountPtr<IPooledRenderTarget>& InAOTexture)
{
	if (!GetHairStrandsSkyAOEnable() || Views.Num() == 0 || !InAOTexture || !HairDatas)
		return;

	for (uint32 ViewIndex = 0; ViewIndex < uint32(Views.Num()); ++ViewIndex)
	{
		check(ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()));
		const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
		const bool bRenderHairLighting = VisibilityData.NodeIndex && VisibilityData.NodeDataSRV;
		if (!bRenderHairLighting)
		{
			continue;
		}

		if (ViewIndex > uint32(HairDatas->MacroGroupsPerViews.Views.Num()))
			continue;

		const FViewInfo& View = Views[ViewIndex];
		const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef AOTexture = InAOTexture ? GraphBuilder.RegisterExternalTexture(InAOTexture, TEXT("AOTexture")) : nullptr;
		for (const FHairStrandsMacroGroupData& MacroGroupData : HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas)
		{
			AddHairStrandsEnvironmentAOPass(GraphBuilder, View, VisibilityData, MacroGroupDatas, MacroGroupData, AOTexture);

		}
		GraphBuilder.Execute();
	}
}
