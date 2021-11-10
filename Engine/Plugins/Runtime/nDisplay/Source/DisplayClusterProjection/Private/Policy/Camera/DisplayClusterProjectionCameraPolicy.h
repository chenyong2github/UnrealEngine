// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Containers/DisplayClusterProjectionCameraPolicySettings.h"
#include "Misc/DisplayClusterObjectRef.h"

class IDisplayClusterViewport;
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
	virtual bool HandleStartScene(IDisplayClusterViewport* InViewport) override;
	virtual void HandleEndScene(IDisplayClusterViewport* InViewport) override;

	virtual bool CalculateView(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{ return false; }

	virtual bool ShouldUseSourceTextureWithMips() const override
	{ return true; }

public:
	void SetCamera(UCameraComponent* const NewCamera, const FDisplayClusterProjectionCameraPolicySettings& InCameraSettings);

private:
	bool ImplGetProjectionMatrix(const float CameraFOV, const float CameraAspectRatio, IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix);

protected:
	UCameraComponent* GetCameraComponent();

private:
	// Camera to use for rendering
	FDisplayClusterSceneComponentRef CameraRef;
	FDisplayClusterProjectionCameraPolicySettings CameraSettings;
	float ZNear = 1;
	float ZFar = 1;
};
