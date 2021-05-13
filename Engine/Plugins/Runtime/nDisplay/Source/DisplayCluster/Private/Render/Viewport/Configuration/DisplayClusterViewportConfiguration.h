// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/DisplayClusterObjectRef.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"

struct FDisplayClusterRenderFrameSettings;

class FDisplayClusterViewportManager;
class ADisplayClusterRootActor;
class UDisplayClusterConfigurationData;
struct FDisplayClusterConfigurationRenderFrame;
struct FDisplayClusterConfigurationViewportPreview;

class FDisplayClusterViewportConfiguration
{
public:
	FDisplayClusterViewportConfiguration(FDisplayClusterViewportManager& InViewportManager)
		: ViewportManager(InViewportManager)
	{}

	~FDisplayClusterViewportConfiguration()
	{}

public:
	// Return true, if root actor ref changed
	bool SetRootActor(ADisplayClusterRootActor* InRootActorPtr);
	ADisplayClusterRootActor* GetRootActor() const;

	const FDisplayClusterRenderFrameSettings& GetRenderFrameSettings() const
	{ 
		check(IsInGameThread());

		return RenderFrameSettings; 
	}

	bool UpdateConfiguration(EDisplayClusterRenderFrameMode InRenderMode, const FString& InClusterNodeId);

#if WITH_EDITOR
	bool UpdatePreviewConfiguration(const FDisplayClusterConfigurationViewportPreview& PreviewConfiguration);
#endif

private:
	void ImplUpdateRenderFrameConfiguration(const FDisplayClusterConfigurationRenderFrame& InRenderFrameConfiguration);
	void ImplUpdateConfiguration(const TArray<FString>& InClusterNodeIds, ADisplayClusterRootActor& InRootActor, const UDisplayClusterConfigurationData& InConfigurationData);
	void ImplUpdateConfigurationICVFX(ADisplayClusterRootActor& InRootActor, const UDisplayClusterConfigurationData& InConfigurationData);
	void ImplUpdateConfigurationVisibility(ADisplayClusterRootActor& InRootActor, const UDisplayClusterConfigurationData& InConfigurationData);

	void ImplUpdateConfiguration_PostProcess(const FString& InClusterNodeId, const UDisplayClusterConfigurationData& ConfigurationData);

private:
	FDisplayClusterViewportManager&    ViewportManager;
	FDisplayClusterActorRef            RootActorRef;
	FDisplayClusterRenderFrameSettings RenderFrameSettings;
};

