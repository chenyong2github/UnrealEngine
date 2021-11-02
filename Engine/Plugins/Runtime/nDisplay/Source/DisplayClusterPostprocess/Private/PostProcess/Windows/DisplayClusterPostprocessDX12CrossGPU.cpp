// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPostprocessDX12CrossGPU.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"

#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "ITextureShareItem.h"

#include "ITextureShareD3D12.h"
#include "ID3D12CrossGPUHeap.h"

/*

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterPostprocessD3D12CrossGPU
//////////////////////////////////////////////////////////////////////////////////////////////
static bool GetCrossGPUHeap(TSharedPtr<ID3D12CrossGPUHeap>& OutCrossGPUHeap)
{
	static ITextureShareD3D12& SingletonTextureShareD3D12Api = ITextureShareD3D12::Get();
	return SingletonTextureShareD3D12Api.GetCrossGPUHeap(OutCrossGPUHeap);
}

bool FDisplayClusterPostprocessD3D12CrossGPU::CreateResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName, const FDisplayClusterViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	FIntRect SrcTextureRect = ResourceViewport.GetRect();

	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->CreateCrossGPUResource(RHICmdList, ShareName, ResourceTexture, &SrcTextureRect) : false;
}

bool FDisplayClusterPostprocessD3D12CrossGPU::OpenResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->OpenCrossGPUResource(RHICmdList, ResourceID) : false;
}

bool FDisplayClusterPostprocessD3D12CrossGPU::BeginSession_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->BeginCrossGPUSession(RHICmdList) : false;
}

bool FDisplayClusterPostprocessD3D12CrossGPU::EndSession_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->EndCrossGPUSession(RHICmdList) : false;
}

bool FDisplayClusterPostprocessD3D12CrossGPU::SendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->SendCrossGPUResource(RHICmdList, ResourceID, SrcResource, &SrcTextureRect) : false;
}

bool FDisplayClusterPostprocessD3D12CrossGPU::ReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const
{
	TSharedPtr<ID3D12CrossGPUHeap> CrossGPUHeap;
	return GetCrossGPUHeap(CrossGPUHeap) ? CrossGPUHeap->ReceiveCrossGPUResource(RHICmdList, ResourceID, DstResource, &DstTextureRect) : false;
}
*/
