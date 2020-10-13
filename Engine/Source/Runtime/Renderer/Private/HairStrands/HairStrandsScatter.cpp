// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsScatter.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "HairStrandsRendering.h"
#include "GpuDebugRendering.h"

/////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairStrandsScatter_Enable = 0;
static int32 GHairStrandsScatter_Debug = 0;
static float GHairStrandsScatter_WorldRadius = 1;
static int32 GHairStrandsScatter_PassCount = 1;
static int32 GHairStrandsScatter_SampleCount = 8;
static FAutoConsoleVariableRef CVarHairStrandsScatter_WorldRadius(TEXT("r.HairStrands.Scatter.WorldRadius"), GHairStrandsScatter_WorldRadius, TEXT("Gather radius in world space (in cm)."));
static FAutoConsoleVariableRef CVarHairStrandsScatter_Enable(TEXT("r.HairStrands.Scatter"), GHairStrandsScatter_Enable, TEXT("Enable screen space hair scattering."));
static FAutoConsoleVariableRef CVarHairStrandsScatter_Debug(TEXT("r.HairStrands.Scatter.Debug"), GHairStrandsScatter_Debug, TEXT("Enable debug view of screen space hair scattering."));
static FAutoConsoleVariableRef CVarHairStrandsScatter_PassCount(TEXT("r.HairStrands.Scatter.IterationCount"), GHairStrandsScatter_PassCount, TEXT("Number of diffusion iterations."));
static FAutoConsoleVariableRef CVarHairStrandsScatter_SampleCount(TEXT("r.HairStrands.Scatter.SampleCount"), GHairStrandsScatter_SampleCount, TEXT("Number of sample using during the scattering integration."));

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairComposePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairComposePS);
	SHADER_USE_PARAMETER_STRUCT(FHairComposePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairComposePS, "/Engine/Private/HairStrands/HairScatterCompose.usf", "MainPS", SF_Pixel);

static FRDGTextureRef AddPreScatterComposePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& InCategorizationTexture,
	const FRDGTextureRef& InSceneColorTexture)
{
	const FIntPoint Resolution = InSceneColorTexture->Desc.Extent;
	FRDGTextureRef OutputTexture;
	{
		FRDGTextureDesc Desc;
		Desc.Extent = Resolution;
		Desc.Format = InSceneColorTexture->Desc.Format;
		Desc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(0);
		OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairComposedTexture"));
	}

	FVector2D PixelVelocity(1.f / (Resolution.X * 2), 1.f / (Resolution.Y * 2));

	FHairComposePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairComposePS::FParameters>();
	Parameters->CategorizationTexture = InCategorizationTexture;
	Parameters->SceneColorTexture = InSceneColorTexture;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairComposePS> PixelShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;

	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairCompose"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}

	return OutputTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairScatterPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairScatterPS);
	SHADER_USE_PARAMETER_STRUCT(FHairScatterPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER(FVector2D, OutputResolution)
		SHADER_PARAMETER(float, GatherWorldRadius)
		SHADER_PARAMETER(float, PixelRadiusAtDepth1)
		SHADER_PARAMETER(uint32, Enable)
		SHADER_PARAMETER(uint32, Debug)
		SHADER_PARAMETER(uint32, SampleCount)
		SHADER_PARAMETER(uint32, HairComponents)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, HairLUTTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, HairEnergyLUTTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutputColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisibilityNodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, VisibilityNodeData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DiffusionInputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairScatterPS, "/Engine/Private/HairStrands/HairScatter.usf", "MainPS", SF_Pixel);

static FRDGTextureRef AddScatterPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FVirtualVoxelResources& VoxelResources,
	const FRDGTextureRef& InVisibilityNodeIndex,
	const FRDGBufferRef&  InVisibilityNodeData,
	const FRDGTextureRef& InCategorizationTexture,
	const FRDGTextureRef& InDiffusionInput,
	const FRDGTextureRef& OutSceneColorTexture)
{
	if (!VoxelResources.IsValid())
		return nullptr;

	const FIntPoint Resolution = OutSceneColorTexture->Desc.Extent;
	const FHairLUT InHairLUT = GetHairLUT(GraphBuilder, View);

	float PixelRadiusAtDepth1 = 0;
	{
		const float DiameterToRadius = 0.5f;
		const float hFOV = FMath::DegreesToRadians(View.FOV);
		const float DiameterAtDepth1 = FMath::Tan(hFOV * 0.5f) / (0.5f * Resolution.X);
		PixelRadiusAtDepth1 = DiameterAtDepth1 * DiameterToRadius;
	}

	FRDGTextureRef OutDiffusionOutput = GraphBuilder.CreateTexture(InDiffusionInput->Desc, TEXT("HairDiffusionOutput"));

	FHairScatterPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairScatterPS::FParameters>();
	Parameters->Enable = GHairStrandsScatter_Enable > 0 ? 1 : 0;
	Parameters->Debug = GHairStrandsScatter_Debug > 0? 1 : 0;
	Parameters->SampleCount = FMath::Clamp(GHairStrandsScatter_SampleCount, 1, 32);
	Parameters->GatherWorldRadius = FMath::Clamp(GHairStrandsScatter_WorldRadius, 0.f, 100.f);
	Parameters->PixelRadiusAtDepth1 = PixelRadiusAtDepth1;
	Parameters->HairComponents = ToBitfield(GetHairComponents());
	Parameters->OutputResolution = Resolution;
	Parameters->VisibilityNodeIndex = InVisibilityNodeIndex;
	Parameters->VisibilityNodeData = GraphBuilder.CreateSRV(InVisibilityNodeData);
	Parameters->CategorizationTexture = InCategorizationTexture;
	Parameters->DiffusionInputTexture = InDiffusionInput;
	Parameters->HairLUTTexture = InHairLUT.Textures[HairLUTType_DualScattering];
	Parameters->HairEnergyLUTTexture = InHairLUT.Textures[HairLUTType_MeanEnergy];
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->VirtualVoxel = VoxelResources.UniformBuffer;
	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	}
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutSceneColorTexture, ERenderTargetLoadAction::ELoad);
//	Parameters->RenderTargets[1] = FRenderTargetBinding(OutSceneColorSubPixelTexture, ERenderTargetLoadAction::ELoad); // TODO
	Parameters->RenderTargets[2] = FRenderTargetBinding(OutDiffusionOutput, ERenderTargetLoadAction::ENoAction);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairScatterPS> PixelShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;

	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairScatter"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			if (GHairStrandsScatter_Debug > 0)
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero > ::GetRHI();
			}
			else
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One,
					CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One > ::GetRHI();
			}
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}

	return OutDiffusionOutput;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void AddHairDiffusionPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FVirtualVoxelResources& VoxelResources,
	const FRDGTextureRef SceneColorDepth,
	FRDGTextureRef OutLightSampleTexture)
{
	const uint32 DiffusionPassCount = FMath::Clamp(GHairStrandsScatter_PassCount, 0, 8);
	const bool bIsEnabled = DiffusionPassCount > 0 &&
		GHairStrandsScatter_Enable > 0 &&
		VisibilityData.NodeIndex &&
		VisibilityData.NodeData &&
		VisibilityData.CategorizationTexture;

	if (!bIsEnabled)
		return;

	FRDGTextureRef DiffusionInput = AddPreScatterComposePass(
		GraphBuilder,
		View,
		VisibilityData.CategorizationTexture,
		OutLightSampleTexture);

	for (uint32 DiffusionPassIt = 0; DiffusionPassIt < DiffusionPassCount; ++DiffusionPassIt)
	{
		FRDGTextureRef DiffusionOutput = AddScatterPass(
			GraphBuilder,
			View,
			VoxelResources,
			VisibilityData.NodeIndex,
			VisibilityData.NodeData,
			VisibilityData.CategorizationTexture,
			DiffusionInput,
			OutLightSampleTexture);

		DiffusionInput = DiffusionOutput;
	}
}