// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"

#include "IMPCDI.h"
#include "IPicpMPCDI.h"
#include "MPCDIRegion.h"

#include "Overlay/PicpProjectionOverlayRender.h"

class FPicpMPCDIShader
{
public:
	static bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData, FPicpProjectionOverlayViewportData* ViewportOverlayData);
};
