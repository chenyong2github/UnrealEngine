// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlintShadingLUTs.h: look up table to be ablew to render luts.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

class FViewInfo;

struct FGlintShadingLUTsStateData
{
	TRefCountPtr<IPooledRenderTarget> GlintShadingLUTs = nullptr;
	FRHITexture2DArray* RHIGlintShadingLUTs = nullptr;

	uint64 GetGPUSizeBytes(bool bLogSizes) const;

	static void Init(FRDGBuilder& GraphBuilder, FViewInfo& View);
};
