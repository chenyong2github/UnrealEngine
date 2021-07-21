// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"
#include "RewindDebuggerCamera.h"

class SRewindDebugger;

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
