// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerWindowController.h"

#include "ConcertServerTabs.h"
#include "Browser/ConcertServerSessionBrowserController.h"
#include "OutputLog/OutputLogController.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "OutputLog/Public/OutputLogModule.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

FConcertServerWindowController::FConcertServerWindowController(const FConcertServerWindowInitParams& Params)
	: MultiUserServerLayoutIni(Params.MultiUserServerLayoutIni)
{
	SessionBrowserController = MakeShared<FConcertServerSessionBrowserController>();
	OutputLogController = MakeShared<FOutputLogController>();
	InitComponents(Params);

	FDisplayMetrics DisplayMetrics;
	FSlateApplication::Get().GetDisplayMetrics(DisplayMetrics);
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(DisplayMetrics.PrimaryDisplayWorkAreaRect.Left, DisplayMetrics.PrimaryDisplayWorkAreaRect.Top);

	const bool bEmbedTitleAreaContent = false;
	const FVector2D ClientSize(960.0f * DPIScaleFactor, 640.0f * DPIScaleFactor);
	TSharedRef<SWindow> RootWindow = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Unreal Multi User Server"))
		.CreateTitleBar(!bEmbedTitleAreaContent)
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.IsInitiallyMaximized(false)
		.IsInitiallyMinimized(false)
		.SizingRule(ESizingRule::UserSized)
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.ClientSize(ClientSize)
		.AdjustInitialSizeAndPositionForDPIScale(false);
		
	const bool bShowRootWindowImmediately = false;
	FSlateApplication::Get().AddWindow(RootWindow, bShowRootWindowImmediately);
	FGlobalTabmanager::Get()->SetRootWindow(RootWindow);
	FGlobalTabmanager::Get()->SetAllowWindowMenuBar(true);

	FSlateNotificationManager::Get().SetRootWindow(RootWindow);
	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("UnrealMultiUserServerLayout_v1.0");
	DefaultLayout->AddArea
	(
		FTabManager::NewPrimaryArea()
		->Split
		(
			FTabManager::NewStack()
				->AddTab(ConcertServerTabs::GetSessionBrowserTabId(), ETabState::OpenedTab)
				->AddTab(ConcertServerTabs::GetOutputLogTabId(), ETabState::OpenedTab)
				->SetForegroundTab(ConcertServerTabs::GetSessionBrowserTabId())
		)
	);
	
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(Params.MultiUserServerLayoutIni, DefaultLayout);
	TSharedPtr<SWidget> Content = FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), RootWindow, bEmbedTitleAreaContent, EOutputCanBeNullptr::Never);
	RootWindow->SetContent(Content.ToSharedRef());

	RootWindow->SetOnWindowClosed(FOnWindowClosed::CreateRaw(this, &FConcertServerWindowController::OnWindowClosed));
	RootWindow->ShowWindow();
	const bool bForceWindowToFront = true;
	RootWindow->BringToFront(bForceWindowToFront);
}

void FConcertServerWindowController::InitComponents(const FConcertServerWindowInitParams& WindowInitParams) const
{
	const FConcertComponentInitParams Params { WindowInitParams.Server };
	SessionBrowserController->Init(Params);
	OutputLogController->Init(Params);
}

void FConcertServerWindowController::OnWindowClosed(const TSharedRef<SWindow>& Window)
{
	SaveLayout();
}

void FConcertServerWindowController::SaveLayout() const
{
	FLayoutSaveRestore::SaveToConfig(MultiUserServerLayoutIni, PersistentLayout.ToSharedRef());
    GConfig->Flush(false, MultiUserServerLayoutIni);
}

#undef LOCTEXT_NAMESPACE 