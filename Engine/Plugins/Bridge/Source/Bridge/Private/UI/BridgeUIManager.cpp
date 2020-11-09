// Copyright Epic Games, Inc. All Rights Reserved.
#include "UI/BridgeUIManager.h"
#include "UI/BridgeStyle.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "NodePort.h"
#include "NodeProcess.h"

// WebBrowser
#include "SWebBrowser.h"
#include "WebBrowserModule.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserCookieManager.h"
// Widgets
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "Bridge"
#define LEVELEDITOR_MODULE_NAME TEXT("LevelEditor")
#define CONTENTBROWSER_MODULE_NAME TEXT("ContentBrowser")

TSharedPtr<FBridgeUIManagerImpl> FBridgeUIManager::Instance;
UMegascansAuthentication* FBridgeUIManager::MegascansAuthentication;

const FName BridgeTabName = "BridgeTab";

class FBridgeUIManagerImpl
{
public:
	void Initialize();
	void Shutdown();
	void HandleBrowserUrlChanged(const FText& Url);

private:
	void SetupMenuItem();
	void CreateWIndow();
	TSharedRef<SDockTab> CreateBridgeTab(const FSpawnTabArgs& Args);
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	TSharedPtr<SWebBrowser> WebBrowserWidget;

	void AddPluginMenu(FMenuBuilder& MenuBuilder);

protected:
	const FText BridgeTabDisplay = FText::FromString("Bridge");
	const FText BridgeToolTip = FText::FromString("Launch Megascans Bridge");
};

void FBridgeUIManager::Initialize()
{
	if (!Instance.IsValid())
	{
		//Instance = MakeUnique<FBridgeUIManagerImpl>();
		Instance = MakeShareable(new FBridgeUIManagerImpl);
		Instance->Initialize();
	}
}

void FBridgeUIManagerImpl::Initialize()
{
	FBridgeStyle::Initialize();
	SetupMenuItem();
}

void FBridgeUIManagerImpl::SetupMenuItem()
{
	FBridgeStyle::SetIcon("Logo", "Logo80x80");
	FBridgeStyle::SetIcon("ContextLogo", "Logo32x32");
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LEVELEDITOR_MODULE_NAME);
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateRaw(this, &FBridgeUIManagerImpl::FillToolbar));
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);

	// For Deleting Cookies
	//IWebBrowserSingleton* WebBrowserSingleton = IWebBrowserModule::Get().GetSingleton();
	//if (WebBrowserSingleton)
	//{
	//	TSharedPtr<IWebBrowserCookieManager> CookieManager = WebBrowserSingleton->GetCookieManager();
	//	if (CookieManager.IsValid())
	//	{
	//		CookieManager->DeleteCookies();
	//	}
	//}

	TSharedPtr<FExtender> NewMenuExtender = MakeShareable(new FExtender);
	NewMenuExtender->AddMenuExtension("LevelEditor",
		EExtensionHook::After,
		NULL,
		FMenuExtensionDelegate::CreateRaw(this, &FBridgeUIManagerImpl::AddPluginMenu));
	LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(NewMenuExtender);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BridgeTabName,
	FOnSpawnTab::CreateRaw(this, &FBridgeUIManagerImpl::CreateBridgeTab))
		.SetDisplayName(BridgeTabDisplay)
		.SetAutoGenerateMenuEntry(false)
		.SetTooltipText(BridgeToolTip);
}

void FBridgeUIManagerImpl::AddPluginMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("CustomMenu", TAttribute<FText>(FText::FromString("Bifrost")));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenWindow", "Bifrost"),
		LOCTEXT("ToolTip", "Open Quixel Bifrost"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FBridgeUIManagerImpl::CreateWIndow))
	);

	MenuBuilder.EndSection();
}

void FBridgeUIManagerImpl::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection(TEXT("QuixelBridge"));
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateRaw(this, &FBridgeUIManagerImpl::CreateWIndow)),
			FName(TEXT("Quixel Bridge")),
			LOCTEXT("QMSLiveLink_label", "Bridge"),
			LOCTEXT("WorldProperties_ToolTipOverride", "Megascans Link with Bridge"),
			FSlateIcon(FBridgeStyle::GetStyleSetName(), "Bridge.Logo"),
			EUserInterfaceActionType::Button,
			FName(TEXT("QuixelBridge"))
		);
	}
	ToolbarBuilder.EndSection();
}

void FBridgeUIManagerImpl::HandleBrowserUrlChanged(const FText& Url)
{
	UE_LOG(LogTemp, Error, TEXT("URL changed"));
}

void FBridgeUIManagerImpl::CreateWIndow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(BridgeTabName);
}

void FBridgeUIManager::Shutdown()
{
	FBridgeStyle::Shutdown();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BridgeTabName);
}

TSharedRef<SDockTab> FBridgeUIManagerImpl::CreateBridgeTab(const FSpawnTabArgs& Args)
{
	// Temp workaround which enables authentication (by impersonating Launcher's user agent)
	FWebBrowserInitSettings browserInitSettings = FWebBrowserInitSettings();
	browserInitSettings.ProductVersion = TEXT("EpicGamesLauncher/255.255.255-7654321+++Debug+Launcher UnrealEngine/4.23.0-0+UE4 Chrome/59.0.3071.15");
	IWebBrowserModule::Get().CustomInitialize(browserInitSettings);

	FString PluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Bridge"));
	FString IndexUrl = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("Content"), TEXT("out"), TEXT("index.html")));

	TSharedPtr<SDockTab> BrowserDock;
	{
		SAssignNew(BrowserDock, SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SBorder)
			.Padding(2)
			[
				SAssignNew(WebBrowserWidget, SWebBrowser)
				.InitialURL(FPaths::Combine(TEXT("file:///"), IndexUrl))
				.ShowControls(false)
			]
		];
	}

	UNodePort* NodePortInfo = NewObject<UNodePort>();
	FBridgeUIManager::MegascansAuthentication = NewObject<UMegascansAuthentication>();
	WebBrowserWidget->BindUObject(TEXT("NodePortInfo"), NodePortInfo, true);
	WebBrowserWidget->BindUObject(TEXT("MegascansAuthentication"), FBridgeUIManager::MegascansAuthentication, true);
	return BrowserDock.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE
