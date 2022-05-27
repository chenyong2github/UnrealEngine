// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertClientBrowser.h"

#include "ClientBrowserItem.h"
#include "ConcertServerStyle.h"
#include "SConcertClientBrowserItem.h"
#include "Models/IClientBrowserModel.h"

#include "Algo/AnyOf.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Models/IClientNetworkStatisticsModel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/STileView.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI.SConcertClientBrowser"

void UE::MultiUserServer::SConcertClientBrowser::Construct(const FArguments& InArgs, TSharedRef<IClientBrowserModel> InBrowserModel, TSharedRef<IClientNetworkStatisticsModel> InStatisticsModel)
{
	BrowserModel = MoveTemp(InBrowserModel);
	StatisticsModel = MoveTemp(InStatisticsModel);
	
	HighlightText = MakeShared<FText>();
	SessionFilter = MakeShared<FClientTextFilter>(FClientTextFilter::FItemToStringArray::CreateSP(this, &SConcertClientBrowser::GenerateSearchTerms));
	SessionFilter->OnChanged().AddSP(this, &SConcertClientBrowser::UpdateTileViewFromAllowedSessions);

	OnClientDoubleClicked = InArgs._OnClientDoubleClicked;

	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			CreateSearchArea(InArgs)
		]

		+SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(5.f)
		[
			SNew(SOverlay)

			+SOverlay::Slot()
			[
				CreateTileView()
			]

			+SOverlay::Slot()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility_Lambda([this](){ return DisplayedClients.Num() == 0 ? EVisibility::Visible : EVisibility::Hidden; })
				[
					SNew(STextBlock)
					.Text(this, &SConcertClientBrowser::GetErrorMessageText)
				]
			]
		]
	];

	BrowserModel->OnSessionCreated().AddSP(this, &SConcertClientBrowser::OnSessionCreated);
	BrowserModel->OnSessionDestroyed().AddSP(this, &SConcertClientBrowser::OnSessionDestroyed);
	BrowserModel->OnClientListChanged().AddSP(this, &SConcertClientBrowser::OnClientListChanged);

	AllowAllSessions();
}

void UE::MultiUserServer::SConcertClientBrowser::ShowOnlyClientsFromSession(const FGuid& SessionId)
{
	AllowedSessions = { SessionId };
	UpdateTileViewFromAllowedSessions();
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowser::CreateSearchArea(const FArguments& InArgs)
{
	return SNew(SHorizontalBox)
	
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &SConcertClientBrowser::MakeSessionOption)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return AllowedSessions.Num() == BrowserModel->GetSessions().Num()
						? LOCTEXT("MessageActionFilter.Selection.All", "All")
						: FText::FromString(FString::FromInt(AllowedSessions.Num()));
				})
			]
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.FillWidth(0.65f)
		.Padding(4.f, 0.f)
		.VAlign(VAlign_Fill)
		[
			SNew(SSearchBox)
			.OnTextChanged_Lambda([this](const FText& SearchText)
			{
				*HighlightText = SearchText;
				SessionFilter->SetRawFilterText(SearchText);
			})
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			InArgs._RightOfSearch.Widget
		];
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowser::CreateTileView()
{
	constexpr int32 Height = 200;
	constexpr int32 Width = 270;
	return SAssignNew(TileView, STileView<TSharedPtr<FClientBrowserItem>>)
		.SelectionMode(ESelectionMode::Multi)
		.ListItemsSource(&DisplayedClients)
		.OnGenerateTile(this, &SConcertClientBrowser::MakeTileViewWidget)
		.OnContextMenuOpening(this, &SConcertClientBrowser::OnGetContextMenuContent)
		.OnMouseButtonDoubleClick(this, &SConcertClientBrowser::OnListMouseButtonDoubleClick)
		.ItemHeight(Height)
		.ItemWidth(Width)
	;
}

void UE::MultiUserServer::SConcertClientBrowser::OnSessionCreated(const FGuid& SessionId)
{
	if (bShowAllSessions)
	{
		AllowedSessions.Add(SessionId);
		UpdateTileViewFromAllowedSessions();
	}
}

void UE::MultiUserServer::SConcertClientBrowser::OnSessionDestroyed(const FGuid& SessionId)
{
	if (AllowedSessions.Contains(SessionId))
	{
		AllowedSessions.Remove(SessionId);
		UpdateTileViewFromAllowedSessions();
	}
}

void UE::MultiUserServer::SConcertClientBrowser::OnClientListChanged(const FGuid& SessionId, EConcertClientStatus UpdateType, const FConcertSessionClientInfo& ClientInfo)
{
	switch (UpdateType)
	{
	case EConcertClientStatus::Connected:
		DisplayedClients.Add(MakeShared<FClientBrowserItem>(ClientInfo, BrowserModel->GetClientAddress(ClientInfo.ClientEndpointId)));
		TileView->RequestListRefresh();
		break;
	case EConcertClientStatus::Disconnected:
		RemoveClient(ClientInfo);
		break;
	case EConcertClientStatus::Updated:
		UpdateClientInfo(ClientInfo);
		break;
	default: ;
	}
}

void UE::MultiUserServer::SConcertClientBrowser::RemoveClient(const FConcertSessionClientInfo& ClientInfo)
{
	const int32 Index = DisplayedClients.IndexOfByPredicate([&ClientInfo](const TSharedPtr<FClientBrowserItem>& Item)
		{
			return Item->ClientInfo.ClientEndpointId == ClientInfo.ClientEndpointId;
		});
	if (ensure(Index != INDEX_NONE))
	{
		DisplayedClients.RemoveAt(Index);
		ClientWidgets.Remove(ClientInfo.ClientEndpointId);
		TileView->RequestListRefresh();
	}
}

void UE::MultiUserServer::SConcertClientBrowser::UpdateClientInfo(const FConcertSessionClientInfo& ClientInfo)
{
	if (const TSharedPtr<SConcertClientBrowserItem>* Widget = ClientWidgets.Find(ClientInfo.ClientEndpointId))
	{
		Widget->Get()->OnClientInfoChanged();
	}
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowser::SConcertClientBrowser::MakeSessionOption()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SelectAll", "All"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				if (BrowserModel->GetSessions().Num() == AllowedSessions.Num())
				{
					DisallowAllSessions();
				}
				else
				{
					AllowAllSessions();
				}
			}),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked::CreateLambda([this](){ return BrowserModel->GetSessions().Num() == AllowedSessions.Num(); })),
		NAME_None,
		EUserInterfaceActionType::Check
	);
	MenuBuilder.AddSeparator();
	for (const FGuid& SessionId : BrowserModel->GetSessions())
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(BrowserModel->GetSessionInfo(SessionId)->SessionName),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, SessionId]()
				{
					if (AllowedSessions.Contains(SessionId))
					{
						AllowedSessions.Remove(SessionId);
					}
					else
					{
						AllowedSessions.Add(SessionId);
					}
					UpdateTileViewFromAllowedSessions();
				}),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this, SessionId]{ return AllowedSessions.Contains(SessionId); })),
			NAME_None,
			EUserInterfaceActionType::Check
		);
	}

	return MenuBuilder.MakeWidget();
}

FText UE::MultiUserServer::SConcertClientBrowser::GetErrorMessageText() const
{
	if (BrowserModel->GetSessions().Num() == 0)
	{
		return LOCTEXT("NoLiveSessions", "No live sessions");
	}
	
	if (AllowedSessions.Num() == 0)
	{
		return LOCTEXT("NoSessionsSelected", "All live sessions filtered out");
	}

	const bool bAtLeastOneClient = Algo::AnyOf(AllowedSessions, [this](const FGuid& SessionId)
	{
		return BrowserModel->GetSessionClients(SessionId).Num() > 0;
	});
	if (bAtLeastOneClient)
	{
		return LOCTEXT("AllFilteredOut", "All results have been filtered. Try changing your active filters above.");
	}
	
	return LOCTEXT("NoClients", "No clients connected to selected sessions");
}

TSharedRef<ITableRow> UE::MultiUserServer::SConcertClientBrowser::MakeTileViewWidget(TSharedPtr<FClientBrowserItem> ClientItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FClientEndpointId ClientId = ClientItem->ClientInfo.ClientEndpointId;
	TSharedPtr<SConcertClientBrowserItem>* ExistingWidget = ClientWidgets.Find(ClientId);
	if (!ExistingWidget)
	{
		ClientWidgets.Add(ClientItem->ClientInfo.ClientEndpointId, SNew(SConcertClientBrowserItem, ClientItem.ToSharedRef(), StatisticsModel.ToSharedRef())
			 .HighlightText(HighlightText)
			 );
	}
	
	return SNew(STableRow<TSharedPtr<FClientBrowserItem>>, OwnerTable )
		.Padding(2.f)
		.Style(FConcertServerStyle::Get(), "Concert.Clients.TileTableRow")
		.Content()
		[
			ClientWidgets[ClientId].ToSharedRef()
		];
}

TSharedPtr<SWidget> UE::MultiUserServer::SConcertClientBrowser::OnGetContextMenuContent()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenLog", "Open log"),
		LOCTEXT("OpenLogTooltip", "Opens a new tab in which you can filter log events related to this client"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				TArray<TSharedPtr<FClientBrowserItem>> Items = TileView->GetSelectedItems();
				for (const TSharedPtr<FClientBrowserItem>& Item : Items)
				{
					OnListMouseButtonDoubleClick(Item);
				}
			}),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	
	return MenuBuilder.MakeWidget();
}

void UE::MultiUserServer::SConcertClientBrowser::OnListMouseButtonDoubleClick(TSharedPtr<FClientBrowserItem> ClientItem)
{
	OnClientDoubleClicked.ExecuteIfBound(ClientItem->ClientInfo.ClientEndpointId);
}

void UE::MultiUserServer::SConcertClientBrowser::SConcertClientBrowser::AllowAllSessions()
{
	AllowedSessions = BrowserModel->GetSessions();
	UpdateTileViewFromAllowedSessions();
}

void UE::MultiUserServer::SConcertClientBrowser::DisallowAllSessions()
{
	AllowedSessions.Reset();
	UpdateTileViewFromAllowedSessions();
}

void UE::MultiUserServer::SConcertClientBrowser::UpdateTileViewFromAllowedSessions()
{
	bShowAllSessions = AllowedSessions.Num() == BrowserModel->GetSessions().Num();
	DisplayedClients.Empty();
	
	for (const FGuid& AllowedSession : AllowedSessions)
	{
		for (const FConcertSessionClientInfo& ClientInfo : BrowserModel->GetSessionClients(AllowedSession))
		{
			if (SessionFilter->PassesFilter(ClientInfo))
			{
				DisplayedClients.Add(MakeShared<FClientBrowserItem>(ClientInfo, BrowserModel->GetClientAddress(ClientInfo.ClientEndpointId)));
			}
		}
	}
	TileView->RequestListRefresh();
}

void UE::MultiUserServer::SConcertClientBrowser::GenerateSearchTerms(const FConcertSessionClientInfo& ClientInfo, TArray<FString>& SearchTerms) const
{
	if (const TSharedPtr<SConcertClientBrowserItem>* ItemWidget = ClientWidgets.Find(ClientInfo.ClientEndpointId))
	{
		ItemWidget->Get()->AppendSearchTerms(SearchTerms);
	}
}

#undef LOCTEXT_NAMESPACE
