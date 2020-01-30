// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterProjectionBlueprintAPIImpl.h"

#include "IDisplayClusterProjection.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Policy/Camera/DisplayClusterProjectionCameraPolicy.h"
#include "Policy/Camera/DisplayClusterProjectionCameraPolicyFactory.h"

#include "Camera/CameraComponent.h"



//////////////////////////////////////////////////////////////////////////////////////////////
// Policy: CAMERA
//////////////////////////////////////////////////////////////////////////////////////////////
void UDisplayClusterProjectionBlueprintAPIImpl::CameraPolicySetCamera(const FString& ViewportId, UCameraComponent* NewCamera, float FOVMultiplier)
{
	check(NewCamera);
	check(FOVMultiplier >= 0.1f);

	IDisplayClusterProjection& Module = IDisplayClusterProjection::Get();
	
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = Module.GetProjectionFactory(DisplayClusterStrings::projection::Camera);
	if (Factory.IsValid())
	{
		TSharedPtr<FDisplayClusterProjectionCameraPolicyFactory> CameraFactory = StaticCastSharedPtr<FDisplayClusterProjectionCameraPolicyFactory>(Factory);
		if (CameraFactory.IsValid())
		{
			TSharedPtr<IDisplayClusterProjectionPolicy> PolicyInstance = CameraFactory->GetPolicyInstance(ViewportId);
			if (PolicyInstance.IsValid())
			{
				TSharedPtr<FDisplayClusterProjectionCameraPolicy> CameraPolicyInstance = StaticCastSharedPtr<FDisplayClusterProjectionCameraPolicy>(PolicyInstance);
				if (CameraPolicyInstance)
				{
					CameraPolicyInstance->SetCamera(NewCamera, FOVMultiplier);
				}
			}
		}
	}

}
