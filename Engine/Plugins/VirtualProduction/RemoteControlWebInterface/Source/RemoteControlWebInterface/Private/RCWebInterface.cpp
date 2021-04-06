// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCWebInterface.h"

#include "RCWebInterfacePrivate.h"
#include "RCWebInterfaceProcess.h"
#include "RCWebInterfaceSettings.h"
#include "IWebRemoteControlModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_EDITOR
#include "ISettingsContainer.h"
#include "ISettingsCategory.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "RCWebInterfaceCustomizations.h"
#endif

#define LOCTEXT_NAMESPACE "FRemoteControlWebInterfaceModule"

void FRemoteControlWebInterfaceModule::StartupModule()
{
	bRCWebInterfaceDisable = FParse::Param(FCommandLine::Get(), TEXT("RCWebInterfaceDisable")) || IsRunningCommandlet();

	WebApp = MakeShared<FRemoteControlWebInterfaceProcess>();
	
	if (!bRCWebInterfaceDisable)
	{
		WebApp->Start();
	}

	if (IWebRemoteControlModule* WebRemoteControlModule = FModuleManager::GetModulePtr<IWebRemoteControlModule>("WebRemoteControl"))
	{
		WebSocketServerStartedDelegate = WebRemoteControlModule->OnWebSocketServerStarted().AddLambda([this](uint32) { OnSettingsModified(); });
	}

#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		TSharedPtr<ISettingsSection> SettingsSection = SettingsModule->RegisterSettings(
			"Project",
			"Plugins",
			"RemoteControlWebInterface",
			LOCTEXT("RemoteControlWebInterfaceSettingsName", "Remote Control Web Interface"),
			LOCTEXT("RemoteControlWebInterfaceSettingsDescription", "Configure the Web Remote Control settings."),
			GetMutableDefault<URemoteControlWebInterfaceSettings>());

		SettingsSection->OnModified().BindRaw(this, &FRemoteControlWebInterfaceModule::OnSettingsModified);
	}
	Customizations = MakePimpl<FRCWebInterfaceCustomizations>(WebApp);
#endif
}

void FRemoteControlWebInterfaceModule::ShutdownModule()
{
	if (!bRCWebInterfaceDisable)
	{
		WebApp->Shutdown();
	}

#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "RemoteControlWebInterface");
	}
#endif
	
	if (IWebRemoteControlModule* WebRemoteControlModule = FModuleManager::GetModulePtr<IWebRemoteControlModule>("WebRemoteControl"))
	{
		WebRemoteControlModule->OnWebSocketServerStarted().Remove(WebSocketServerStartedDelegate);
	}
}

bool FRemoteControlWebInterfaceModule::OnSettingsModified()
{
	if (bRCWebInterfaceDisable)
	{
		return true;
	}

	WebApp->Shutdown();
	WebApp->Start();

	return true;
}

#undef LOCTEXT_NAMESPACE

DEFINE_LOG_CATEGORY(LogRemoteControlWebInterface);

IMPLEMENT_MODULE(FRemoteControlWebInterfaceModule, RemoteControlWebInterface)