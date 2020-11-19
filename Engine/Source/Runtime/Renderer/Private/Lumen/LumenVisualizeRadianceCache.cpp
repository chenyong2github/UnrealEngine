// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenVisualizeRadianceCache.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "LumenRadianceCache.h"
#include "DeferredShadingRenderer.h"

int32 GLumenRadianceCacheVisualize = 0;
FAutoConsoleVariableRef CVarLumenRadianceCacheVisualize(
	TEXT("r.Lumen.RadianceCache.Visualize"),
	GLumenRadianceCacheVisualize,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GLumenRadianceCacheVisualizeRadiusScale = .05f;
FAutoConsoleVariableRef CVarLumenRadianceCacheVisualizeRadiusScale(
	TEXT("r.Lumen.RadianceCache.VisualizeRadiusScale"),
	GLumenRadianceCacheVisualizeRadiusScale,
	TEXT("Scales the size of the spheres used to visualize radiance cache samples."),
	ECVF_RenderThreadSafe
);

int32 GLumenRadianceCacheVisualizeClipmapIndex = -1;
FAutoConsoleVariableRef CVarLumenRadianceCacheVisualizeClipmapIndex(
	TEXT("r.Lumen.RadianceCache.VisualizeClipmapIndex"),
	GLumenRadianceCacheVisualizeClipmapIndex,
	TEXT("Selects which radiance cache clipmap should be visualized. -1 visualizes all clipmaps at once."),
	ECVF_RenderThreadSafe
);

int32 GLumenRadianceCacheVisualizeProbeRadius = 0;
FAutoConsoleVariableRef CVarLumenRadianceCacheVisualizeProbeRadius(
	TEXT("r.Lumen.RadianceCache.VisualizeProbeRadius"),
	GLumenRadianceCacheVisualizeProbeRadius,
	TEXT("Whether to visualize radiance cache probe radius"),
	ECVF_RenderThreadSafe
);

BEGIN_SHADER_PARAMETER_STRUCT(FVisualizeRadianceCacheCommonParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
	SHADER_PARAMETER(FVector, ProbeCoordToWorldCenterBias)
	SHADER_PARAMETER(float, ProbeCoordToWorldCenterScale)
	SHADER_PARAMETER(float, VisualizeProbeRadiusScale)
	SHADER_PARAMETER(uint32, ProbeClipmapIndex)
END_SHADER_PARAMETER_STRUCT()

class FVisualizeRadianceCacheVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeRadianceCacheVS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeRadianceCacheVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeRadianceCacheCommonParameters, VisualizeCommonParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeRadianceCacheVS,"/Engine/Private/Lumen/LumenVisualizeRadianceCache.usf", "VisualizeRadianceCacheVS", SF_Vertex);

class FVisualizeRadianceCachePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeRadianceCachePS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeRadianceCachePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeRadianceCacheCommonParameters, VisualizeCommonParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeRadianceCachePS, "/Engine/Private/Lumen/LumenVisualizeRadianceCache.usf", "VisualizeRadianceCachePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FVisualizeRadianceCacheParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeRadianceCacheVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeRadianceCachePS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderLumenRadianceCacheVisualization(FRDGBuilder& GraphBuilder)
{
	if (GAllowLumenScene
		&& DoesPlatformSupportLumenGI(ShaderPlatform)
		&& Views.Num() == 1
		&& Views[0].ViewState
		&& LumenRadianceCache::IsEnabled(Views[0])
		&& GLumenRadianceCacheVisualize != 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "VisualizeLumenRadianceCache");

		const FViewInfo& View = Views[0];
		const FRadianceCacheState& RadianceCacheState = Views[0].ViewState->RadianceCacheState;

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
		FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor());
		FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ);

		const int32 VisualizationClipmapIndex = FMath::Clamp(GLumenRadianceCacheVisualizeClipmapIndex, -1, RadianceCacheState.Clipmaps.Num() - 1);
		for (int32 ClipmapIndex = 0; ClipmapIndex < RadianceCacheState.Clipmaps.Num(); ++ClipmapIndex)
		{
			if (VisualizationClipmapIndex != -1 && VisualizationClipmapIndex != ClipmapIndex)
			{
				continue;
			}

			const FRadianceCacheClipmap& Clipmap = RadianceCacheState.Clipmaps[ClipmapIndex];

			FVisualizeRadianceCacheCommonParameters VisualizeCommonParameters;
			VisualizeCommonParameters.View = View.ViewUniformBuffer;
			LumenRadianceCache::GetParameters(View, GraphBuilder, VisualizeCommonParameters.RadianceCacheParameters);
			VisualizeCommonParameters.VisualizeProbeRadiusScale = GLumenRadianceCacheVisualizeRadiusScale;
			VisualizeCommonParameters.ProbeClipmapIndex = ClipmapIndex;
			VisualizeCommonParameters.ProbeCoordToWorldCenterBias = Clipmap.ProbeCoordToWorldCenterBias;
			VisualizeCommonParameters.ProbeCoordToWorldCenterScale = Clipmap.ProbeCoordToWorldCenterScale;

			FVisualizeRadianceCacheParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeRadianceCacheParameters>();
			PassParameters->VS.VisualizeCommonParameters = VisualizeCommonParameters;
			PassParameters->PS.VisualizeCommonParameters = VisualizeCommonParameters;

			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
				SceneDepth,
				ERenderTargetLoadAction::ENoAction,
				ERenderTargetLoadAction::ELoad,
				FExclusiveDepthStencil::DepthWrite_StencilWrite);
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);

			const int32 NumInstancesPerClipmap = LumenRadianceCache::GetClipmapGridResolution() * LumenRadianceCache::GetClipmapGridResolution() * LumenRadianceCache::GetClipmapGridResolution();

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("Visualize Radiance Cache Clipmap:%d", ClipmapIndex),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, NumInstancesPerClipmap](FRHICommandList& RHICmdList)
				{
					TShaderMapRef<FVisualizeRadianceCacheVS> VertexShader(View.ShaderMap);
					TShaderMapRef<FVisualizeRadianceCachePS> PixelShader(View.ShaderMap);

					RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNear>::GetRHI();
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

					RHICmdList.SetStreamSource(0, NULL, 0);
					RHICmdList.DrawIndexedPrimitive(GCubeIndexBuffer.IndexBufferRHI, 0, 0, 8, 0, 2 * 6, NumInstancesPerClipmap);
				});
		}
	}
}

void FDeferredShadingSceneRenderer::LumenRadianceCachePDIVisualization()
{
	if (GAllowLumenScene
		&& DoesPlatformSupportLumenGI(ShaderPlatform)
		&& Views.Num() == 1
		&& LumenRadianceCache::IsEnabled(Views[0])
		&& GLumenRadianceCacheVisualizeProbeRadius != 0)
	{
		const FRadianceCacheState& RadianceCacheState = Views[0].ViewState->RadianceCacheState;

		FViewElementPDI ViewPDI(&Views[0], nullptr, &Views[0].DynamicPrimitiveCollector);

		const int32 VisualizationClipmapIndex = FMath::Clamp(GLumenRadianceCacheVisualizeClipmapIndex, -1, RadianceCacheState.Clipmaps.Num() - 1);
		for (int32 ClipmapIndex = 0; ClipmapIndex < RadianceCacheState.Clipmaps.Num(); ++ClipmapIndex)
		{
			if (VisualizationClipmapIndex != -1 && VisualizationClipmapIndex != ClipmapIndex)
			{
				continue;
			}

			const uint8 MarkerHue = (ClipmapIndex * 100) & 0xFF;
			const uint8 MarkerSaturation = 0xFF;
			const uint8 MarkerValue = 0xFF;

			FLinearColor MarkerColor = FLinearColor::MakeFromHSV8(MarkerHue, MarkerSaturation, MarkerValue);
			MarkerColor.A = 0.5f;

			const FRadianceCacheClipmap& Clipmap = RadianceCacheState.Clipmaps[ClipmapIndex];
			const int32 ClipmapResolution = LumenRadianceCache::GetClipmapGridResolution();
			const int32 StartProbeIndex = ClipmapResolution / 2;

			for (int32 ProbeZ = StartProbeIndex; ProbeZ < StartProbeIndex + 1; ++ProbeZ)
			{
				for (int32 ProbeY = StartProbeIndex; ProbeY < StartProbeIndex + 1; ++ProbeY)
				{
					for (int32 ProbeX = StartProbeIndex; ProbeX < StartProbeIndex + 1; ++ProbeX)
					{
						const FVector ProbeWorldPosition = Clipmap.ProbeCoordToWorldCenterScale * FVector(ProbeX, ProbeY, ProbeZ) + Clipmap.ProbeCoordToWorldCenterBias;
						DrawWireSphere(&ViewPDI, ProbeWorldPosition, MarkerColor, Clipmap.ProbeTMin, 32, SDPG_World);
					}
				}
			}
		}
	}
}
