// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterDX12CrossGPU.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"

#include "DisplayClusterPostprocessHelpers.h"
#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Device/DisplayClusterRenderViewport.h"

#include "ITextureShareItem.h"

#include "ITextureShareD3D12.h"
#include "ID3D12CrossGPUHeap.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterD3D12CrossGPU
//////////////////////////////////////////////////////////////////////////////////////////////
static bool GetCrossGPUHeap(TSharedPtr<ID3D12CrossGPUHeap>& OutCrossGPUHeap)
{
	static ITextureShareD3D12& SingletoneApi = ITextureShareD3D12::Get();
	return SingletoneApi.GetCrossGPUHeap(OutCrossGPUHeap);
}

bool FDisplayClusterD3D12CrossGPU::CreateResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName, const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	FIntRect SrcTextureRect = ResourceViewport.GetArea();

	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->CreateCrossGPUResource(RHICmdList, ShareName, ResourceTexture, &SrcTextureRect) : false;
}

bool FDisplayClusterD3D12CrossGPU::OpenResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->OpenCrossGPUResource(RHICmdList, ResourceID) : false;
}

bool FDisplayClusterD3D12CrossGPU::BeginSession_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->BeginCrossGPUSession(RHICmdList) : false;
}

bool FDisplayClusterD3D12CrossGPU::EndSession_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->EndCrossGPUSession(RHICmdList) : false;
}

bool FDisplayClusterD3D12CrossGPU::SendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->SendCrossGPUResource(RHICmdList, ResourceID, SrcResource, &SrcTextureRect) : false;
}

bool FDisplayClusterD3D12CrossGPU::ReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->ReceiveCrossGPUResource(RHICmdList, ResourceID, DstResource, &DstTextureRect) : false;
}
