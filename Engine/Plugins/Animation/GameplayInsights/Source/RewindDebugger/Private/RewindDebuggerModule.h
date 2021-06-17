// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"
#include "RewindDebuggerCamera.h"

class SRewindDebugger;

struct FDebugObjectInfo
{
	FDebugObjectInfo(uint64 Id, const FString& Name): ObjectId(Id), ObjectName(Name), bExpanded(true)
	{
	}

	uint64 ObjectId;
	FString ObjectName;
	bool bExpanded;

	TArray<TSharedPtr<FDebugObjectInfo>> Children;
};

class FRewindDebuggerModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedRef<SDockTab> SpawnRewindDebuggerTab(const FSpawnTabArgs& SpawnTabArgs);

private:
	TSharedPtr<SRewindDebugger> RewindDebuggerWidget;

	FRewindDebuggerCamera RewindDebuggerCameraExtension;
};
