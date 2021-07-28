// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCWebInterface.h"

#include "RCWebInterfacePrivate.h"
#include "RCWebInterfaceProcess.h"
#include "RemoteControlSettings.h"
#include "IWebRemoteControlModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_EDITOR
#include "Misc/CoreDelegates.h"
#include "RCWebInterfaceCustomizations.h"
#endif

#define LOCTEXT_NAMESPACE "FRemoteControlWebInterfaceModule"

void FRemoteControlWebInterfaceModule::StartupModule()
{
	bRCWebInterfaceDisable = FParse::Param(FCommandLine::Get(), TEXT("RCWebInterfaceDisable")) || IsRunningCommandlet();

	WebApp = MakeShared<FRemoteControlWebInterfaceProcess>();
	if (!bRCWebInterfaceDisable)
	{
		TSharedPtr<FRemoteControlWebInterfaceProcess> WebAppLocal = WebApp;
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([WebAppLocal]()
		{
			WebAppLocal->Start();
		});
	}

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
	if (!bRCWebInterfaceDisable)
	{
		WebApp->Shutdown();
	}

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
	if (bRCWebInterfaceDisable)
	{
		return;
	}
	
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