// Copyright Epic Games, Inc. All Rights Reserved.
#include "UI/BridgeUIManager.h"
#include "UI/FBridgeMessageHandler.h"
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
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "Bridge"
#define LEVELEDITOR_MODULE_NAME TEXT("LevelEditor")
#define CONTENTBROWSER_MODULE_NAME TEXT("ContentBrowser")

TSharedPtr<FBridgeUIManagerImpl> FBridgeUIManager::Instance;
UBrowserBinding* FBridgeUIManager::BrowserBinding;

const FName BridgeTabName = "BridgeTab";

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
	// IWebBrowserSingleton* WebBrowserSingleton = IWebBrowserModule::Get().GetSingleton();
	// if (WebBrowserSingleton)
	// {
	// 	TSharedPtr<IWebBrowserCookieManager> CookieManager = WebBrowserSingleton->GetCookieManager();
	// 	if (CookieManager.IsValid())
	// 	{
	// 		CookieManager->DeleteCookies();
	// 	}
	// }

	// Adding Bridge entry to Quick Content menu.
	UToolMenu* ContentMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.ContentQuickMenu");
	FToolMenuSection& Section = ContentMenu->FindOrAddSection("GetContent");
	Section.AddMenuEntry("OpenBridgeTab",
		LOCTEXT("OpenBridgeTab_Label", "Quixel Bridge"),
		LOCTEXT("OpenBridgeTab_Desc", "Opens the Quixel Bridge."),
		FSlateIcon(FBridgeStyle::GetStyleSetName(), "Bridge.Logo"),
		FUIAction(FExecuteAction::CreateRaw(this, &FBridgeUIManagerImpl::CreateWIndow), FCanExecuteAction())
	);
	Section.AddSeparator(NAME_None);

	//Adding Bridge entry to Content Browser context and New menu.
	UToolMenu* ContextMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu");
	//FToolMenuSection& ContextMenuSection = ContextMenu->AddSection("ContentBrowserMegascans", LOCTEXT("GetContentMenuHeading", "Quixel Content"));
	FToolMenuSection& ContextMenuSection = ContextMenu->FindOrAddSection("ContentBrowserGetContent");
	
	ContextMenuSection.AddMenuEntry(
		"GetMegascans",
		LOCTEXT("OpenBridgeTabText", "Add Quixel Content"),
		LOCTEXT("GetBridgeTooltip", "Add Megascans and DHI assets to project."),
		FSlateIcon(FBridgeStyle::GetStyleSetName(), "Bridge.Logo"),
		FUIAction(FExecuteAction::CreateRaw(this, &FBridgeUIManagerImpl::CreateWIndow), FCanExecuteAction())
	);

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
	MenuBuilder.BeginSection("CustomMenu", TAttribute<FText>(FText::FromString("Quixel")));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenWindow", "Quixel Bridge"),
		LOCTEXT("ToolTip", "Open Quixel Bridge"),
		FSlateIcon(FBridgeStyle::GetStyleSetName(), "Bridge.Logo"),
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
	// browserInitSettings.ProductVersion = TEXT("EpicGamesLauncher/255.255.255-7654321+++Debug+Launcher UnrealEngine/4.23.0-0+UE4 Chrome/59.0.3071.15");
	browserInitSettings.ProductVersion = TEXT("EpicGamesLauncher/12.0.5-15338009+++Portal+Release-Live UnrealEngine/4.23.0-0+UE4 Chrome/84.0.4147.38");
	IWebBrowserModule::Get().CustomInitialize(browserInitSettings);

	FString PluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Bridge"));
	FString IndexUrl = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("ThirdParty"), TEXT("megascans"), TEXT("index.html")));
	
	// Start node process
	FNodeProcessManager::Get()->StartNodeProcess();

	TSharedPtr<SDockTab> LocalBrowserDock;
    {
		SAssignNew(LocalBrowserDock, SDockTab)
		.OnTabClosed_Lambda([](TSharedRef<class SDockTab> InParentTab)
		{
			FBridgeUIManager::Instance->WebBrowserWidget.Reset();
			FBridgeUIManager::BrowserBinding->OnExitDelegate.Execute(TEXT("test"));
		})
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SBorder)
			.Padding(2)
			[
				SAssignNew(WebBrowserWidget, SWebBrowser)
				.InitialURL(FPaths::Combine(TEXT("file:///"), IndexUrl))
				//.InitialURL(TEXT("http://localhost:3000/megascans/home"))
				//.InitialURL(TEXT("chrome://version/"))
				//.InitialURL(TEXT("https://www.whatismybrowser.com/detect/?utm_source=whatismybrowsercom&utm_medium=internal&utm_campaign=breadcrumbs"))
				.ShowControls(false)
			]
		];
	}
	UNodePort* NodePortInfo = NewObject<UNodePort>();
	FBridgeUIManager::BrowserBinding = NewObject<UBrowserBinding>();
	WebBrowserWidget->BindUObject(TEXT("NodePortInfo"), NodePortInfo, true);
	WebBrowserWidget->BindUObject(TEXT("BrowserBinding"), FBridgeUIManager::BrowserBinding, true);

	return LocalBrowserDock.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE
