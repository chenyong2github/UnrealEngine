// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IStereoLayers.h"
#include "XRSwapChain.h"

struct FOpenXRLayer
{
	IStereoLayers::FLayerDesc	Desc;
	FXRSwapChainPtr				Swapchain;
	FXRSwapChainPtr				LeftSwapchain;
	FVector2D					SwapchainSize;
	bool						bUpdateTexture;

	FOpenXRLayer(const IStereoLayers::FLayerDesc& InLayerDesc)
		: Desc(InLayerDesc)
		, Swapchain()
		, LeftSwapchain()
		, bUpdateTexture(false)
	{ }

	void SetLayerId(uint32 InId) { Desc.SetLayerId(InId); }
	uint32 GetLayerId() const { return Desc.GetLayerId(); }

	bool NeedReAllocateTexture();
	bool NeedReAllocateLeftTexture();

	FIntRect GetViewport() const;
	FVector2D GetQuadSize() const;
};

bool GetLayerDescMember(const FOpenXRLayer& Layer, IStereoLayers::FLayerDesc& OutLayerDesc);
void SetLayerDescMember(FOpenXRLayer& OutLayer, const IStereoLayers::FLayerDesc& InLayerDesc);
void MarkLayerTextureForUpdate(FOpenXRLayer& Layer);
