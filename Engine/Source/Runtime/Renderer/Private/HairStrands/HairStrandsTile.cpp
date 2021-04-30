// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsTile.h"
#include "HairStrandsUtils.h"
#include "HairStrandsInterface.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "MeshPassProcessor.inl"
#include "ScenePrivate.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsTilePassVS::FParameters GetHairStrandsTileParameters(const FHairStrandsTiles& In)
{
	FHairStrandsTilePassVS::FParameters Out;
	Out.bRectPrimitive			= In.bRectPrimitive ? 1 : 0;
	Out.TileOutputResolution	= In.Resolution;
	Out.TileDataBuffer			= In.TileDataSRV;
	Out.TileIndirectBuffer		= In.TileIndirectDrawBuffer;
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate indirect draw and indirect dispatch buffers

class FHairStrandsTileCopyArgsPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsTileCopyArgsPassCS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsTileCopyArgsPassCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, TileCountXY)
		SHADER_PARAMETER(uint32, TilePerThread_GroupSize)
		SHADER_PARAMETER(uint32, bRectPrimitive)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TileCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileIndirectDrawBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileIndirectDispatchBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TilePerThreadIndirectDispatchBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TILE_COPY_ARGS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsTileCopyArgsPassCS, "/Engine/Private/HairStrands/HairStrandsVisibilityTile.usf", "MainCS", SF_Compute);

void AddHairStrandsCopyArgsTilesPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FHairStrandsTiles& TileData)
{
	TShaderMapRef<FHairStrandsTileCopyArgsPassCS> ComputeShader(View.ShaderMap);
	FHairStrandsTileCopyArgsPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairStrandsTileCopyArgsPassCS::FParameters>();
	PassParameters->TileCountXY = TileData.TileCountXY;
	PassParameters->TilePerThread_GroupSize = TileData.TilePerThread_GroupSize;
	PassParameters->bRectPrimitive = TileData.bRectPrimitive ? 1 : 0;
	PassParameters->TileCountBuffer = GraphBuilder.CreateSRV(TileData.TileCountBuffer, PF_R32_UINT);
	PassParameters->TileIndirectDrawBuffer = GraphBuilder.CreateUAV(TileData.TileIndirectDrawBuffer, PF_R32_UINT);
	PassParameters->TileIndirectDispatchBuffer = GraphBuilder.CreateUAV(TileData.TileIndirectDispatchBuffer, PF_R32_UINT);
	PassParameters->TilePerThreadIndirectDispatchBuffer = GraphBuilder.CreateUAV(TileData.TilePerThreadIndirectDispatchBuffer, PF_R32_UINT);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTileCopyArgs"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate tiles data based on input texture (hair pixel coverage)

class FHairStrandsTileGenerationPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsTileGenerationPassCS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsTileGenerationPassCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, ViewResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint4>, InputTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileDataBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TILE_GENERATION"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FHairStrandsTileGenerationPassCS, "/Engine/Private/HairStrands/HairStrandsVisibilityTile.usf", "TileMainCS", SF_Compute);

FHairStrandsTiles AddHairStrandsGenerateTilesPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& InputTexture)
{
	FHairStrandsTiles Out;

	check(FHairStrandsTiles::TilePerThread_GroupSize == 64); // If this value change, we need to update the shaders using 
	check(FHairStrandsTiles::TileSize == 8); // only size supported for now
	const FIntPoint InputResolution = InputTexture->Desc.Extent;
	Out.TileCountXY = FIntPoint(FMath::CeilToInt(InputResolution.X / float(FHairStrandsTiles::TileSize)), FMath::CeilToInt(InputResolution.Y / float(FHairStrandsTiles::TileSize)));
	Out.TileCount = Out.TileCountXY.X * Out.TileCountXY.Y;
	Out.Resolution = InputResolution;
	Out.bRectPrimitive = GRHISupportsRectTopology;
	Out.TileCountBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, 1), TEXT("Hair.TileCountBuffer"));
	Out.TileIndirectDrawBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("Hair.TileIndirectDrawBuffer"));
	Out.TileIndirectDispatchBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Hair.TileIndirectDispatchBuffer"));
	Out.TilePerThreadIndirectDispatchBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Hair.TilePerThreadIndirectDispatchBuffer"));
	Out.TileDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Out.TileCount), TEXT("Hair.TileDataBuffer"));

	FRDGBufferUAVRef TileCountUAV = GraphBuilder.CreateUAV(Out.TileCountBuffer, PF_R32_UINT);
	AddClearUAVPass(GraphBuilder, TileCountUAV, 0u);

	TShaderMapRef<FHairStrandsTileGenerationPassCS> ComputeShader(View.ShaderMap);
	FHairStrandsTileGenerationPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairStrandsTileGenerationPassCS::FParameters>();
	PassParameters->ViewResolution = View.ViewRect.Size();
	PassParameters->InputTexture = InputTexture;
	PassParameters->TileDataBuffer = GraphBuilder.CreateUAV(Out.TileDataBuffer, PF_R16G16_UINT);
	PassParameters->TileCountBuffer = TileCountUAV;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTileClassification"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(InputResolution, FHairStrandsTiles::TileSize));

	Out.TileDataSRV = GraphBuilder.CreateSRV(Out.TileDataBuffer, PF_R16G16_UINT);

	// Initialize indirect dispatch buffer, based on the indirect draw bugger
	AddHairStrandsCopyArgsTilesPass(GraphBuilder, View, Out);

	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FHairStrandsTilePassVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
}

void FHairStrandsTilePassVS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("SHADER_TILE_VS"), 1);
}

IMPLEMENT_GLOBAL_SHADER(FHairStrandsTilePassVS, "/Engine/Private/HairStrands/HairStrandsVisibilityTile.usf", "MainVS", SF_Vertex);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FHairStrandsTileDebugPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsTileDebugPassPS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsTileDebugPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairStrandsTilePassVS::FParameters, TileParameters)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5; //TODO
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TILE_DEBUG"), 1); 
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsTileDebugPassPS, "/Engine/Private/HairStrands/HairStrandsVisibilityTile.usf", "MainPS", SF_Pixel);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AddHairStrandsDebugTilePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& ColorTexture,
	const FHairStrandsTiles& TileData)
{	
	const FIntPoint OutputResolution = View.ViewRect.Size();
	
	FHairStrandsTileDebugPassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairStrandsTileDebugPassPS::FParameters>();
	PassParameters->TileParameters = GetHairStrandsTileParameters(TileData);
	PassParameters->OutputResolution = OutputResolution;

	TShaderMapRef<FHairStrandsTilePassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairStrandsTileDebugPassPS> PixelShader(View.ShaderMap);

	PassParameters->RenderTargets[0] = FRenderTargetBinding(ColorTexture, ERenderTargetLoadAction::ELoad);
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsTileDebugPass"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, VertexShader, PixelShader, OutputResolution](FRHICommandList& RHICmdList)
		{
			FHairStrandsTilePassVS::FParameters ParametersVS = PassParameters->TileParameters;

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PassParameters->TileParameters.bRectPrimitive > 0 ? PT_RectList : PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			RHICmdList.SetViewport(0, 0, 0.0f, OutputResolution.X, OutputResolution.Y, 1.0f);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(PassParameters->TileParameters.TileIndirectBuffer->GetRHI(), 0);
		});
}