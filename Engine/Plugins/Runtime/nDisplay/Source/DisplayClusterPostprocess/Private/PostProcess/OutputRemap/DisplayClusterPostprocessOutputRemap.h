// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PostProcess/DisplayClusterPostprocessBase.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

class FDisplayClusterRender_MeshComponent;

/**
 * MPCDI projection policy
 */
class FDisplayClusterPostprocessOutputRemap
	: public FDisplayClusterPostprocessBase
{
public:
	FDisplayClusterPostprocessOutputRemap(const FString& PostprocessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess);
	virtual ~FDisplayClusterPostprocessOutputRemap();

public:
	virtual const FString GetTypeId() const override;
	virtual bool HandleStartScene(IDisplayClusterViewportManager* InViewportManager) override;
	virtual void HandleEndScene(IDisplayClusterViewportManager* InViewportManager) override;
	virtual void Tick() override;

	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterPostprocessOutputRemap
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsPostProcessFrameAfterWarpBlendRequired() const override
	{ return bIsInitialized; }

	virtual bool ShouldUseAdditionalFrameTargetableResource() const
	{ return true; }

	virtual void PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets = nullptr, const TArray<FRHITexture2D*>* InAdditionalFrameTargets = nullptr) const override;

private:
	bool ImplInitialize(IDisplayClusterViewportManager* InViewportManager);
	void ImplRelease();

protected:
	bool bIsInitialized = false;

private:
	FDisplayClusterRender_MeshComponent& OutputRemapMesh;
	class IDisplayClusterShaders& ShadersAPI;
};
