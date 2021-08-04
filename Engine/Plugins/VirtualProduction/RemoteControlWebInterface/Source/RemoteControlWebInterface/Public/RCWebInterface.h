// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Templates/PimplPtr.h"

class FRemoteControlWebInterfaceProcess;

class FRCWebInterfaceCustomizations;

class FRemoteControlWebInterfaceModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Handle Web Interface settings modifications. */
	void OnSettingsModified(UObject* Settings, struct FPropertyChangedEvent& PropertyChangedEvent);

private:
	/** The actual process that runs the middleman server. */
	TSharedPtr<FRemoteControlWebInterfaceProcess> WebApp;

	/** WebSocketServer Start Delegate */
	FDelegateHandle WebSocketServerStartedDelegate;

	/** Customizations/Additions to add to the RC Panel. */
	TPimplPtr<FRCWebInterfaceCustomizations> Customizations;

	/** Prevents the WebApp from starting. Can be set from command line with -RCWebInterfaceDisable */
	bool bRCWebInterfaceDisable;
};
