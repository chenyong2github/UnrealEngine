// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCWebInterface.h"
#include "RCWebInterfacePrivate.h"
#include "RCWebInterfaceSettings.h"
#include "IWebRemoteControlModule.h"
#if WITH_EDITOR
#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "HAL/PlatformProcess.h"
#include "Input/Reply.h"
#include "IRemoteControlUIModule.h"
#include "ISettingsContainer.h"
#include "ISettingsCategory.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Images/SThrobber.h"
#endif


#define LOCTEXT_NAMESPACE "FRemoteControlWebInterfaceModule"

void FRemoteControlWebInterfaceModule::StartupModule()
{
	if (!IsRunningCommandlet())
	{
		WebApp.Start();
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
	RegisterPanelExtension();
#endif
}

void FRemoteControlWebInterfaceModule::ShutdownModule()
{
	if (!IsRunningCommandlet())
	{
		WebApp.Shutdown();
	}

#if WITH_EDITOR
	UnregisterPanelExtension();
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
	WebApp.Shutdown();
	WebApp.Start();
	return true;
}

#if WITH_EDITOR
void FRemoteControlWebInterfaceModule::RegisterPanelExtension()
{
	if (IRemoteControlUIModule* RemoteControlUI = FModuleManager::GetModulePtr<IRemoteControlUIModule>("RemoteControlUI"))
	{
		RemoteControlUI->GetExtensionGenerators().AddRaw(this, &FRemoteControlWebInterfaceModule::GeneratePanelExtensions);
	}
}

void FRemoteControlWebInterfaceModule::UnregisterPanelExtension()
{
	if (IRemoteControlUIModule* RemoteControlUI = FModuleManager::GetModulePtr<IRemoteControlUIModule>("RemoteControlUI"))
	{
		RemoteControlUI->GetExtensionGenerators().RemoveAll(this);
	}
}

void FRemoteControlWebInterfaceModule::GeneratePanelExtensions(TArray<TSharedRef<SWidget>>& OutExtensions)
{
	auto GetDetailWidgetIndex = [this]()
	{
		if (WebApp.GetStatus() == FRemoteControlWebInterfaceProcess::EStatus::Launching)
		{
			return 0;	
		}
		else
		{
			return 1;
		}
	};

	TSharedPtr<SWidget> Throbber;

	OutExtensions.Add(
		SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda(GetDetailWidgetIndex)
		+ SWidgetSwitcher::Slot()
		[
			SAssignNew(Throbber, SCircularThrobber)
			.Radius(10.f)
		]
		+ SWidgetSwitcher::Slot()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "FlatButton")
			.OnClicked_Raw(this, &FRemoteControlWebInterfaceModule::OpenWebApp)
			[
				SNew(STextBlock)
				.ToolTipText_Lambda([this]()
					{
						switch (WebApp.GetStatus())
						{
						case FRemoteControlWebInterfaceProcess::EStatus::Error: return LOCTEXT("AppErrorTooltip", "An error occurred when launching the web app.\r\nClick to change the Web App's settings.");
						case FRemoteControlWebInterfaceProcess::EStatus::Running: return LOCTEXT("AppRunningTooltip", "Open the web app in a web browser.");
						case FRemoteControlWebInterfaceProcess::EStatus::Stopped: return LOCTEXT("AppStoppedTooltip", "The web app is not currently running.\r\nClick to change the Web App's settings.");
						default: return FText();
						}
					})
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text_Lambda([this]()
					{
						switch (WebApp.GetStatus())
						{
						case FRemoteControlWebInterfaceProcess::EStatus::Error: return FEditorFontGlyphs::Exclamation_Triangle;
						case FRemoteControlWebInterfaceProcess::EStatus::Running: return FEditorFontGlyphs::External_Link;
						case FRemoteControlWebInterfaceProcess::EStatus::Stopped: return FEditorFontGlyphs::Exclamation_Triangle;
						default: return FEditorFontGlyphs::Exclamation_Triangle;
						}
					})
			]
		]
	);

	Throbber->SetToolTipText(LOCTEXT("AppLaunchingTooltip", "The web app is in the process of launching."));
}

FReply FRemoteControlWebInterfaceModule::OpenWebApp() const
{
	if (WebApp.GetStatus() == FRemoteControlWebInterfaceProcess::EStatus::Running)
	{
		const uint32 Port = GetDefault<URemoteControlWebInterfaceSettings>()->RemoteControlWebInterfacePort;
		const FString Address = FString::Printf(TEXT("http://127.0.0.1:%d"), Port);

		FPlatformProcess::LaunchURL(*Address, nullptr, nullptr);
	}
	else
	{
		FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "RemoteControlWebInterface");
	}

	return FReply::Handled();
}
#endif

#undef LOCTEXT_NAMESPACE
	
DEFINE_LOG_CATEGORY(LogRemoteControlWebInterface);

IMPLEMENT_MODULE(FRemoteControlWebInterfaceModule, RemoteControlWebInterface)