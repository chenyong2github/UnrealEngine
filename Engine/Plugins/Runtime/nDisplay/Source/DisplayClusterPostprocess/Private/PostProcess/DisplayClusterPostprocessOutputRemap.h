// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/PostProcess/IDisplayClusterPostProcess.h"

#include "IOutputRemap.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

/**
 * MPCDI projection policy
 */
class FDisplayClusterPostprocessOutputRemap
	: public IDisplayClusterPostProcess
{
public:
	FDisplayClusterPostprocessOutputRemap();
	virtual ~FDisplayClusterPostprocessOutputRemap();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterPostprocessOutputRemap
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsPostProcessRenderTargetAfterWarpBlendRequired() override;

	virtual void PerformPostProcessRenderTargetAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const override;

	virtual void InitializePostProcess(const FString& CfgLine) override;

protected:
	bool InitializeResources_RenderThread(const FIntPoint& ScreenSize) const;

private:
	IOutputRemap& OutputRemapAPI;

	int MeshRef;

	//@todo: remove, use ext paired texture
	mutable FTexture2DRHIRef RTTexture;

	mutable bool bIsRenderResourcesInitialized = false;
	mutable FCriticalSection RenderingResourcesInitializationCS;
};
