// Copyright Epic Games, Inc. All Rights Reserved.
#include "NodePort.h"
#include "UI/BridgeUIManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

UNodePort::UNodePort(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FString UNodePort::GetNodePort()
{
	FString NodePort;
	if (FFileHelper::LoadFileToString(NodePort, *PortFilePath))
	{
		return NodePort;
	}

	return TEXT("0");
}

bool UNodePort::IsNodeRunning()
{
	return true;
}
