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
	FToolMenuSection& Section = ContentMenu->FindOrAddSection("ExternalContent");
	Section.AddMenuEntry("OpenBridgeTab",
		LOCTEXT("OpenBridgeTab_Label", "Quixel Bridge"),
		LOCTEXT("OpenBridgeTab_Desc", "Opens the Quixel Bridge."),
		FSlateIcon(FBridgeStyle::GetStyleSetName(), "Bridge.Logo"),
		FUIAction(FExecuteAction::CreateRaw(this, &FBridgeUIManagerImpl::CreateWIndow), FCanExecuteAction())
	);
	//Section.AddSeparator(NAME_None);

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

	/*TSharedPtr<FExtender> NewMenuExtender = MakeShareable(new FExtender);
	NewMenuExtender->AddMenuExtension("LevelEditor",
		EExtensionHook::After,
		NULL,
		FMenuExtensionDelegate::CreateRaw(this, &FBridgeUIManagerImpl::AddPluginMenu));
	LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(NewMenuExtender);*/
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

void FBridgeUIManagerImpl::CreateWIndow()
{
	if (BridgeWindow != NULL)
	{
		BridgeWindow->BringToFront();
		return;
	}

	FString PluginPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Bridge"));
	FString IndexUrl = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("ThirdParty"), TEXT("megascans"), TEXT("index.html")));
	
	// Start node process
	FNodeProcessManager::Get()->StartNodeProcess();

#if PLATFORM_WINDOWS
	// On Windows, direct rendering is enabled.
	// We also create a transparent overlay window to enable drag and drop in direct rendering mode.

	FCreateBrowserWindowSettings WindowSettings;
	WindowSettings.InitialURL = FPaths::Combine(TEXT("file:///"), IndexUrl);

	BridgeWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Bridge")))
		.ClientSize(FVector2D(800, 800));

	FSlateApplication::Get().AddWindow(BridgeWindow.ToSharedRef());

	WindowSettings.OSWindowHandle = BridgeWindow.Get()->GetNativeWindow().Get()->GetOSWindowHandle();

	TSharedPtr<IWebBrowserWindow> Browser = IWebBrowserModule::Get().GetSingleton()->CreateBrowserWindow(WindowSettings);

	BridgeWindow.Get()->SetContent(
		SAssignNew(WebBrowserWidget, SWebBrowser, Browser)
		.ShowControls(false)
	);

	// Create and add a transparent overlay window to enable drag n drop on top of CEF
	FBridgeUIManager::Instance->OverlayWindow = SNew(SWindow)
		.InitialOpacity(0.01f)
		.CreateTitleBar(false)
		.FocusWhenFirstShown(false)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.ClientSize(FVector2D(1, 1))
		.SupportsTransparency(EWindowTransparency::PerWindow)
		.FocusWhenFirstShown(false);

	FSlateApplication::Get().AddWindow(FBridgeUIManager::Instance->OverlayWindow.ToSharedRef());
	FBridgeUIManager::Instance->OverlayWindow->HideWindow();

#elif PLATFORM_MAC || PLATFORM_LINUX
	// On Mac & Linux we get offscreen rendering.
	// A simple browser window is required.

	BridgeWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Bridge")))
		.ClientSize(FVector2D(800, 800))
		[
			SAssignNew(WebBrowserWidget, SWebBrowser)
			.InitialURL(FPaths::Combine(TEXT("file:///"), IndexUrl))
			.ShowControls(false)
		];

	FSlateApplication::Get().AddWindow(BridgeWindow.ToSharedRef());

#endif

	BridgeWindow.Get()->SetOnWindowClosed(
		FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>& Window)
		{
			FBridgeUIManager::Instance->WebBrowserWidget.Reset();
			FBridgeUIManager::BrowserBinding->OnExitDelegate.ExecuteIfBound(TEXT("test"));
			FBridgeUIManager::Instance->BridgeWindow = NULL;
		})
	);

	UNodePort* NodePortInfo = NewObject<UNodePort>();
	FBridgeUIManager::BrowserBinding = NewObject<UBrowserBinding>();
	WebBrowserWidget->BindUObject(TEXT("NodePortInfo"), NodePortInfo, true);
	WebBrowserWidget->BindUObject(TEXT("BrowserBinding"), FBridgeUIManager::BrowserBinding, true);
}

void FBridgeUIManager::Shutdown()
{
	FBridgeStyle::Shutdown();
}

#undef LOCTEXT_NAMESPACE
