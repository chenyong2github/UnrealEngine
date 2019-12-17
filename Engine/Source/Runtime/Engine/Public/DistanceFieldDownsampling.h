// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldDownsampling.h
=============================================================================*/

#pragma once

#include "RHI.h"

class FRHICommandListImmediate;

struct FDistanceFieldDownsamplingDataTask
{
	FTexture3DRHIRef VolumeTextureRHI;
	FVector TexelSrcSize;
	FIntVector DstSize;
	FIntVector OffsetInAtlas;
};

class ENGINE_API FDistanceFieldDownsampling
{
public:
	static bool CanDownsample();
	static void GetDownsampledSize(const FIntVector& Size, float Factor, FIntVector& OutDownsampledSize);
	static void FillDownsamplingTask(const FIntVector& SrcSize, const FIntVector& DstSize, const FIntVector& OffsetInAtlas, EPixelFormat Format, FDistanceFieldDownsamplingDataTask& OutDataTask, FUpdateTexture3DData& OutTextureUpdateData);
	static void DispatchDownsampleTasks(FRHICommandListImmediate& RHICmdList, FRHIUnorderedAccessView* DFAtlasUAV, ERHIFeatureLevel::Type FeatureLevel, TArray<FDistanceFieldDownsamplingDataTask>& DownsamplingTasks, TArray<FUpdateTexture3DData>& UpdateTextureData);
};
