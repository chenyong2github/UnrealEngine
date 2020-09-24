// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"

class FTextureShareRHI
{
public:
	static bool WriteToShareTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstShareTexture, const FIntRect* SrcTextureRect, bool bIsFormatResampleRequired);
	static bool ReadFromShareTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcShareTexture, FRHITexture* DstTexture, const FIntRect* DstTextureRect, bool bIsFormatResampleRequired);

};
