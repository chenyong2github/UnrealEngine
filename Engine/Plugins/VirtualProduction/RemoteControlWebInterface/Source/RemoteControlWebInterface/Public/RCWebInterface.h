// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "RCWebInterfaceProcess.h"

class SWidget;

class FRemoteControlWebInterfaceModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Handle Web Interface settings modifications. */
	bool OnSettingsModified();

#if WITH_EDITOR
	//~ Handle registering a widget extension with the remote control panel to show the web app's status.
	void RegisterPanelExtension();
	void UnregisterPanelExtension();

	/** Handles adding the widget extension to the remote control panel. */
	void GeneratePanelExtensions(TArray<TSharedRef<SWidget>>& OutExtensions);

	/** Handles launching the web app through a web browser. */
	class FReply OpenWebApp() const;
#endif

	/** The actual process that runs the middleman server. */
	FRemoteControlWebInterfaceProcess WebApp;

	/** WebSocketServer Start Delegate */
	FDelegateHandle WebSocketServerStartedDelegate;
};
