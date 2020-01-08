// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

class MAGICLEAPHELPERVULKAN_API FMagicLeapHelperVulkan
{
public:
	static void BlitImage(uint64 SrcName, int32 SrcX, int32 SrcY, int32 SrcZ, int32 SrcWidth, int32 SrcHeight, int32 SrcDepth, uint64 DstName, int32 DstLayer, int32 DstX, int32 DstY, int32 DstZ, int32 DstWidth, int32 DstHeight, int32 DstDepth);
	static void SignalObjects(uint64 SignalObject0, uint64 SignalObject1);
	static void ClearImage(uint64 Dest, const FLinearColor& ClearColor, uint32 BaseMipLevel, uint32 LevelCount, uint32 BaseArrayLayer, uint32 LayerCount);
	static uint64 AliasImageSRGB(const uint64 Allocation, const uint64 AllocationOffset, const uint32 Width, const uint32 Height);
	static void DestroyImageSRGB(void* Dest);
	static bool GetVulkanInstanceExtensionsRequired(TArray<const ANSICHAR*>& Out);
	static bool GetVulkanDeviceExtensionsRequired(struct VkPhysicalDevice_T* pPhysicalDevice, TArray<const ANSICHAR*>& Out);
	static bool GetMediaTexture(FTextureRHIRef& Result, FSamplerStateRHIRef& SamplerResult, const uint64 MediaTextureHandle);
	static void AliasMediaTexture(FRHITexture* DestTexture, FRHITexture* SrcTexture);
};
