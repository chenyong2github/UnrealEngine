// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Containers/DisplayClusterProjectionCameraPolicySettings.h"

class UCameraComponent;
class UWorld;

/**
 * Implements math behind the native camera projection policy (use symmetric frustum of a camera)
 */
class FDisplayClusterProjectionCameraPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionCameraPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionCameraPolicy();

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::Camera; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
	virtual void HandleEndScene(class IDisplayClusterViewport* InViewport) override;

	virtual bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{ return false; }

public:
	void SetCamera(UCameraComponent* const NewCamera, const FDisplayClusterProjectionCameraPolicySettings& InCameraSettings);

protected:
	bool GetSettingsFromConfig(class IDisplayClusterViewport* InViewport, UCameraComponent* &OutCamera, FDisplayClusterProjectionCameraPolicySettings& OutCameraSettings);


private:
	// Camera to use for rendering
	UCameraComponent* AssignedCamera = nullptr;

	FDisplayClusterProjectionCameraPolicySettings CameraSettings;
};
