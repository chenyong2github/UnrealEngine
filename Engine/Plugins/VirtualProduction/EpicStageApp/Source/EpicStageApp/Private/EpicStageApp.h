// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "StageAppBeaconReceiver.h"
#include "StageAppRouteHandler.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FEpicStageAppModule : public IModuleInterface
{
	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface
	
	/** Handler for WebSocket routes. */
	FStageAppRouteHandler RouteHandler;

	/** Receiver that responds to beacon messages from the stage app. */
	FStageAppBeaconReceiver StageAppBeaconReceiver;
};

IMPLEMENT_MODULE(FEpicStageAppModule, EpicStageApp);