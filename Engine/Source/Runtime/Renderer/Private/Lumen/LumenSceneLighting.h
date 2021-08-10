// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneLighting.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"
#include "RendererPrivateUtils.h"
#include "Lumen.h"
#include "LumenSceneRendering.h"

class FLumenCardRenderer;
class FLumenLight;
class FLumenCardTracingInputs;

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardScatterParameters, )
	RDG_BUFFER_ACCESS(DrawIndirectArgs, ERHIAccess::IndirectArgs)
	RDG_BUFFER_ACCESS(DispatchIndirectArgs, ERHIAccess::IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, QuadAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, QuadData)
	SHADER_PARAMETER(uint32, MaxQuadsPerScatterInstance)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardTileScatterParameters, )
	RDG_BUFFER_ACCESS(DrawIndirectArgs, ERHIAccess::IndirectArgs)
	RDG_BUFFER_ACCESS(DispatchIndirectArgs, ERHIAccess::IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileData)
	SHADER_PARAMETER(uint32, MaxCardTilesPerScatterInstance)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FCullCardsShapeParameters, )
	SHADER_PARAMETER(FVector4, InfluenceSphere)
	SHADER_PARAMETER(FVector3f, LightPosition)
	SHADER_PARAMETER(FVector3f, LightDirection)
	SHADER_PARAMETER(float, LightRadius)
	SHADER_PARAMETER(float, CosConeAngle)
	SHADER_PARAMETER(float, SinConeAngle)
END_SHADER_PARAMETER_STRUCT()

struct FCardCaptureAtlas
{
	FIntPoint Size;
	FRDGTextureRef Albedo;
	FRDGTextureRef Normal;
	FRDGTextureRef Emissive;
	FRDGTextureRef DepthStencil;
};

enum class ECullCardsMode
{
	OperateOnCardPagesToRender,
	OperateOnScene,
	OperateOnSceneForceUpdateForCardPagesToRender,
	OperateOnEmptyList,
	MAX,
};

enum class ECullCardsShapeType
{
	None,
	PointLight,
	SpotLight,
	RectLight
};

class FLumenCardScatterInstance
{
public:
	FCullCardsShapeParameters ShapeParameters;
	ECullCardsShapeType ShapeType;
};

class FLumenCardScatterContext
{
public:
	int32 MaxQuadCount = 0;
	int32 MaxQuadsPerScatterInstance = 0;
	int32 MaxCardTilesPerScatterInstance = 0;
	int32 NumCardPagesToOperateOn = 0;
	int32 MaxScatterInstanceCount = 0;
	ECullCardsMode CardsCullMode;

	FLumenCardScatterParameters CardPageParameters;
	FLumenCardTileScatterParameters CardTileParameters;

	void Build(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FLumenSceneData& LumenSceneData,
		const FLumenCardRenderer& LumenCardRenderer,
		TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
		bool InBuildCardTiles,
		ECullCardsMode InCardsCullMode,
		float UpdateFrequencyScale,
		FCullCardsShapeParameters ShapeParameters,
		ECullCardsShapeType ShapeType);

	void Build(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FLumenSceneData& LumenSceneData,
		const FLumenCardRenderer& LumenCardRenderer,
		TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
		bool InBuildCardTiles,
		ECullCardsMode InCardsCullMode,
		float UpdateFrequencyScale,
		const TArray<FLumenCardScatterInstance, SceneRenderingAllocator>& ScatterInstances,
		int32 InMaxScatterInstanceCount);
};

class FRasterizeToCardsVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRasterizeToCardsVS);
	SHADER_USE_PARAMETER_STRUCT(FRasterizeToCardsVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardScatterParameters, CardScatterParameters)
		SHADER_PARAMETER(uint32, CardScatterInstanceIndex)
		SHADER_PARAMETER(FVector4, InfluenceSphere)
		SHADER_PARAMETER(FVector2D, IndirectLightingAtlasSize)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, RectMinMaxBuffer)
		SHADER_PARAMETER(FVector2D, InvRectMinMaxResolution)
	END_SHADER_PARAMETER_STRUCT()

	class FClampToInfluenceSphere : SHADER_PERMUTATION_BOOL("CLAMP_TO_INFLUENCE_SPHERE");
	class FRectBufferSrc : SHADER_PERMUTATION_BOOL("DIM_RECT_BUFFER_SRC");
	class FRectBufferDst : SHADER_PERMUTATION_BOOL("DIM_RECT_BUFFER_DST");

	using FPermutationDomain = TShaderPermutationDomain<FClampToInfluenceSphere, FRectBufferSrc, FRectBufferDst>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

class FRasterizeToCardTilesVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRasterizeToCardTilesVS);
	SHADER_USE_PARAMETER_STRUCT(FRasterizeToCardTilesVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTileScatterParameters, CardScatterParameters)
		SHADER_PARAMETER(uint32, CardScatterInstanceIndex)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

template<typename PixelShaderType, typename PassParametersType>
void DrawQuadsToAtlas(
	FIntPoint ViewportSize,
	TShaderRefBase<PixelShaderType, FShaderMapPointerTable> PixelShader,
	const PassParametersType* PassParameters,
	FGlobalShaderMap* GlobalShaderMap,
	FRHIBlendState* BlendState,
	FRHICommandList& RHICmdList,
	bool bRectBufferSrc = false,
	bool bRectBufferDst = false)
{
	FRasterizeToCardsVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRasterizeToCardsVS::FClampToInfluenceSphere>(false);
	PermutationVector.Set<FRasterizeToCardsVS::FRectBufferSrc>(bRectBufferSrc);
	PermutationVector.Set<FRasterizeToCardsVS::FRectBufferDst>(bRectBufferDst);
	auto VertexShader = GlobalShaderMap->GetShader<FRasterizeToCardsVS>(PermutationVector);

	DrawQuadsToAtlas(ViewportSize, 
		VertexShader,
		PixelShader, 
		PassParameters, 
		GlobalShaderMap, 
		BlendState, 
		RHICmdList, 
		[](FRHICommandList& RHICmdList, TShaderRefBase<PixelShaderType, FShaderMapPointerTable> Shader, FRHIPixelShader* ShaderRHI, const typename PixelShaderType::FParameters& Parameters)
	{
	});
}

template<typename VertexShaderType, typename PixelShaderType, typename PassParametersType, typename SetParametersLambdaType>
void DrawQuadsToAtlas(
	FIntPoint ViewportSize,
	TShaderRefBase<VertexShaderType, FShaderMapPointerTable> VertexShader,
	TShaderRefBase<PixelShaderType, FShaderMapPointerTable> PixelShader,
	const PassParametersType* PassParameters,
	FGlobalShaderMap* GlobalShaderMap,
	FRHIBlendState* BlendState,
	FRHICommandList& RHICmdList,
	SetParametersLambdaType&& SetParametersLambda,
	uint32 DrawIndirectArgOffset = 0)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	RHICmdList.SetViewport(0, 0, 0.0f, ViewportSize.X, ViewportSize.Y, 1.0f);

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = BlendState;

	GraphicsPSOInit.PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
	SetParametersLambda(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

	RHICmdList.DrawPrimitiveIndirect(PassParameters->VS.CardScatterParameters.DrawIndirectArgs->GetIndirectRHICallBuffer(), DrawIndirectArgOffset);
}

class FClearLumenCardsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearLumenCardsPS);
	SHADER_USE_PARAMETER_STRUCT(FClearLumenCardsPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

// Must match LIGHT_TYPE_* in LumenSceneDirectLighting.usf
enum class ELumenLightType
{
	Directional,
	Point,
	Spot,
	Rect,

	MAX
};

struct FLumenShadowSetup
{
	const FProjectedShadowInfo* VirtualShadowMap;
	const FProjectedShadowInfo* DenseShadowMap;
};

FLumenShadowSetup GetShadowForLumenDirectLighting(FVisibleLightInfo& VisibleLightInfo);

void TraceLumenHardwareRayTracedDirectLightingShadows(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenLight& LumenLight,
	const FLumenCardScatterContext& CardScatterContext,
	FRDGBufferUAVRef ShadowMaskTilesUAV);

class FLumenLight
{
public:
	FString Name;
	ELumenLightType Type;
	const FLightSceneInfo* LightSceneInfo = nullptr;
	uint32 CardScatterInstanceIndex = 0;
	uint32 ShadowMaskTilesOffset = UINT32_MAX;

	bool HasShadowMask() const
	{
		return ShadowMaskTilesOffset != UINT32_MAX;
	}
};

namespace Lumen
{
	void SetDirectLightingDeferredLightUniformBuffer(
		const FViewInfo& View,
		const FLightSceneInfo* LightSceneInfo,
		TUniformBufferBinding<FDeferredLightUniformStruct>& UniformBuffer);

	void CombineLumenSceneLighting(
		FScene* Scene,
		const FViewInfo& View,
		FRDGBuilder& GraphBuilder,
		const FLumenCardTracingInputs& TracingInputs,
		const FLumenCardScatterContext& VisibleCardScatterContext);
};