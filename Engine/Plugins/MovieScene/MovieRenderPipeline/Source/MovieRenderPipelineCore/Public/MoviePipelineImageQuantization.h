// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Engine/EngineTypes.h"

// Forward Declare
struct FImagePixelData;

namespace UE
{
namespace MoviePipeline
{
	MOVIERENDERPIPELINECORE_API TUniquePtr<FImagePixelData> QuantizeImagePixelDataToBitDepth(const FImagePixelData* InData, const int32 TargetBitDepth);
}
}
	  