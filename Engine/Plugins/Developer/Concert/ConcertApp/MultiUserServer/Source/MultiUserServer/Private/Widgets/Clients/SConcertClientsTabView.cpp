// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertClientsTabView.h"

#include "ConcertUtil.h"
#include "IConcertServer.h"
#include "IConcertSyncServer.h"
#include "Logging/Filter/ConcertLogFilter_FrontendRoot.h"
#include "Logging/Source/GlobalLogSource.h"
#include "Logging/Util/ConcertLogTokenizer.h"
#include "Util/EndpointToUserNameCache.h"
#include "Widgets/Clients/Browser/SConcertNetworkBrowser.h"
#include "Widgets/Clients/Browser/Models/ClientBrowserModel.h"
#include "Widgets/Clients/Browser/Models/ClientNetworkStatisticsModel.h"
#include "Widgets/Clients/Logging/SConcertTransportLog.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI.SConcertClientsTabView"

const FName SConcertClientsTabView::ClientBrowserTabId("ClientBrowserTabId");
const FName SConcertClientsTabView::GlobalLogTabId("GlobalLogTabId");

void SConcertClientsTabView::Construct(const FArguments& InArgs, FName InStatusBarID, TSharedRef<IConcertSyncServer> InServer, TSharedRef<FGlobalLogSource> InLogBuffer)
{
	Server = MoveTemp(InServer);
	LogBuffer = MoveTemp(InLogBuffer);
	ClientInfoCache = MakeShared<FEndpointToUserNameCache>(Server->GetConcertServer());
	LogTokenizer = MakeShared<FConcertLogTokenizer>(ClientInfoCache.ToSharedRef());
	
	SConcertTabViewWithManagerBase::Construct(
		SConcertTabViewWithManagerBase::FArguments()
		.ConstructUnderWindow(InArgs._ConstructUnderWindow)
		.ConstructUnderMajorTab(InArgs._ConstructUnderMajorTab)
		.CreateTabs(FCreateTabs::CreateLambda([this, &InArgs](const TSharedRef<FTabManager>& InTabManager, const TSharedRef<FTabManager::FLayout>& InLayout)
		{
			CreateTabs(InTabManager, InLayout, InArgs);
		}))
		.LayoutName("ConcertClientsTabView_v0.1"),
		InStatusBarID
	);
}

void SConcertClientsTabView::ShowConnectedClients(const FGuid& SessionId) const
{
	ClientBrowser->ShowOnlyClientsFromSession(SessionId);
}

void SConcertClientsTabView::OpenClientLogTab(const FGuid& ClientMessageNodeId) const
{
	const FName TabId = *ClientMessageNodeId.ToString();
	if (const TSharedPtr<SDockTab> ExistingTab = GetTabManager()->FindExistingLiveTab(FTabId(TabId)))
	{
		GetTabManager()->DrawAttention(ExistingTab.ToSharedRef());
	}
	else
	{
		const TOptional<FConcertClientInfo> ClientInfo = ClientInfoCache->GetClientInfoFromNodeId(ClientMessageNodeId);
		const TSharedRef<SDockTab> NewTab = SNew(SDockTab)
			.Label_Lambda([this, ClientMessageNodeId]()
			{
				const TOptional<FConcertClientInfo> ClientInfo = ClientInfoCache->GetClientInfoFromNodeId(ClientMessageNodeId);
				return ClientInfo
					? FText::Format(LOCTEXT("ClientTabFmt", "{0} Log"), FText::FromString(ClientInfo->DisplayName))
					: FText::FromString(ClientMessageNodeId.ToString(EGuidFormats::DigitsWithHyphens));
			})
			.ToolTipText(FText::Format(LOCTEXT("ClientTabTooltipFmt", "Logs all networked requests originating or going to client {0} (NodeId = {1})"), ClientInfo ? FText::FromString(ClientInfo->DisplayName) : FText::GetEmpty(), FText::FromString(ClientMessageNodeId.ToString())))
			.TabRole(PanelTab)
			[
				SNew(SConcertTransportLog, LogBuffer.ToSharedRef(), ClientInfoCache.ToSharedRef(), LogTokenizer.ToSharedRef())
				.Filter(UE::MultiUserServer::MakeClientLogFilter(LogTokenizer.ToSharedRef(), ClientMessageNodeId, ClientInfoCache.ToSharedRef()))
			]; 

		// We need a tab to place the client tab next to
		if (IsGlobalLogOpen())
		{
			const FTabManager::FLiveTabSearch Search(GlobalLogTabId);
			GetTabManager()->InsertNewDocumentTab(TabId, Search, NewTab);
		}
		else
		{
			OpenGlobalLogTab();
			
			const FTabManager::FLiveTabSearch Search(GlobalLogTabId);
			GetTabManager()->InsertNewDocumentTab(TabId, Search, NewTab);

			CloseGlobalLogTab();
		}
	}
}

void SConcertClientsTabView::OpenGlobalLogTab() const
{
	GetTabManager()->TryInvokeTab(GlobalLogTabId);
}

void SConcertClientsTabView::CloseGlobalLogTab() const
{
	if (const TSharedPtr<SDockTab> GlobalLogTab = GetGlobalLogTab())
	{
		GlobalLogTab->RequestCloseTab();
	}
}

bool SConcertClientsTabView::IsGlobalLogOpen() const
{
	return GetGlobalLogTab().IsValid();
}

TSharedPtr<SDockTab> SConcertClientsTabView::GetGlobalLogTab() const
{
	return GetTabManager()->FindExistingLiveTab(GlobalLogTabId);
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
			SAssignNew(ClientBrowser, UE::MultiUserServer::SConcertNetworkBrowser,
				MakeShared<UE::MultiUserServer::FClientBrowserModel>(Server->GetConcertServer(), ClientInfoCache.ToSharedRef(), MakeShared<UE::MultiUserServer::FClientNetworkStatisticsModel>()))
			.RightOfSearch()
			[
				CreateOpenGlobalLogButton()
			]
			.OnClientDoubleClicked_Raw(this, &SConcertClientsTabView::OpenClientLogTab)
		]; 
}

TSharedRef<SDockTab> SConcertClientsTabView::SpawnGlobalLogTab(const FSpawnTabArgs& InTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("GlobalLogTabLabel", "Global Log"))
		.TabRole(PanelTab)
		[
			SNew(SConcertTransportLog, LogBuffer.ToSharedRef(), ClientInfoCache.ToSharedRef(), LogTokenizer.ToSharedRef())
			.Filter(UE::MultiUserServer::MakeGlobalLogFilter(LogTokenizer.ToSharedRef()))
		]; 
}

TSharedRef<SWidget> SConcertClientsTabView::CreateOpenGlobalLogButton() const
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ToolTipText(LOCTEXT("OpenGlobalLogTooltip", "Opens the Global Log which logs all incoming networked messages."))
		.ContentPadding(FMargin(1, 0))
		.Visibility_Lambda(
			[this]()
			{
				const bool bIsGlobalLogOpen = IsGlobalLogOpen();
				return bIsGlobalLogOpen ? EVisibility::Hidden : EVisibility::Visible;
			})
		.OnClicked_Lambda([this]()
		{
			OpenGlobalLogTab();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0, 0.0f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("Icons.Layout"))
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(4.0, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OpenGlobalLog", "Open Global Log"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
		];
}

#undef LOCTEXT_NAMESPACE
