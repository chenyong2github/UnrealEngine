// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertClientBrowserItem.h"

#include "ClientBrowserItem.h"
#include "ConcertServerStyle.h"
#include "INetworkMessagingExtension.h"
#include "SClientNetworkStats.h"
#include "Graph/SClientNetworkGraphs.h"
#include "Models/ClientTransferStatisticsModel.h"
#include "Models/IClientNetworkStatisticsModel.h"
#include "Table/SClientOutboundTransferStatTable.h"
#include "Table/SClientInboundTransferStatTable.h"

#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/LayerManager/STooltipPresenter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI.SConcertClientBrowserItem"

void UE::MultiUserServer::SConcertClientBrowserItem::Construct(const FArguments& InArgs, TSharedRef<FClientBrowserItem> InClientItem, TSharedRef<IClientNetworkStatisticsModel> InStatModel)
{
	Item = MoveTemp(InClientItem);
	StatModel = MoveTemp(InStatModel);
	TransferStatsModel = MakeShared<FClientTransferStatisticsModel>(Item->ClientAddress);
	HighlightText = InArgs._HighlightText;

	ChildSlot
	.Padding(FMargin(0.0f, 0.0f, 4.0f, 4.0f))
	[
		// Shadow behind thumbnail
		SNew(SBorder)
		.BorderImage(FConcertServerStyle::Get().GetBrush("Concert.Clients.DropShadow"))
		.Padding(FMargin(0.0f, 0.0f, 5.0f, 5.0f))
		[
			// Change outside of thumbnail depending on hover state; lighter than inside
			SNew(SBorder)
			.BorderImage(this, &SConcertClientBrowserItem::GetBackgroundImage)
			.Padding(2.f)
			[
				// Inside of thumbnail is darker
				SNew(SBorder)
				.BorderImage(FConcertServerStyle::Get().GetBrush("Concert.Clients.ThumbnailTitle"))
				.Padding(2.f)
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Top)
					[
						CreateHeader()	
					]
					
					+SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(-2.f, 5.f, -2.f, 0.f)
					[
						CreateContentArea()
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 5.f, 0.f, 0.f)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Bottom)
					[
						CreateStats()
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(-2.f, 5.f, -2.f, -2.f)
					.VAlign(VAlign_Bottom)
					[
						CreateFooter()
					]
				]
			]
		]
	];
}

FString UE::MultiUserServer::SConcertClientBrowserItem::GetClientDisplayName() const
{
	const TOptional<FConcertClientInfo> ClientInfo = Item->GetClientInfo.Execute();
	if (ClientInfo)
	{
		return ClientInfo->DisplayName;
	}
	
	const FString NodeIdAsString = Item->MessageNodeId.ToString(EGuidFormats::DigitsWithHyphens);
	int32 Index = INDEX_NONE;
	const bool bFound = NodeIdAsString.FindChar('-', Index);
	// Avoid making the name too long by only showing the first few digits of the node ID
	return FString::Printf(TEXT("Admin (%s)"), bFound ? *NodeIdAsString.Left(Index) : *NodeIdAsString);
}

void UE::MultiUserServer::SConcertClientBrowserItem::AppendSearchTerms(TArray<FString>& SearchTerms) const
{
	NetworkStats->AppendSearchTerms(SearchTerms);
	SearchTerms.Add(ClientName->GetText().ToString());
	SearchTerms.Add(ClientIP4->GetText().ToString());
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowserItem::CreateHeader()
{
	return SAssignNew(ClientName, STextBlock)
		.Font(FConcertServerStyle::Get().GetFontStyle("Concert.Clients.ClientNameTileFont"))
		.Text_Lambda([this](){ return FText::FromString(GetClientDisplayName()); })
		.ToolTipText_Lambda([this]()
		{
			const TOptional<FConcertClientInfo> ClientInfo = Item->GetClientInfo.Execute();
			if (!ClientInfo)
			{
				return FText::Format(
					LOCTEXT("Name.NotAvailable.TooltipFmt", "This client's display information becomes available after joining a session.\nNodeID: {0}\nAddress ID: {1}"),
					FText::FromString(Item->MessageNodeId.ToString(EGuidFormats::DigitsWithHyphens)),
					FText::FromString(Item->ClientAddress.ToString())
					);
			}
			return FText::Format(
					LOCTEXT("Name.Available.TooltipFmt", "NodeID: {0}\nAddress ID: {1}"),
					FText::FromString(Item->MessageNodeId.ToString(EGuidFormats::DigitsWithHyphens)),
					FText::FromString(Item->ClientAddress.ToString())
					);
		})
		.HighlightText_Lambda([this](){ return *HighlightText; })
		.ColorAndOpacity(FColor::White);
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowserItem::CreateContentArea()
{
	return SNew(SOverlay)
		+SOverlay::Slot()
		[
			SNew(SClientNetworkGraphs, TransferStatsModel.ToSharedRef())
			.Visibility_Lambda([this](){ return GetDisplayMode() == EClientDisplayMode::NetworkGraph ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+SOverlay::Slot().Padding(4.f, 0.f)
		[
			SNew(SClientOutboundTransferStatTable, TransferStatsModel.ToSharedRef())
			.Visibility_Lambda([this](){ return GetDisplayMode() == EClientDisplayMode::OutboundSegementTable ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+SOverlay::Slot().Padding(4.f, 0.f)
		[
			SNew(SClientInboundTransferStatTable, TransferStatsModel.ToSharedRef())
			.Visibility_Lambda([this](){ return GetDisplayMode() == EClientDisplayMode::InboundSegmentTable ? EVisibility::Visible : EVisibility::Collapsed; })
		];
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowserItem::CreateStats()
{
	return SNew(SScaleBox)
		.Stretch(EStretch::ScaleToFit)
		.HAlign(HAlign_Fill)
		[
			SAssignNew(NetworkStats, SClientNetworkStats, Item->ClientAddress, StatModel.ToSharedRef())
			.HighlightText(HighlightText)
		];
}

TSharedRef<SWidget> UE::MultiUserServer::SConcertClientBrowserItem::CreateFooter()
{
	return SNew(SBorder)
		.BorderImage(FConcertServerStyle::Get().GetBrush("Concert.Clients.ThumbnailFooter"))
		[
			SNew(SHorizontalBox)

			// Online / offline indicator
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.Padding(2.f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
				.ColorAndOpacity_Lambda([this]()
				{
					return StatModel->IsOnline(Item->ClientAddress)
						? FStyleColors::AccentGreen
						: FStyleColors::AccentGray;
				})
				.ToolTipText_Lambda([this]()
				{
					return StatModel->IsOnline(Item->ClientAddress)
						? LOCTEXT("ConnectionIndicator.Online", "Connected")
						: LOCTEXT("ConnectionIndicator.Offline", "Not reachable");
				})
			]

			// IP address
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.Padding(2.f)
			[
				SAssignNew(ClientIP4, STextBlock)
				.ColorAndOpacity(FColor::White)
				.HighlightText_Lambda([this](){ return *HighlightText; })
				.Text_Lambda([this]()
				{
					const FString Fallback(TEXT("No IP available"));
					if (const TOptional<FMessageTransportStatistics> Statistics = StatModel->GetLatestNetworkStatistics(Item->ClientAddress))
					{
						const FString DisplayString = Statistics->IPv4AsString.IsEmpty()
							? Fallback
							: Statistics->IPv4AsString;
						return FText::FromString(DisplayString);
					}
					return FText::FromString(Fallback);
				})
			]
		];
}

const FSlateBrush* UE::MultiUserServer::SConcertClientBrowserItem::GetBackgroundImage() const
{
	if (IsHovered())
	{
		return FConcertServerStyle::Get().GetBrush("Concert.Clients.ThumbnailAreaHoverBackground");
	}
	return FConcertServerStyle::Get().GetBrush("Concert.Clients.ThumbnailAreaBackground");
}

#undef LOCTEXT_NAMESPACE