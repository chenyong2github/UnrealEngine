// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/PostProcess/IDisplayClusterPostProcess.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"

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
	virtual bool IsPostProcessFrameAfterWarpBlendRequired() const override;
	virtual void PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets = nullptr, const TArray<FRHITexture2D*>* InAdditionalFrameTargets = nullptr) const override;
	virtual void InitializePostProcess(class IDisplayClusterViewportManager& InViewportManager, const TMap<FString, FString>& Parameters) override;

	virtual bool ShouldUseAdditionalFrameTargetableResource() const
	{ return true; }

protected:
	bool bIsInitialized = false;

private:
	FDisplayClusterRender_MeshComponentProxy& MeshComponentProxy;
	class IDisplayClusterShaders& ShadersAPI;
};
