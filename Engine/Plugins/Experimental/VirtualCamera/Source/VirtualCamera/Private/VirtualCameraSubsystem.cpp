// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraSubsystem.h"

void UVirtualCameraSubsystem::SetVirtualCameraController(TScriptInterface<IVirtualCameraController> VirtualCamera)
{
	ActiveCameraController = VirtualCamera;
	//todo deactive the last current, initialize the new active, call back
}