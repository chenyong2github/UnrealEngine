// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraSubsystem.h"
#include "LevelSequencePlaybackController.h"


UVirtualCameraSubsystem::UVirtualCameraSubsystem()
	: bIsStreaming(false)
{
	SequencePlaybackController = CreateDefaultSubobject<ULevelSequencePlaybackController>("SequencePlaybackController");
}

bool UVirtualCameraSubsystem::StartStreaming()
{
	if (bIsStreaming)
	{
		return false;
	}

	if (ActiveCameraController)
	{
		bIsStreaming = ActiveCameraController->StartStreaming();
	}

	if (bIsStreaming)
	{
		FEditorScriptExecutionGuard ScriptGuard;
		OnStreamStartedDelegate.Broadcast();
	}

	return bIsStreaming;
}

bool UVirtualCameraSubsystem::StopStreaming()
{
	if (!bIsStreaming)
	{
		return false;
	}

	if (ActiveCameraController)
	{
		bIsStreaming = !(ActiveCameraController->StopStreaming());
	}

	if (!bIsStreaming)
	{
		FEditorScriptExecutionGuard ScriptGuard;
		OnStreamStoppedDelegate.Broadcast();
	}

	return bIsStreaming;
}

bool UVirtualCameraSubsystem::IsStreaming() const
{
	return bIsStreaming;
}

TScriptInterface<IVirtualCameraController> UVirtualCameraSubsystem::GetVirtualCameraController() const
{
	return ActiveCameraController;
}

void UVirtualCameraSubsystem::SetVirtualCameraController(TScriptInterface<IVirtualCameraController> VirtualCamera)
{
	ActiveCameraController = VirtualCamera;
	//todo deactive the last current, initialize the new active, call back
}
