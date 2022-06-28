// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertClientBrowser.h"

#include "ClientBrowserItem.h"
#include "ConcertServerStyle.h"
#include "MultiUserServerModule.h"
#include "Models/IClientBrowserModel.h"
#include "Models/IClientNetworkStatisticsModel.h"
#include "SConcertClientBrowserItem.h"
#include "Algo/Accumulate.h"
#include "Algo/AllOf.h"

#include "Algo/AnyOf.h"
#include "Algo/Count.h"
#include "Dialog/SMessageDialog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STileView.h"
#include "Window/ModalWindowManager.h"

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
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		[
			CreateKeepDisconnectedClients()
		];
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowser::CreateKeepDisconnectedClients()
{
	return SNew(SHorizontalBox)
		.ToolTipText(LOCTEXT("KeepDisconnectedClients.Tooltip", "Whether to keep clients that have disconnected in memory. This may be useful in unstable networks when you want to analyse why clients keep disconnecting."))
		
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("KeepDisconnectedClients.Label", "Keep Disconnected"))
			]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this](){ return BrowserModel->ShouldKeepClientsAfterDisconnect() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
			{
				const bool bKeepDisconnected = NewState == ECheckBoxState::Checked;
				if (bKeepDisconnected)
				{
					BrowserModel->SetKeepClientsAfterDisconnect(true);
					return;
				}
				
				const int32 NumDisconnected = Algo::CountIf(BrowserModel->GetItems(), [](const TSharedPtr<FClientBrowserItem>& Item) { return Item->bIsDisconnected; });
				if (NumDisconnected == 0)
				{
					BrowserModel->SetKeepClientsAfterDisconnect(false);
					return;
				}
				
				const TSharedRef<SMessageDialog> Dialog = SNew(SMessageDialog)
					.Title(LOCTEXT("RemoveDisconnectedClients.Title", "Remove disconnected clients?"))
					.Icon(FAppStyle::Get().GetBrush("Icons.WarningWithColor.Large"))
					.Message(FText::Format(LOCTEXT("RemoveDisconnectedClients.MessageFmt", "There are {0} disconnected clients. If you proceed, these clients will be removed from the session browser; opened log tabs will remain open.\nProceed?"), NumDisconnected))
					.UseScrollBox(false)
					.Buttons({
						SMessageDialog::FButton(LOCTEXT("RemoveButton", "Remove"))
							.SetOnClicked(FSimpleDelegate::CreateLambda([this]()
							{
								BrowserModel->SetKeepClientsAfterDisconnect(false);
							})),
						SMessageDialog::FButton(LOCTEXT("CancelButton", "Keep"))
							.SetPrimary(true)
							.SetFocus()
					});
				FConcertServerUIModule::Get().GetModalWindowManager()->ShowFakeModalWindow(Dialog);
			})
		]
	;
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowser::CreateTileView()
{
	constexpr int32 Height = 270;
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

void UE::MultiUserServer::SConcertClientBrowser::OnClientListChanged(TSharedPtr<FClientBrowserItem> Item, IClientBrowserModel::EClientUpdateType UpdateType)
{
	switch (UpdateType)
	{
	case IClientBrowserModel::EClientUpdateType::Added:
		if (PassesFilter(Item))
		{
			DisplayedClients.Add(Item);
		}
		break;
	case IClientBrowserModel::EClientUpdateType::Removed:
		DisplayedClients.Remove(Item);
		break;
	default:
		checkNoEntry();
	}

	TileView->RequestListRefresh();
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
	MenuBuilder.AddMenuEntry(
		LOCTEXT("SessionlessEndpoints.Label", "Show Sessionless clients"),
		LOCTEXT("SessionlessEndpoints.Tooltip", "Whether to show clients that are only discovering available sessions"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				bShowSessionlessClients = !bShowSessionlessClients;
				UpdateTileViewFromAllowedSessions();
			}),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked::CreateLambda([this](){ return bShowSessionlessClients; })),
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

	const bool bAtLeastOneClient = BrowserModel->GetItems().Num() > 0;
	if (bAtLeastOneClient)
	{
		return LOCTEXT("AllFilteredOut", "All results have been filtered. Try changing your active filters above.");
	}
	
	return LOCTEXT("NoClients", "No known clients");
}

TSharedRef<ITableRow> UE::MultiUserServer::SConcertClientBrowser::MakeTileViewWidget(TSharedPtr<FClientBrowserItem> ClientItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FMessagingNodeId MessagingNodeId = ClientItem->MessageNodeId;
	const TSharedPtr<SConcertClientBrowserItem>* ExistingWidget = ClientWidgets.Find(MessagingNodeId);
	if (!ExistingWidget)
	{
		ClientWidgets.Add(MessagingNodeId, SNew(SConcertClientBrowserItem, ClientItem.ToSharedRef(), StatisticsModel.ToSharedRef())
			 .HighlightText(HighlightText)
			 );
	}
	
	return SNew(STableRow<TSharedPtr<FClientBrowserItem>>, OwnerTable )
		.Padding(2.f)
		.Style(FConcertServerStyle::Get(), "Concert.Clients.TileTableRow")
		.Content()
		[
			ClientWidgets[MessagingNodeId].ToSharedRef()
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
	
	MenuBuilder.AddSeparator();
	AddDisplayModeEntry(MenuBuilder, EClientDisplayMode::NetworkGraph,
		LOCTEXT("DisplayMode.NetworkGraph.Title", "Network graph"),
		LOCTEXT("DisplayMode.NetworkGraph.Tooltip", "Show the up and down stream network traffic on a graph")
		);
	AddDisplayModeEntry(MenuBuilder, EClientDisplayMode::SegementTable,
		LOCTEXT("DisplayMode.SegementTable.Title", "Segment table"),
		LOCTEXT("DisplayMode.SegementTable.Tooltip", "A table displaying the messaging protocol's segments MessageId, Sent, Acked and Size in realtime.")
		);
	return MenuBuilder.MakeWidget();
}

void UE::MultiUserServer::SConcertClientBrowser::AddDisplayModeEntry(FMenuBuilder& MenuBuilder, EClientDisplayMode DisplayMode, FText Title, FText Tooltip) const
{
	MenuBuilder.AddMenuEntry(
		Title,
		Tooltip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, DisplayMode]()
			{
				TArray<TSharedPtr<FClientBrowserItem>> Items = TileView->GetSelectedItems();
				for (const TSharedPtr<FClientBrowserItem>& Item : Items)
				{
					if (const TSharedPtr<SConcertClientBrowserItem>* ItemWidget = ClientWidgets.Find(Item->MessageNodeId))
					{
						ItemWidget->Get()->SetDisplayMode(DisplayMode);
					}
				}
			}),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked::CreateLambda([this, DisplayMode]()
			{
				return Algo::AllOf(TileView->GetSelectedItems(), [this, DisplayMode](const TSharedPtr<FClientBrowserItem>& Item)
				{
					if (const TSharedPtr<SConcertClientBrowserItem>* ItemWidget = ClientWidgets.Find(Item->MessageNodeId))
					{
						return ItemWidget->Get()->GetDisplayMode() == DisplayMode;
					}
					return false;
				});
			})),
		NAME_None,
		EUserInterfaceActionType::Check
	);
}

void UE::MultiUserServer::SConcertClientBrowser::OnListMouseButtonDoubleClick(TSharedPtr<FClientBrowserItem> ClientItem)
{
	OnClientDoubleClicked.ExecuteIfBound(ClientItem->MessageNodeId);
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
	
	for (const TSharedPtr<FClientBrowserItem>& Item : BrowserModel->GetItems())
	{
		if (PassesFilter(Item))
		{
			DisplayedClients.Add(Item);
		}
	}
	TileView->RequestListRefresh();

	for (auto CachedClientWidgetIt = ClientWidgets.CreateIterator(); CachedClientWidgetIt; ++CachedClientWidgetIt)
	{
		const bool bShouldExist = BrowserModel->GetItems().ContainsByPredicate([MessageNodeId = CachedClientWidgetIt->Key](const TSharedPtr<FClientBrowserItem>& Item)
		{
			return Item->MessageNodeId == MessageNodeId;
		});
		if (!bShouldExist)
		{
			CachedClientWidgetIt.RemoveCurrent();
		}
	}
}

bool UE::MultiUserServer::SConcertClientBrowser::PassesFilter(const TSharedPtr<FClientBrowserItem>& Client) const
{
	const bool bIsSessionAllowed = Client->CurrentSession.IsSet() && AllowedSessions.Contains(*Client->CurrentSession);
	const bool bAllowBaseOnSession = bIsSessionAllowed || (bShowSessionlessClients && !Client->CurrentSession.IsSet());
	return bAllowBaseOnSession && SessionFilter->PassesFilter(Client);
}

void UE::MultiUserServer::SConcertClientBrowser::GenerateSearchTerms(const TSharedPtr<FClientBrowserItem>& Client, TArray<FString>& SearchTerms) const
{
	if (const TSharedPtr<SConcertClientBrowserItem>* ItemWidget = ClientWidgets.Find(Client->MessageNodeId))
	{
		ItemWidget->Get()->AppendSearchTerms(SearchTerms);
	}
}


#undef LOCTEXT_NAMESPACE
