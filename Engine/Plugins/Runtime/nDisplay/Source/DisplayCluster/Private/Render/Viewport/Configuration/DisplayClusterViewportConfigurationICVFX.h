// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/DisplayClusterViewportStrings.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettingsICVFX.h"

class FDisplayClusterViewportManager;
class FDisplayClusterViewport;

class UDisplayClusterConfigurationViewport;
class UDisplayClusterConfigurationData;

struct FDisplayClusterConfigurationPostRender_Override;
struct FDisplayClusterConfigurationPostRender_BlurPostprocess;
struct FDisplayClusterConfigurationPostRender_GenerateMips;
struct FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings;
struct FDisplayClusterConfigurationICVFX_LightcardSettings;
struct FDisplayClusterConfigurationICVFX_CameraSettings;
struct FDisplayClusterConfigurationICVFX_StageSettings;


class ADisplayClusterRootActor;
class UCameraComponent;

class FDisplayClusterViewportConfigurationICVFX
{
public:
	FDisplayClusterViewportConfigurationICVFX(FDisplayClusterViewportManager& InViewportManager, ADisplayClusterRootActor& InRootActor, const UDisplayClusterConfigurationData& InConfigurationData);

public:
	void Update();

	bool                     CreateProjectionPolicy(const FString& InViewportId, const FString& InResourceId, bool bIsCameraProjection, TSharedPtr<class IDisplayClusterProjectionPolicy>& OutProjPolicy) const;
	FDisplayClusterViewport* CreateViewport(const FString& InViewportId, const FString& InResourceId, TSharedPtr<IDisplayClusterProjectionPolicy>& InProjectionPolicy) const;
	FDisplayClusterViewport* FindViewport(const FString& InViewportId, const FString& InResourceId) const;

private:
	void ImplBeginReallocateViewports();
	void ImplFinishReallocateViewports();
	void ImplGetCameras(TArray<class FDisplayClusterViewportConfigurationCameraICVFX*>& OutCameras);
	EDisplayClusterViewportICVFXFlags ImplGetTargetViewports(TArray<FDisplayClusterViewport*>& OutTargets);

	bool CreateLightcardViewport(FDisplayClusterViewport& BaseViewport);
	bool ImplCreateLightcardViewport(FDisplayClusterViewport& BaseViewport, bool bIsOpenColorIO);

protected:
	void UpdateTargetViewportConfiguration(TArray<FDisplayClusterViewport*>& TargetViewports);

	// Return unique ICVFX name
	FString GetNameICVFX(const FString& InViewportId, const FString& InResourceId) const
		{ return FString::Printf(TEXT("%s_%s_%s"), DisplayClusterViewportStrings::icvfx::prefix, *InViewportId, *InResourceId); }

private:
	FDisplayClusterViewportManager& ViewportManager;
	ADisplayClusterRootActor& RootActor;

public:
	const UDisplayClusterConfigurationData& ConfigurationData;
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings;
	const FDisplayClusterConfigurationICVFX_LightcardSettings& LightcardSettings;
};

