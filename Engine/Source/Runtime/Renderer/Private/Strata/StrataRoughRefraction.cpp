// Copyright Epic Games, Inc. All Rights Reserved.

#include "Strata.h"
#include "HAL/IConsoleManager.h"
#include "PixelShaderUtils.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "RendererInterface.h"
#include "UniformBuffer.h"
#include "SceneTextureParameters.h"
#include "ScreenPass.h"
#include "ShaderCompiler.h"


static TAutoConsoleVariable<int32> CVarStrataOpaqueMaterialRoughRefraction(
	TEXT("r.Strata.OpaqueMaterialRoughRefraction"),
	0,
	TEXT("Enable Strata opaque material rough refractions effect from top layers over layers below."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);


namespace Strata
{

bool IsStrataOpaqueMaterialRoughRefractionEnabled()
{
	return IsStrataEnabled() && CVarStrataOpaqueMaterialRoughRefraction.GetValueOnAnyThread() > 0;
}

class FOpaqueRoughRefractionPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOpaqueRoughRefractionPS);
	SHADER_USE_PARAMETER_STRUCT(FOpaqueRoughRefractionPS, FGlobalShader);

	class FEnableBlur : SHADER_PERMUTATION_BOOL("PERMUTATION_ENABLE_BLUR");
	using FPermutationDomain = TShaderPermutationDomain<FEnableBlur>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FStrataGlobalUniformParameters, Strata)
		SHADER_PARAMETER_STRUCT_INCLUDE(Strata::FStrataTilePassVS::FParameters, StrataTile)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeparatedOpaqueRoughRefractionSceneColor)
		SHADER_PARAMETER(FVector2f, BlurDirection)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT();

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("OPAQUE_ROUGH_REFRACTION_PS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FOpaqueRoughRefractionPS, "/Engine/Private/Strata/StrataRoughRefraction.usf", "OpaqueRoughRefractionPS", SF_Pixel);


void AddStrataOpaqueRoughRefractionPasses(
	FRDGBuilder& GraphBuilder,
	FSceneTextures& SceneTextures,
	TArrayView<const FViewInfo> Views)
{
	const uint32 ViewCount = Views.Num();
	const bool bOpaqueRoughRefractionEnabled = IsStrataOpaqueMaterialRoughRefractionEnabled() && ViewCount > 0;
	RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, bOpaqueRoughRefractionEnabled, "StrataOpaqueRoughRefraction");
	if (!bOpaqueRoughRefractionEnabled)
	{
		return;
	}

	FRDGTextureRef SceneColorTexture = SceneTextures.Color.Target;
	FRDGTextureRef TempTexture = nullptr;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;

	//
	// 1. First blur pass into temporary buffer. This only blurs tiles containing pixels with rough refractions.
	//
	EStrataTileType StrataTileType = EStrataTileType::EOpaqueRoughRefraction;
	for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (TempTexture == nullptr)
		{
			TempTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(GetSceneTextureExtent(), PF_FloatRGBA, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("Strata.RoughRefrac.TempTexture"));
		}

		FRDGTextureRef SeparatedOpaqueRoughRefractionSceneColor = View.StrataSceneData->SeparatedOpaqueRoughRefractionSceneColor;

		FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder);
		FOpaqueRoughRefractionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOpaqueRoughRefractionPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View.StrataSceneData);
		PassParameters->SeparatedOpaqueRoughRefractionSceneColor = SeparatedOpaqueRoughRefractionSceneColor;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(TempTexture, LoadAction);
		PassParameters->BlurDirection = FVector2f(1.0f, 0.0f);

		FOpaqueRoughRefractionPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FOpaqueRoughRefractionPS::FEnableBlur>(true);
		TShaderMapRef<FOpaqueRoughRefractionPS> PixelShader(View.ShaderMap, PermutationVector);

		Strata::FStrataTilePassVS::FPermutationDomain VSPermutationVector;
		VSPermutationVector.Set< Strata::FStrataTilePassVS::FEnableDebug >(false);
		VSPermutationVector.Set< Strata::FStrataTilePassVS::FEnableTexCoordScreenVector >(false);
		TShaderMapRef<Strata::FStrataTilePassVS> TileVertexShader(View.ShaderMap, VSPermutationVector);

		EPrimitiveType PrimitiveType = PT_TriangleList;
		PassParameters->StrataTile = Strata::SetTileParameters(GraphBuilder, View, StrataTileType, PrimitiveType);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OpaqueRoughRefraction - Pass0"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, TileVertexShader, PixelShader, PassParameters, StrataTileType, PrimitiveType](FRHICommandList& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				// Set the device viewport for the view.
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI(); // no blend
				GraphicsPSOInit.PrimitiveType = PrimitiveType;
				GraphicsPSOInit.bDepthBounds = false;
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = TileVertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0x0);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				SetShaderParameters(RHICmdList, TileVertexShader, TileVertexShader.GetVertexShader(), PassParameters->StrataTile);
				RHICmdList.DrawPrimitiveIndirect(PassParameters->StrataTile.TileIndirectBuffer->GetIndirectRHICallBuffer(), Strata::TileTypeDrawIndirectArgOffset(StrataTileType));
			});

		LoadAction = ERenderTargetLoadAction::ELoad;
	}

	//
	// 2. second blur pass into temporary buffer. This only blurs tiles containing pixels with rough refractions.
	//
	for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder);
		FOpaqueRoughRefractionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOpaqueRoughRefractionPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View.StrataSceneData);
		PassParameters->SeparatedOpaqueRoughRefractionSceneColor = TempTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->BlurDirection = FVector2f(0.0f, 1.0f);

		FOpaqueRoughRefractionPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FOpaqueRoughRefractionPS::FEnableBlur>(true);
		TShaderMapRef<FOpaqueRoughRefractionPS> PixelShader(View.ShaderMap, PermutationVector);

		Strata::FStrataTilePassVS::FPermutationDomain VSPermutationVector;
		VSPermutationVector.Set< Strata::FStrataTilePassVS::FEnableDebug >(false);
		VSPermutationVector.Set< Strata::FStrataTilePassVS::FEnableTexCoordScreenVector >(false);
		TShaderMapRef<Strata::FStrataTilePassVS> TileVertexShader(View.ShaderMap, VSPermutationVector);

		EPrimitiveType PrimitiveType = PT_TriangleList;
		PassParameters->StrataTile = Strata::SetTileParameters(GraphBuilder, View, StrataTileType, PrimitiveType);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OpaqueRoughRefraction - Pass2)"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, TileVertexShader, PixelShader, PassParameters, StrataTileType, PrimitiveType](FRHICommandList& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				// Set the device viewport for the view.
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI(); // Additive blend
				GraphicsPSOInit.PrimitiveType = PrimitiveType;
				GraphicsPSOInit.bDepthBounds = false;
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = TileVertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0x0);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				SetShaderParameters(RHICmdList, TileVertexShader, TileVertexShader.GetVertexShader(), PassParameters->StrataTile);
				RHICmdList.DrawPrimitiveIndirect(PassParameters->StrataTile.TileIndirectBuffer->GetIndirectRHICallBuffer(), Strata::TileTypeDrawIndirectArgOffset(StrataTileType));
			});
	}

	//
	// 3. Add remaining tiles with subsurface scattering that did not have rough refractions on them, resulting in a complete scene color texture.
	//
	StrataTileType = EStrataTileType::ESSSWithoutOpaqueRoughRefraction;
	for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		FRDGTextureRef SeparatedOpaqueRoughRefractionSceneColor = View.StrataSceneData->SeparatedOpaqueRoughRefractionSceneColor;

		FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder);
		FOpaqueRoughRefractionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOpaqueRoughRefractionPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View.StrataSceneData);
		PassParameters->SeparatedOpaqueRoughRefractionSceneColor = SeparatedOpaqueRoughRefractionSceneColor;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->BlurDirection = FVector2f(0.0f, 0.0f);

		FOpaqueRoughRefractionPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FOpaqueRoughRefractionPS::FEnableBlur>(false);
		TShaderMapRef<FOpaqueRoughRefractionPS> PixelShader(View.ShaderMap, PermutationVector);

		Strata::FStrataTilePassVS::FPermutationDomain VSPermutationVector;
		VSPermutationVector.Set< Strata::FStrataTilePassVS::FEnableDebug >(false);
		VSPermutationVector.Set< Strata::FStrataTilePassVS::FEnableTexCoordScreenVector >(false);
		TShaderMapRef<Strata::FStrataTilePassVS> TileVertexShader(View.ShaderMap, VSPermutationVector);

		EPrimitiveType PrimitiveType = PT_TriangleList;
		PassParameters->StrataTile = Strata::SetTileParameters(GraphBuilder, View, StrataTileType, PrimitiveType);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("OpaqueRoughRefraction - Add SSSOnly tiles"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, TileVertexShader, PixelShader, PassParameters, StrataTileType, PrimitiveType](FRHICommandList& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				// Set the device viewport for the view.
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI(); // Additive blend
				GraphicsPSOInit.PrimitiveType = PrimitiveType;
				GraphicsPSOInit.bDepthBounds = false;
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = TileVertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0x0);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				SetShaderParameters(RHICmdList, TileVertexShader, TileVertexShader.GetVertexShader(), PassParameters->StrataTile);
				RHICmdList.DrawPrimitiveIndirect(PassParameters->StrataTile.TileIndirectBuffer->GetIndirectRHICallBuffer(), Strata::TileTypeDrawIndirectArgOffset(StrataTileType));
			});
	}
}


//////////////////////////////////////////////////////////////////////////
// RnD shaders only used when enabled locally
//////////////////////////////////////////////////////////////////////////

// Keeping it simple: this should always be checked in with a value of 0
#define STRATA_ROUGH_REFRACTION_RND 0

#if STRATA_ROUGH_REFRACTION_RND

static TAutoConsoleVariable<int32> CVarStrataRoughRefractionShadersShowRoughRefractionRnD(
	TEXT("r.Strata.ShowRoughRefractionRnD"),
	1,
	TEXT("Enable strata rough refraction shaders."),
	ECVF_RenderThreadSafe);

bool ShouldRenderStrataRoughRefractionRnD()
{
	return CVarStrataRoughRefractionShadersShowRoughRefractionRnD.GetValueOnAnyThread() > 0;
}

#else
bool ShouldRenderStrataRoughRefractionRnD() { return false; }
#endif


#if STRATA_ROUGH_REFRACTION_RND

class FEvaluateRoughRefractionLobeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEvaluateRoughRefractionLobeCS);
	SHADER_USE_PARAMETER_STRUCT(FEvaluateRoughRefractionLobeCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SampleCountTextureUAV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<>, LobStatisticsBufferUAV)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(uint32, TraceSqrtSampleCount)
	END_SHADER_PARAMETER_STRUCT()
	
	static const uint32 ThreadGroupSize = 8;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("STRATA_RND_SHADERS"), 1);
		OutEnvironment.SetDefine(TEXT("EVALUATE_ROUGH_REFRACTION_LOBE_CS"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};
IMPLEMENT_GLOBAL_SHADER(FEvaluateRoughRefractionLobeCS, "/Engine/Private/Strata/StrataRoughRefraction.usf", "EvaluateRoughRefractionLobeCS", SF_Compute);



class FVisualizeRoughRefractionPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeRoughRefractionPS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeRoughRefractionPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint>, SampleCountTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<>, LobStatisticsBuffer)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(float, TraceDomainSize)
		SHADER_PARAMETER(uint32, SlabInterfaceLineCount)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("STRATA_RND_SHADERS"), 1);
		OutEnvironment.SetDefine(TEXT("VISUALIZE_ROUGH_REFRACTION_PS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVisualizeRoughRefractionPS, "/Engine/Private/Strata/StrataRoughRefraction.usf", "VisualizeRoughRefractionPS", SF_Pixel);



#endif // STRATA_ROUGH_REFRACTION_RND


void StrataRoughRefractionRnD(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture& ScreenPassSceneColor)
{
#if STRATA_ROUGH_REFRACTION_RND
	if (IsStrataEnabled() && ShouldRenderStrataRoughRefractionRnD())
	{
		if (!ShaderPrint::IsSupported(View.GetShaderPlatform()))
		{
			return;
		}
		check(ShaderPrint::IsEnabled(View));	// One must enable ShaderPrint beforehand using r.ShaderPrint=1

		//////////////////////////////////////////////////////////////////////////
		// Create resources
		
		// Texture to count
		const uint32 SampleCountTextureWidth = 64;
		const FIntPoint SampleCountTextureSize(SampleCountTextureWidth, SampleCountTextureWidth);
		FRDGTextureRef SampleCountTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SampleCountTextureSize, PF_R32_UINT, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource), TEXT("Strata.RoughRefrac.SampleCount"));
		FRDGTextureUAVRef SampleCountTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SampleCountTexture));
		FRDGTextureSRVRef SampleCountTextureSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SampleCountTexture));

		FRDGBufferRef LobStatisticsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float) * 8, 16), TEXT("Strata.RoughRefrac.LobStat"));
		FRDGBufferUAVRef LobStatisticsBufferUAV = GraphBuilder.CreateUAV(LobStatisticsBuffer, PF_R32_UINT);
		FRDGBufferSRVRef LobStatisticsBufferSRV = GraphBuilder.CreateSRV(LobStatisticsBuffer, PF_R32_UINT);

		//////////////////////////////////////////////////////////////////////////
		// Clear resources
		AddClearUAVPass(GraphBuilder, SampleCountTextureUAV, uint32(0));

		//////////////////////////////////////////////////////////////////////////
		// Trace and update resources
		{
			FEvaluateRoughRefractionLobeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEvaluateRoughRefractionLobeCS::FParameters>();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->SampleCountTextureUAV = SampleCountTextureUAV;
			PassParameters->LobStatisticsBufferUAV = LobStatisticsBufferUAV;
			PassParameters->MiniFontTexture = GetMiniFontTexture();
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintParameters);
			PassParameters->TraceSqrtSampleCount = 128;

			FEvaluateRoughRefractionLobeCS::FPermutationDomain PermutationVector;
			TShaderMapRef<FEvaluateRoughRefractionLobeCS> ComputeShader(View.ShaderMap, PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Strata::EvaluateRoughRefractionLobeCS"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(1, FEvaluateRoughRefractionLobeCS::ThreadGroupSize));
		}



		//////////////////////////////////////////////////////////////////////////
		// Debug print everything on screen
		{
			const float TraceDomainSize = 32.0f;
			const float SlabInterfaceLineCount = 16.0f;

			ShaderPrint::RequestSpaceForLines((TraceDomainSize * TraceDomainSize + SlabInterfaceLineCount * SlabInterfaceLineCount * 2) * 2); // overallocate * 2 for on the fly added debug
			ShaderPrint::RequestSpaceForCharacters(256);

			FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder);
			FVisualizeRoughRefractionPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeRoughRefractionPS::FParameters>();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->SampleCountTexture = SampleCountTextureSRV;
			PassParameters->LobStatisticsBuffer = LobStatisticsBufferSRV;
			PassParameters->MiniFontTexture = GetMiniFontTexture();
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintParameters);
			PassParameters->TraceDomainSize = TraceDomainSize;
			PassParameters->SlabInterfaceLineCount = SlabInterfaceLineCount;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(ScreenPassSceneColor.Texture, ERenderTargetLoadAction::ELoad);

			FVisualizeRoughRefractionPS::FPermutationDomain PermutationVector;
			TShaderMapRef<FVisualizeRoughRefractionPS> PixelShader(View.ShaderMap, PermutationVector);

			FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

			FPixelShaderUtils::AddFullscreenPass<FVisualizeRoughRefractionPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("Strata::VisualizeRoughRefractionPS"),
				PixelShader, PassParameters, View.ViewRect, PreMultipliedColorTransmittanceBlend);
		}
	}
#endif
}


} // namespace Strata