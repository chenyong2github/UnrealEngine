// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessHistogram.h: Post processing histogram implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "PostProcess/RenderingCompositionGraph.h"

// derives from TRenderingCompositePassBase<InputCount, OutputCount>
class FRCPassPostProcessHistogram : public TRenderingCompositePassBase<1, 1>
{
public:
	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	// -------------------------------------------

	// IMPORTANT: Optimized algorithm needs groups thread count == 256. Change shader if you want to change group size.
	// changing this number require Histogram.usf to be recompiled
	static const uint32 ThreadGroupSizeX = 16;
	static const uint32 ThreadGroupSizeY = 16;

	static const uint32 HistogramSize = 64;

	// /4 as we store 4 buckets in one ARGB texel
	static const uint32 HistogramTexelCount = HistogramSize / 4;

	// one ThreadGroup processes LoopCountX*LoopCountY blocks of size ThreadGroupSizeX*ThreadGroupSizeY

	// Using smaller loop count (2,2) to increase wave count. Otherwise will only use a few CUs (GPU practically idles).
	// Higher amount of groups is not a problem since new algorithm doesn't need a separate reduction step (all groups accumulate to same histogram with atomic adds).
	static const uint32 LoopCountX = 2;
	static const uint32 LoopCountY = 2;

	// -------------------------------------------

	static FIntPoint ComputeGatherExtent(const FRenderingCompositePassContext& Context);
	static FIntPoint ComputeThreadGroupCount(FIntPoint PixelExtent);
};
