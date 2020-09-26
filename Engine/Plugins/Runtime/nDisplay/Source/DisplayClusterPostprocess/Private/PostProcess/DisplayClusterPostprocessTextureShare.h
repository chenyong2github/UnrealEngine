// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterRenderViewport.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

/**
 * Shared viewport Postprocess projection policy
 */
class FDisplayClusterPostprocessTextureShare
	: public IDisplayClusterPostProcess
{
	struct FSharedViewport
	{
		FString  TextureName;
		FIntRect Rect;

		FSharedViewport(const FString& InName, const FIntRect& InRect)
			: TextureName(InName), Rect(InRect)
		{ }
	};

public:
	virtual ~FDisplayClusterPostprocessTextureShare()
	{
		Release();
	};

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterPostprocessTextureShare
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsPostProcessRenderTargetBeforeWarpBlendRequired() override;
	virtual void PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture, const TArray<FDisplayClusterRenderViewport>& RenderViewports) const override;
	virtual void PerformUpdateViewport(const FViewport& MainViewport, const TArray<FDisplayClusterRenderViewport>& RenderViewports) override;

	virtual void InitializePostProcess(const TMap<FString, FString>& Parameters) override;

protected:
	void Release();

	virtual bool SendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const;
	virtual bool ReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const;

	virtual bool CreateResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName, const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
	{ return true; };

	virtual bool OpenResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName) const
	{ return true; };

	virtual bool BeginSession_RenderThread(FRHICommandListImmediate& RHICmdList) const
	{ return true; };

	virtual bool EndSession_RenderThread(FRHICommandListImmediate& RHICmdList) const
	{ return true; };

	virtual bool CreateResource(const FString& ShareName, const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const;
	virtual bool OpenResource(const FString& ShareName) const;
	
	virtual bool BeginSession() const;
	virtual bool EndSession() const;

private:
	void InitializeResources_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture, const TArray<FDisplayClusterRenderViewport>& RenderViewports) const;
	void SendViewports_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture, const TArray<FDisplayClusterRenderViewport>& RenderViewports) const;
	void ReceiveViewports_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const;

	bool ImplCreateResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const;
	bool ImplOpenResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName) const;

	bool ImplSendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const;
	bool ImplReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const;

	void InitializeResources(const FViewport& MainViewport, const TArray<FDisplayClusterRenderViewport>& RenderViewports);

	bool ImplCreateResource(const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const;
	bool ImplOpenResource(const FString& ShareName) const;

private:
	bool bIsEnabled = false;
	TMap<FString, bool>    ShareViewportsMap;
	TMap<FString, FString> DestinationViewportsMap;

	mutable TArray<FString> ShareResourceNames;
	mutable FCriticalSection DataGuard;

	int TestDuplicateTextures = 0;
	int TestRepeatCopy = 0;
};
