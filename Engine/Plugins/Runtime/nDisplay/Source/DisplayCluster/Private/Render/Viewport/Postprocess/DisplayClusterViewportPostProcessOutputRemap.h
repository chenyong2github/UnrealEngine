// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

class IDisplayClusterViewportManager;
class FDisplayClusterRender_MeshComponent;
class UStaticMesh;

/**
 * Display Cluster Viewport Post Process Output Remap
 */
class FDisplayClusterViewportPostProcessOutputRemap
{
public:
	FDisplayClusterViewportPostProcessOutputRemap();
	virtual ~FDisplayClusterViewportPostProcessOutputRemap();

	// Game thread update calls
	bool UpdateConfiguration_ExternalFile(const FString& InExternalFile);
	bool UpdateConfiguration_StaticMesh(UStaticMesh* InStaticMesh);
	void UpdateConfiguration_Disabled();

	bool IsEnabled() const
	{
		return bIsEnabled;
	}

public:
	void PerformPostProcessFrame_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets = nullptr, const TArray<FRHITexture2D*>* InAdditionalFrameTargets = nullptr) const;

private:
	bool bIsEnabled = false;
	UStaticMesh* StaticMesh = nullptr;
	FString ExternalFile;

	bool bErrorCantFindFileOnce = false;
	bool bErrorFailLoadFromFileOnce = false;

private:
	FDisplayClusterRender_MeshComponent& OutputRemapMesh;
	class IDisplayClusterShaders& ShadersAPI;
};
