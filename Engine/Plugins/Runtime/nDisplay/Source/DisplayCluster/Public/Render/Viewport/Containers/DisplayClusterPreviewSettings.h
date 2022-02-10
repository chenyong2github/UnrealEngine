// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDisplayClusterPreviewSettings
{
	// Preview RTT size multiplier
	float PreviewRenderTargetRatioMult = 1.f;

	// The maximum dimension of any texture for preview
	int32 PreviewMaxTextureSize = 2048;

	// Allow mGPU in editor mode
	bool bAllowMultiGPURenderingInEditor = false;

	int32 MinGPUIndex = 1;
	int32 MaxGPUIndex = 1;
};

