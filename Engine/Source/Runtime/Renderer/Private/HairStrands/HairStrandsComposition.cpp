// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsComposition.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "HairStrandsRendering.h"
#include "HairStrandsScatter.h"

/////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairFastResolveVelocityThreshold = 1;
static FAutoConsoleVariableRef CVarHairFastResolveVelocityThreshold(TEXT("r.HairStrands.VelocityThreshold"), GHairFastResolveVelocityThreshold, TEXT("Threshold value (in pixel) above which a pixel is forced to be resolve with responsive AA (in order to avoid smearing). Default is 3."));

static int32 GHairWriteGBufferData = 1;
static FAutoConsoleVariableRef CVarHairWriteGBufferData(TEXT("r.HairStrands.WriteGBufferData"), GHairWriteGBufferData, TEXT("Write hair hair material data into GBuffer before post processing run. 0: no write, 1: dummy write into GBuffer A/B (Normal/ShadingModel), 2: write into GBuffer A/B (Normal/ShadingModel). 2: Write entire GBuffer data. (default 1)."));

static int32 GHairStrandsComposeDOFDepth = 1;
static FAutoConsoleVariableRef CVarHairStrandsComposeDOFDepth(TEXT("r.HairStrands.DOFDepth"), GHairStrandsComposeDOFDepth, TEXT("Compose hair with DOF by lerping hair depth based on its opacity."));

/////////////////////////////////////////////////////////////////////////////////////////

float GetHairFastResolveVelocityThreshold(const FIntPoint& Resolution)
{
	FVector2D PixelVelocity(1.f / (Resolution.X * 2), 1.f / (Resolution.Y * 2));
	const float VelocityThreshold = FMath::Clamp(GHairFastResolveVelocityThreshold, 0, 512) * FMath::Min(PixelVelocity.X, PixelVelocity.Y);
	return VelocityThreshold;
}

/////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityComposeSamplePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityComposeSamplePS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityComposeSamplePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, bComposeDofDepth)
		SHADER_PARAMETER(uint32, bEmissiveEnable)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairEmissiveTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityNodeOffsetAndCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairLightingSampleBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairDOFDepthTexture)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FFogUniformParameters, FogStruct)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_COMPOSE_SAMPLE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityComposeSamplePS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "ComposeSamplePS", SF_Pixel);

static void AddHairVisibilityComposeSamplePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FRDGTextureRef& CategorizationTexture,
	const FRDGTextureRef& HairDOFDepthTexture,
	FRDGTextureRef& OutColorTexture,
	FRDGTextureRef& OutDepthTexture)
{
	check(VisibilityData.SampleLightingBuffer);
	const bool bDOFEnable = HairDOFDepthTexture != nullptr ? 1 : 0;

	TRDGUniformBufferRef<FFogUniformParameters> FogBuffer = CreateFogUniformBuffer(GraphBuilder, View);

	FHairVisibilityComposeSamplePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityComposeSamplePS::FParameters>();
	Parameters->HairSampleCount = VisibilityData.NodeCount;
	Parameters->bComposeDofDepth = bDOFEnable ? 1 : 0;
	Parameters->HairCategorizationTexture = CategorizationTexture;
	Parameters->HairVisibilityNodeOffsetAndCount = VisibilityData.NodeIndex;
	Parameters->HairLightingSampleBuffer = VisibilityData.SampleLightingBuffer;
	Parameters->HairDOFDepthTexture = bDOFEnable ? HairDOFDepthTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
	Parameters->OutputResolution = OutColorTexture->Desc.Extent;
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->bEmissiveEnable = VisibilityData.EmissiveTexture != nullptr ? 1 : 0;
	Parameters->HairEmissiveTexture = VisibilityData.EmissiveTexture != nullptr ? VisibilityData.EmissiveTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
	Parameters->FogStruct = FogBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(OutDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityComposeSamplePS> PixelShader(View.ShaderMap);
	FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutColorTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsComposeSample"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// Alpha usage/output is controlled with r.PostProcessing.PropagateAlpha. The value are:
		// 0: disabled(default);
		// 1: enabled in linear color space;
		// 2: same as 1, but also enable it through the tonemapper.
		//
		// When enable (PorpagateAlpha is set to 1 or 2), the alpha value means:
		// 0: valid pixel
		// 1: invalid pixel (background)
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
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


/////////////////////////////////////////////////////////////////////////////////////////

class FHairDOFDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairDOFDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairDOFDepthPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityNodeOffsetAndCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairLightingSampleBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_DOFDEPTH"), 1);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_FLOAT); 
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairDOFDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "DOFDepthPS", SF_Pixel);

static FRDGTextureRef AddHairDOFDepthPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FRDGTextureRef& CategorizationTexture,
	const FRDGTextureRef& InColorTexture,
	const FRDGTextureRef& InDepthTexture)
{
	check(VisibilityData.SampleLightingBuffer);
	FIntPoint OutputResolution = InColorTexture->Desc.Extent;

	FRDGTextureRef OutDOFDepthTexture = nullptr;
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(OutputResolution, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource, 1);
		OutDOFDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairDOFDepth"));
	}

	FHairDOFDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDOFDepthPS::FParameters>();
	Parameters->HairSampleCount = VisibilityData.NodeCount;
	Parameters->HairCategorizationTexture = CategorizationTexture;
	Parameters->HairVisibilityNodeOffsetAndCount = VisibilityData.NodeIndex;
	Parameters->HairLightingSampleBuffer = VisibilityData.SampleLightingBuffer;
	Parameters->SceneColorTexture = InColorTexture;
	Parameters->SceneDepthTexture = InDepthTexture;
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutDOFDepthTexture, ERenderTargetLoadAction::ENoAction);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairDOFDepthPS> PixelShader(View.ShaderMap);
	FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntPoint Resolution = OutputResolution;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsDOFDepth"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit); 
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Max, BF_One, BF_Zero>::GetRHI();
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

	return OutDOFDepthTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityFastResolvePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityFastResolvePS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityFastResolvePS, FGlobalShader);

	class FMSAACount : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_MSAACOUNT", 2, 4, 8);
	using FPermutationDomain = TShaderPermutationDomain<FMSAACount>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, VelocityThreshold)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityVelocityTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_FASTRESOLVE_MSAA"), 1);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R8G8B8A8);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityFastResolvePS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "FastResolvePS", SF_Pixel);

static void AddHairVisibilityFastResolveMSAAPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& HairVisibilityVelocityTexture,
	FRDGTextureRef& OutDepthTexture)
{
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	FRDGTextureRef DummyTexture;
	{
		FRDGTextureDesc Desc;
		Desc.Extent = Resolution;
		Desc.Format = PF_R8G8B8A8;
		Desc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(0);
		DummyTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairDummyTexture"));
	}

	FVector2D PixelVelocity(1.f / (Resolution.X * 2), 1.f / (Resolution.Y * 2));
	const float VelocityThreshold = FMath::Clamp(GHairFastResolveVelocityThreshold, 0, 512) * FMath::Min(PixelVelocity.X, PixelVelocity.Y);

	FHairVisibilityFastResolvePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityFastResolvePS::FParameters>();
	Parameters->HairVisibilityVelocityTexture = HairVisibilityVelocityTexture;
	Parameters->VelocityThreshold = GetHairFastResolveVelocityThreshold(Resolution);
	Parameters->RenderTargets[0] = FRenderTargetBinding(DummyTexture, ERenderTargetLoadAction::ENoAction);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthNop_StencilWrite);

	const uint32 MSAASampleCount = HairVisibilityVelocityTexture->Desc.NumSamples;
	check(MSAASampleCount == 4 || MSAASampleCount == 8);
	FHairVisibilityFastResolvePS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityFastResolvePS::FMSAACount>(MSAASampleCount == 4 ? 4 : 8);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityFastResolvePS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityMarkTAAFastResolve"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();	
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
				false, CF_Always, 
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
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
}

class FHairVisibilityFastResolveMaskPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityFastResolveMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityFastResolveMaskPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ResolveMaskTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_FASTRESOLVE_MASK"), 1);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R8G8B8A8);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityFastResolveMaskPS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "FastResolvePS", SF_Pixel);

static void AddHairVisibilityFastResolveMaskPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& HairResolveMaskTexture,
	FRDGTextureRef& OutDepthTexture)
{
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	FRDGTextureRef DummyTexture;
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_RenderTargetable);
		DummyTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairDummyTexture"));
	}

	FHairVisibilityFastResolveMaskPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityFastResolveMaskPS::FParameters>();
	Parameters->ResolveMaskTexture = HairResolveMaskTexture;
	Parameters->RenderTargets[0] = FRenderTargetBinding(DummyTexture, ERenderTargetLoadAction::ENoAction);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthNop_StencilWrite);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityFastResolveMaskPS> PixelShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;

	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityMarkTAAFastResolve"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
				RHICmdList.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
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
}


///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityGBufferWritePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityGBufferWritePS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityGBufferWritePS, FGlobalShader);


	class FOutputType : SHADER_PERMUTATION_INT("PERMUTATION_OUTPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FOutputType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, bWriteDummyData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, NodeData)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_B8G8R8A8);
		OutEnvironment.SetRenderTargetOutputFormat(1, PF_FloatRGBA);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityGBufferWritePS, "/Engine/Private/HairStrands/HairStrandsGBufferWrite.usf", "MainPS", SF_Pixel);

static void AddHairVisibilityGBufferWritePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const bool bWriteDummyData,
	const FRDGTextureRef& CategorizationTexture,
	const FRDGTextureRef& NodeIndex,
	const FRDGBufferRef& NodeData,
	FRDGTextureRef OutGBufferATexture,
	FRDGTextureRef OutGBufferBTexture,
	FRDGTextureRef OutGBufferCTexture,
	FRDGTextureRef OutGBufferDTexture,
	FRDGTextureRef OutGBufferETexture,
	FRDGTextureRef OutDepthTexture)
{
	const bool bWriteFullGBuffer = OutGBufferCTexture != nullptr;
	const bool bWriteDepth = OutDepthTexture != nullptr;

	if (!OutGBufferATexture || !OutGBufferBTexture)
	{
		return;
	}

	if (bWriteFullGBuffer && (!OutGBufferCTexture || !OutDepthTexture))
	{
		return;
	}

	FHairVisibilityGBufferWritePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityGBufferWritePS::FParameters>();
	Parameters->bWriteDummyData = bWriteDummyData ? 1 : 0;
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->NodeIndex = NodeIndex;
	Parameters->NodeData = GraphBuilder.CreateSRV(NodeData);
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutGBufferATexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets[1] = FRenderTargetBinding(OutGBufferBTexture, ERenderTargetLoadAction::ELoad);	
	if (bWriteFullGBuffer)
	{
		Parameters->RenderTargets[2] = FRenderTargetBinding(OutGBufferCTexture, ERenderTargetLoadAction::ELoad);
		if (OutGBufferDTexture)
		{
			Parameters->RenderTargets[3] = FRenderTargetBinding(OutGBufferDTexture, ERenderTargetLoadAction::ELoad);
		}
		if (OutGBufferETexture)
		{
			Parameters->RenderTargets[4] = FRenderTargetBinding(OutGBufferETexture, ERenderTargetLoadAction::ELoad);
		}
	}
	if (bWriteDepth)
	{
		Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
	}

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	FHairVisibilityGBufferWritePS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityGBufferWritePS::FOutputType>(bWriteFullGBuffer ? 1 : 0);
	TShaderMapRef<FHairVisibilityGBufferWritePS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutGBufferATexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;
	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsGBufferWrite"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView, bWriteDepth](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			if (bWriteDepth)
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			}

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

void RenderHairComposition(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const int32 ViewIndex,
	const FHairStrandsRenderingData* HairDatas,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture)
{
	if (!HairDatas || HairDatas->HairVisibilityViews.HairDatas.Num() == 0 || ViewIndex < 0)
		return;

	const FHairStrandsVisibilityViews& HairVisibilityViews = HairDatas->HairVisibilityViews;

	DECLARE_GPU_STAT(HairStrandsComposition);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsComposition");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsComposition);

	{
		if (View.Family)
		{
			if (ViewIndex < HairVisibilityViews.HairDatas.Num())
			{
				const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];
				const FHairStrandsVisibilityData& VisibilityData = HairVisibilityViews.HairDatas[ViewIndex];

				if (!VisibilityData.CategorizationTexture)
				{
					return; // Automatically skip for any view not rendering hair
				}

				// todo: rehook the diffusion pass
				//AddHairDiffusionPass(
				//	GraphBuilder,
				//	View,
				//	VisibilityData,
				//	MacroGroupDatas.VirtualVoxelResources,
				//	SceneDepthTexture,
				//	SceneColorSubPixelTexture,
				//	SceneColorTexture);

				FRDGTextureRef DOFDepth = nullptr;
				const bool bHairDOF = GHairStrandsComposeDOFDepth > 0 ? 1 : 0;
				if (bHairDOF)
				{
					DOFDepth = AddHairDOFDepthPass(
						GraphBuilder,
						View,
						VisibilityData,
						VisibilityData.CategorizationTexture,
						SceneColorTexture,
						SceneDepthTexture);
				}

				AddHairVisibilityComposeSamplePass(
					GraphBuilder,
					View,
					VisibilityData,
					VisibilityData.CategorizationTexture,
					DOFDepth,
					SceneColorTexture,
					SceneDepthTexture);

				if (HairVisibilityViews.HairDatas[ViewIndex].VelocityTexture)
				{
					AddHairVisibilityFastResolveMSAAPass(
						GraphBuilder,
						View,
						HairVisibilityViews.HairDatas[ViewIndex].VelocityTexture,
						SceneDepthTexture);
				}
				else if (HairVisibilityViews.HairDatas[ViewIndex].ResolveMaskTexture)
				{
					AddHairVisibilityFastResolveMaskPass(
						GraphBuilder,
						View,
						HairVisibilityViews.HairDatas[ViewIndex].ResolveMaskTexture,
						SceneDepthTexture);
				}

				const bool bWriteDummyData		= View.Family->ViewMode != VMI_VisualizeBuffer && GHairWriteGBufferData == 1;
				const bool bWritePartialGBuffer = View.Family->ViewMode != VMI_VisualizeBuffer && (GHairWriteGBufferData == 1 || GHairWriteGBufferData == 2);
				const bool bWriteFullGBuffer	= View.Family->ViewMode == VMI_VisualizeBuffer || (GHairWriteGBufferData == 3);
				if (bWriteFullGBuffer || bWritePartialGBuffer)
				{
					FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
					const FRDGTextureRef GBufferATexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferA);
					const FRDGTextureRef GBufferBTexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferB);
					const FRDGTextureRef GBufferCTexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferC);
					const FRDGTextureRef GBufferDTexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferD);
					const FRDGTextureRef GBufferETexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferE);
					if (bWritePartialGBuffer && GBufferATexture && GBufferBTexture)
					{
						AddHairVisibilityGBufferWritePass(
							GraphBuilder,
							View,
							bWriteDummyData,
							VisibilityData.CategorizationTexture,
							VisibilityData.NodeIndex,
							VisibilityData.NodeData,
							GBufferATexture,
							GBufferBTexture,
							nullptr,
							nullptr,
							nullptr,
							nullptr);
					}
					else if (bWriteFullGBuffer && GBufferATexture && GBufferBTexture && GBufferCTexture && SceneDepthTexture)
					{
						AddHairVisibilityGBufferWritePass(
							GraphBuilder,
							View,
							bWriteDummyData,
							VisibilityData.CategorizationTexture,
							VisibilityData.NodeIndex,
							VisibilityData.NodeData,
							GBufferATexture,
							GBufferBTexture,
							GBufferCTexture,
							GBufferDTexture,
							GBufferETexture,
							SceneDepthTexture);
					}
				}
			}
		}
	}
}

void RenderHairComposition(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FHairStrandsRenderingData* HairDatas,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			RenderHairComposition(
				GraphBuilder,
				View,
				ViewIndex,
				HairDatas,
				SceneColorTexture,
				SceneDepthTexture);
		}
	}
}