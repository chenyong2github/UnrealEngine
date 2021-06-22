// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ShaderParameters/DisplayClusterShaderParameters_ICVFX.h"
#include "IDisplayClusterProjection.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

class FDisplayClusterViewport;
class FDisplayClusterViewportManager;
class ADisplayClusterRootActor;
class UDisplayClusterICVFXCameraComponent;

struct FDisplayClusterConfigurationICVFX_ChromakeySettings;
struct FDisplayClusterConfigurationICVFX_ChromakeyMarkers;
struct FDisplayClusterConfigurationICVFX_LightcardSettings;
struct FOpenColorIODisplayConfiguration;

struct FCameraContext_ICVFX
{
	//@todo: add stereo context support
	FRotator ViewRotation;
	FVector  ViewLocation;
	FMatrix  PrjMatrix;
};

class FDisplayClusterViewportConfigurationHelpers_ICVFX
{
public:
	static FDisplayClusterViewportManager* GetViewportManager(ADisplayClusterRootActor& RootActor);

	static FDisplayClusterViewport* FindCameraViewport(ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent);

	static FDisplayClusterViewport* GetOrCreateCameraViewport(ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent);
	static FDisplayClusterViewport* GetOrCreateChromakeyViewport(ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent);
	static FDisplayClusterViewport* GetOrCreateLightcardViewport(FDisplayClusterViewport& BaseViewport, ADisplayClusterRootActor& RootActor, bool bIsOpenColorIOViewportExist);

	static void UpdateCameraViewportSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent);
	static void UpdateChromakeyViewportSettings(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& InCameraViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent);
	static void UpdateLightcardViewportSetting(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& BaseViewport, ADisplayClusterRootActor& RootActor, bool bIsOpenColorIOViewportExist);

	static bool IsCameraUsed(UDisplayClusterICVFXCameraComponent& InCameraComponent);
	static bool GetCameraContext(UDisplayClusterICVFXCameraComponent& InCameraComponent, FCameraContext_ICVFX& OutCameraContext);

	static FDisplayClusterShaderParameters_ICVFX::FCameraSettings GetShaderParametersCameraSettings(const FDisplayClusterViewport& InCameraViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent);
	
	static void UpdateCameraSettings_Chromakey(FDisplayClusterShaderParameters_ICVFX::FCameraSettings& InOutCameraSettings, const FDisplayClusterConfigurationICVFX_ChromakeySettings& InChromakeySettings, FDisplayClusterViewport* InChromakeyViewport);
	static void UpdateCameraSettings_ChromakeyMarkers(FDisplayClusterShaderParameters_ICVFX::FCameraSettings& InOutCameraSettings, const FDisplayClusterConfigurationICVFX_ChromakeyMarkers& InChromakeyMarkers);

	static bool IsShouldUseLightcard(const FDisplayClusterConfigurationICVFX_LightcardSettings& InLightcardSettings);

private:
	static FDisplayClusterViewport* ImplCreateViewport(ADisplayClusterRootActor& RootActor, const FString& InViewportId, const FString& InResourceId, TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy);
};
