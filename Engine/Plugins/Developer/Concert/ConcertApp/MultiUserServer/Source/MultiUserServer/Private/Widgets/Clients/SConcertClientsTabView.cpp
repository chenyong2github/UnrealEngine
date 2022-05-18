// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertClientsTabView.h"

#include "IConcertServer.h"
#include "IConcertSyncServer.h"
#include "Logging/Filter/ConcertLogFilter_FrontendRoot.h"
#include "Logging/Source/GlobalLogSource.h"
#include "Logging/Util/ConcertLogTokenizer.h"
#include "SPromptConcertLoggingEnabled.h"
#include "Widgets/Clients/Logging/SConcertTransportLog.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

const FName SConcertClientsTabView::ClientBrowserTabId("ClientBrowserTabId");
const FName SConcertClientsTabView::GlobalLogTabId("GlobalLogTabId");

SConcertClientsTabView::~SConcertClientsTabView()
{
	ConcertTransportEvents::OnConcertTransportLoggingEnabledChangedEvent().RemoveAll(this);
}

void SConcertClientsTabView::Construct(const FArguments& InArgs, FName InStatusBarID, TSharedRef<IConcertSyncServer> InServer)
{
	LogTokenizer = MakeShared<FConcertLogTokenizer>();
	Server = MoveTemp(InServer);
	
	SConcertTabViewWithManagerBase::Construct(
		SConcertTabViewWithManagerBase::FArguments()
		.ConstructUnderWindow(InArgs._ConstructUnderWindow)
		.ConstructUnderMajorTab(InArgs._ConstructUnderMajorTab)
		.CreateTabs(FCreateTabs::CreateLambda([this, &InArgs](const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout)
		{
			CreateTabs(InTabManager, InLayout, InArgs);
		}))
		.OverlayTabs(this, &SConcertClientsTabView::SetupLoggingPromptOverlay)
		.LayoutName("ConcertClientsTabView_v0.1"),
		InStatusBarID
	);
}

void SConcertClientsTabView::CreateTabs(const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout, const FArguments& InArgs)
{
	InTabManager->RegisterTabSpawner(ClientBrowserTabId, FOnSpawnTab::CreateSP(this, &SConcertClientsTabView::SpawnClientBrowserTab))
		.SetDisplayName(LOCTEXT("ClientBrowserTabLabel", "Clients"));
	InTabManager->RegisterTabSpawner(GlobalLogTabId, FOnSpawnTab::CreateSP(this, &SConcertClientsTabView::SpawnGlobalLogTab))
		.SetDisplayName(LOCTEXT("GlobalLogTabLabel", "Global Log"));
	InLayout->AddArea
		(
			FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(ClientBrowserTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(GlobalLogTabId, ETabState::OpenedTab)
				)
		);
}

TSharedRef<SDockTab> SConcertClientsTabView::SpawnClientBrowserTab(const FSpawnTabArgs& InTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ClientBrowserTabLabel", "Clients"))
		.TabRole(PanelTab)
		[
			SNullWidget::NullWidget
		]; 
}

TSharedRef<SDockTab> SConcertClientsTabView::SpawnGlobalLogTab(const FSpawnTabArgs& InTabArgs)
{
	// TODO: Load from config file
	constexpr size_t LogCapacity = 500000;
	
	return SNew(SDockTab)
		.Label(LOCTEXT("GlobalLogTabLabel", "Global Log"))
		.TabRole(PanelTab)
		[
			SNew(SConcertTransportLog, MakeShared<FGlobalLogSource>(LogCapacity))
			.Filter(UE::MultiUserServer::MakeGlobalLogFilter(LogTokenizer.ToSharedRef()))
		]; 
}

TSharedRef<SWidget> SConcertClientsTabView::SetupLoggingPromptOverlay(const TSharedRef<SWidget>& TabsWidget)
{
	SOverlay::FOverlaySlot* MessageSlot;
	EnableLoggingPromptOverlay =  SNew(SOverlay)
		+SOverlay::Slot()
		[
			TabsWidget
		]
		+SOverlay::Slot()
		.Expose(MessageSlot);

	MessageSlot->AttachWidget(SAssignNew(EnableLoggingPrompt, SPromptConcertLoggingEnabled));
	ConcertTransportEvents::OnConcertTransportLoggingEnabledChangedEvent().AddSP(this, &SConcertClientsTabView::OnConcertLoggingEnabledChanged);
	return EnableLoggingPromptOverlay.ToSharedRef();
}

void SConcertClientsTabView::OnConcertLoggingEnabledChanged(bool bNewEnabled)
{
	if (bNewEnabled)
	{
		EnableLoggingPromptOverlay->RemoveSlot(EnableLoggingPrompt.ToSharedRef());
	}
	else
	{
		EnableLoggingPromptOverlay->AddSlot().AttachWidget(SAssignNew(EnableLoggingPrompt, SPromptConcertLoggingEnabled));
	}
}

#undef LOCTEXT_NAMESPACE
