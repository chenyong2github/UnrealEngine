// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessBokehDOF.h: Post processing lens blur implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "PostProcess/RenderingCompositionGraph.h"

struct FDepthOfFieldStats
{
	FDepthOfFieldStats()
		: bNear(true)
		, bFar(true)
	{
	}

	bool bNear;
	bool bFar;
};


// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
// ePId_Input0: Color input
class FRCPassPostProcessVisualizeDOF : public TRenderingCompositePassBase<1, 1>
{
public:
	// constructor
	FRCPassPostProcessVisualizeDOF(const FDepthOfFieldStats& InDepthOfFieldStats)
		: DepthOfFieldStats(InDepthOfFieldStats)
	{}

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	FDepthOfFieldStats DepthOfFieldStats;
};
