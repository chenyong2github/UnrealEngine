// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

class SRewindDebugger;

struct FDebugObjectInfo
{
	FDebugObjectInfo(uint64 Id, const FString& Name): ObjectId(Id), ObjectName(Name)
	{
	}

	uint64 ObjectId;
	FString ObjectName;
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

	FDelegateHandle TickerHandle;
};
