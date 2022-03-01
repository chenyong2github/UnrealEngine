// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerWindowController.h"

#include "ConcertServerTabs.h"
#include "IConcertServer.h"
#include "IConcertSyncServer.h"
#include "Browser/ConcertServerSessionBrowserController.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "OutputLog/Public/OutputLogModule.h"
#include "Session/ConcertServerSessionTab.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

FConcertServerWindowController::FConcertServerWindowController(const FConcertServerWindowInitParams& Params)
	: MultiUserServerLayoutIni(Params.MultiUserServerLayoutIni)
{
	ServerInstance = Params.Server;
	SessionBrowserController = MakeShared<FConcertServerSessionBrowserController>();
}

void FConcertServerWindowController::CreateWindow()
{
	InitComponents();

	FDisplayMetrics DisplayMetrics;
	FSlateApplication::Get().GetDisplayMetrics(DisplayMetrics);
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(DisplayMetrics.PrimaryDisplayWorkAreaRect.Left, DisplayMetrics.PrimaryDisplayWorkAreaRect.Top);

	const bool bEmbedTitleAreaContent = false;
	const FVector2D ClientSize(960.0f * DPIScaleFactor, 640.0f * DPIScaleFactor);
	TSharedRef<SWindow> RootWindowRef = SNew(SWindow)
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
	RootWindow = RootWindowRef;
		
	const bool bShowRootWindowImmediately = false;
	FSlateApplication::Get().AddWindow(RootWindowRef, bShowRootWindowImmediately);
	FGlobalTabmanager::Get()->SetRootWindow(RootWindowRef);
	FGlobalTabmanager::Get()->SetAllowWindowMenuBar(true);

	FSlateNotificationManager::Get().SetRootWindow(RootWindowRef);
	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("UnrealMultiUserServerLayout_v1.0");
	DefaultLayout->AddArea
	(
		FTabManager::NewPrimaryArea()
		->Split
		(
			FTabManager::NewStack()
				->AddTab(ConcertServerTabs::GetSessionBrowserTabId(), ETabState::OpenedTab)
				->SetForegroundTab(ConcertServerTabs::GetSessionBrowserTabId())
		)
	);
	
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(MultiUserServerLayoutIni, DefaultLayout);
	TSharedPtr<SWidget> Content = FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), RootWindow, bEmbedTitleAreaContent, EOutputCanBeNullptr::Never);
	RootWindow->SetContent(Content.ToSharedRef());

	RootWindow->SetOnWindowClosed(FOnWindowClosed::CreateRaw(this, &FConcertServerWindowController::OnWindowClosed));
	RootWindow->ShowWindow();
	const bool bForceWindowToFront = true;
	RootWindow->BringToFront(bForceWindowToFront);
}

void FConcertServerWindowController::OpenSessionTab(const FGuid& SessionId)
{
	if (TSharedPtr<FConcertServerSessionTab> SessionTab = GetOrRegisterSessionTab(SessionId))
	{
		SessionTab->OpenSessionTab();
	}
}

TSharedPtr<FConcertServerSessionTab> FConcertServerWindowController::GetOrRegisterSessionTab(const FGuid& SessionId)
{
	if (TSharedPtr<IConcertServerSession> Session = ServerInstance->GetConcertServer()->GetLiveSession(SessionId))
	{
		const FConcertSessionInfo& SessionInfo = Session->GetSessionInfo();
		if (TSharedRef<FConcertServerSessionTab>* FoundId = RegisteredSessions.Find(SessionInfo.SessionId))
		{
			return *FoundId;
		}
	
		const TSharedRef<FConcertServerSessionTab> SessionTab = MakeShared<FConcertServerSessionTab>(Session.ToSharedRef(), RootWindow.ToSharedRef());
		RegisteredSessions.Add(SessionInfo.SessionId, SessionTab);
		return SessionTab;
	}
	
	return nullptr;
}

void FConcertServerWindowController::InitComponents()
{
	const FConcertComponentInitParams Params { ServerInstance.ToSharedRef(), SharedThis(this) };
	SessionBrowserController->Init(Params);
}

void FConcertServerWindowController::OnWindowClosed(const TSharedRef<SWindow>& Window)
{
	SaveLayout();
	RootWindow.Reset();
}

void FConcertServerWindowController::SaveLayout() const
{
	FLayoutSaveRestore::SaveToConfig(MultiUserServerLayoutIni, PersistentLayout.ToSharedRef());
    GConfig->Flush(false, MultiUserServerLayoutIni);
}

#undef LOCTEXT_NAMESPACE 