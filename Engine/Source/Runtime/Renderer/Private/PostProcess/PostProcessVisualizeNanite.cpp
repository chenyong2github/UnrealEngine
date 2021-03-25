// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessVisualizeNanite.h"
#include "NaniteVisualizationData.h"
#include "UnrealEngine.h"

class FVisualizeNanitePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisualizeNanitePS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeNanitePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FLinearColor, SelectionColor)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeNanitePS, "/Engine/Private/PostProcessVisualizeBuffer.usf", "MainPS", SF_Pixel);

void AddVisualizeNanitePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture Output, const Nanite::FRasterResults& RasterResults)
{
	const FNaniteVisualizationData& VisualizationData = GetNaniteVisualizationData();
	if (VisualizationData.IsActive())
	{
		// Any individual mode
		if (VisualizationData.GetActiveModeID() > 0)
		{
			if (ensure(RasterResults.Visualizations.Num() == 1))
			{
				const Nanite::FVisualizeResult& Visualization = RasterResults.Visualizations[0];
				AddDrawTexturePass(
					GraphBuilder,
					View,
					Visualization.ModeOutput,
					Output.Texture,
					View.ViewRect.Min,
					View.ViewRect.Min,
					View.ViewRect.Size()
				);
			}
		}
		// Overview mode
		else if (VisualizationData.GetActiveModeID() == 0)
		{
			struct FTileLabel
			{
				FString Label;
				FIntPoint Location;
			};

			TArray<FTileLabel> TileLabels;
			TileLabels.Reserve(RasterResults.Visualizations.Num());

			const int32 MaxTilesX = 4;
			const int32 MaxTilesY = 4;
			const int32 TileWidth = View.ViewRect.Width() / MaxTilesX;
			const int32 TileHeight = View.ViewRect.Height() / MaxTilesY;

			FRHISamplerState* BilinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			FScreenPassRenderTarget OutputTarget(Output.Texture, View.ViewRect, ERenderTargetLoadAction::ELoad);

			for (int32 TileIndex = 0; TileIndex < RasterResults.Visualizations.Num(); ++TileIndex)
			{
				const Nanite::FVisualizeResult& Visualization = RasterResults.Visualizations[TileIndex];

				// The list can contain invalid entries to keep the indices static.
				if (Visualization.bSkippedTile)
				{
					continue;
				}

				const int32 TileX = TileIndex % MaxTilesX;
				const int32 TileY = TileIndex / MaxTilesX;

				FScreenPassTextureViewport OutputViewport(OutputTarget);
				OutputViewport.Rect.Min = FIntPoint(TileX * TileWidth, TileY * TileHeight);
				OutputViewport.Rect.Max = OutputViewport.Rect.Min + FIntPoint(TileWidth, TileHeight);

				const FLinearColor SelectionColor = FLinearColor::Transparent;

				FVisualizeNanitePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeNanitePS::FParameters>();
				PassParameters->Output = GetScreenPassTextureViewportParameters(OutputViewport);
				PassParameters->RenderTargets[0] = OutputTarget.GetRenderTargetBinding();
				PassParameters->InputTexture = Visualization.ModeOutput;
				PassParameters->InputSampler = BilinearClampSampler;
				PassParameters->SelectionColor = SelectionColor;

				const FScreenPassTextureViewport InputViewport(Visualization.ModeOutput);

				TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
				TShaderMapRef<FVisualizeNanitePS> PixelShader(View.ShaderMap);
				FRHIBlendState* BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();

				const FText DisplayText = VisualizationData.GetModeDisplayName(Visualization.ModeName);
				const FString& DisplayName = DisplayText.ToString();

				AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Tile: %s", *DisplayName), View, OutputViewport, InputViewport, VertexShader, PixelShader, BlendState, PassParameters);

				FTileLabel TileLabel;
				TileLabel.Label = DisplayName;
				TileLabel.Location.X = 8 + TileX * TileWidth;
				TileLabel.Location.Y = (TileY + 1) * TileHeight - 19;
				TileLabels.Add(TileLabel);
			}

			AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("Labels"), View, OutputTarget, [LocalTileLabels = MoveTemp(TileLabels)](FCanvas& Canvas)
			{
				const FLinearColor LabelColor(1, 1, 0);
				for (const FTileLabel& TileLabel : LocalTileLabels)
				{
					Canvas.DrawShadowedString(TileLabel.Location.X, TileLabel.Location.Y, *TileLabel.Label, GetStatsFont(), LabelColor);
				}
			});
		}
	}
}