// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterPostprocessTextureShare.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

/**
 * Shared viewport Postprocess projection policy

class FDisplayClusterPostprocessD3D12CrossGPU
	: public FDisplayClusterPostprocessTextureShare
{
public:
	virtual ~FDisplayClusterPostprocessD3D12CrossGPU() {};

protected:
	virtual bool SendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const override;
	virtual bool ReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const override;

	virtual bool CreateResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName, const FDisplayClusterViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const override;
	virtual bool OpenResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName) const override;

	virtual bool BeginSession_RenderThread(FRHICommandListImmediate& RHICmdList) const override;
	virtual bool EndSession_RenderThread(FRHICommandListImmediate& RHICmdList) const override;

	virtual bool CreateResource(const FString& ShareName, const FDisplayClusterViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const override
	{ return true; };

	virtual bool OpenResource(const FString& ShareName) const override
	{ return true; };

	virtual bool BeginSession() const override
	{ return true; };

	virtual bool EndSession() const override
	{ return true; };
};
 */
