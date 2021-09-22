// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCWebInterface.h"

#include "RCWebInterfacePrivate.h"
#include "RCWebInterfaceProcess.h"
#include "RemoteControlSettings.h"
#include "IWebRemoteControlModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "RCWebInterfaceCustomizations.h"
#endif

#define LOCTEXT_NAMESPACE "FRemoteControlWebInterfaceModule"

namespace RCWebInterface
{
	bool IsWebInterfaceEnabled()
	{
		bool bIsEditor = false;

#if WITH_EDITOR
		bIsEditor = GIsEditor;
#endif
		// By default, remote control web interface is disabled in -game and packaged game.
		return (!IsRunningCommandlet() && bIsEditor) || FParse::Param(FCommandLine::Get(), TEXT("RCWebInterfaceEnable"));
	}
}

void FRemoteControlWebInterfaceModule::StartupModule()
{
	if (!RCWebInterface::IsWebInterfaceEnabled())
	{
		UE_LOG(LogRemoteControlWebInterface, Display, TEXT("Remote Control Web Interface is disabled by default when running outside the editor. Use the -RCWebInterfaceEnable flag when launching in order to use it."));
		return;
	}
	
	WebApp = MakeShared<FRemoteControlWebInterfaceProcess>();

	TSharedPtr<FRemoteControlWebInterfaceProcess> WebAppLocal = WebApp;

	FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([WebAppLocal]()
		{
			WebAppLocal->Start();
		});

	if (IWebRemoteControlModule* WebRemoteControlModule = FModuleManager::GetModulePtr<IWebRemoteControlModule>("WebRemoteControl"))
	{
		WebSocketServerStartedDelegate = WebRemoteControlModule->OnWebSocketServerStarted().AddLambda([this](uint32)
			{
				WebApp->Shutdown();
				WebApp->Start();
			});
	}

#if WITH_EDITOR
	GetMutableDefault<URemoteControlSettings>()->OnSettingChanged().AddRaw(this, &FRemoteControlWebInterfaceModule::OnSettingsModified);

	FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
			Customizations = MakePimpl<FRCWebInterfaceCustomizations>(WebApp);
		});
#endif
}

void FRemoteControlWebInterfaceModule::ShutdownModule()
{
	if (!RCWebInterface::IsWebInterfaceEnabled())
	{
		return;
	}

	WebApp->Shutdown();

	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
#endif
	
	if (IWebRemoteControlModule* WebRemoteControlModule = FModuleManager::GetModulePtr<IWebRemoteControlModule>("WebRemoteControl"))
	{
		WebRemoteControlModule->OnWebSocketServerStarted().Remove(WebSocketServerStartedDelegate);
	}
}

void FRemoteControlWebInterfaceModule::OnSettingsModified(UObject* Settings, FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(URemoteControlSettings, RemoteControlWebInterfacePort) || PropertyName == GET_MEMBER_NAME_CHECKED(URemoteControlSettings, bForceWebAppBuildAtStartup))
	{
		WebApp->Shutdown();
		WebApp->Start();
	}
}

#undef LOCTEXT_NAMESPACE

DEFINE_LOG_CATEGORY(LogRemoteControlWebInterface);

IMPLEMENT_MODULE(FRemoteControlWebInterfaceModule, RemoteControlWebInterface)