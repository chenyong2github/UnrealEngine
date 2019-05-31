// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessVisualizeBuffer.h: Post processing buffer visualization
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "PostProcess/RenderingCompositionGraph.h"

// derives from TRenderingCompositePassBase<InputCount, OutputCount>
// ePId_Input0: SceneColor
// ePId_Input1: SeparateTranslucency
class FRCPassPostProcessVisualizeBuffer : public TRenderingCompositePassBase<2, 1>
{
public:

	/** Data for a single buffer overview tile **/
	struct TileData
	{
		FRenderingCompositeOutputRef Source;
		FString Name;
		bool bIsSelected;

		TileData(FRenderingCompositeOutputRef InSource, const FString& InName, bool bSelected)
		: Source (InSource)
		, Name (InName)
		, bIsSelected(bSelected)
		{

		}
	};

	// constructor
	void AddVisualizationBuffer(FRenderingCompositeOutputRef InSource, const FString& InName, bool bIsSelected = false);

	// interface FRenderingCompositePass ---------

	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:

	// @return VertexShader
	template <bool bDrawTileTemplate>
	FShader* SetShaderTempl(const FRenderingCompositePassContext& Context);

	TArray<TileData> Tiles;
};
