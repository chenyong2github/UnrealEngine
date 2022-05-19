// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertTransportLog.h"

#include "ConcertHeaderRowUtils.h"
#include "Filter/ConcertLogFilter_FrontendRoot.h"
#include "Filter/ConcertFrontendLogFilter_TextSearch.h"
#include "Util/ConcertLogTokenizer.h"
#include "SConcertTransportLogFooter.h"
#include "SConcertTransportLogRow.h"
#include "Settings/ConcertTransportLogSettings.h"
#include "Settings/MultiUserServerColumnVisibilitySettings.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

const FName SConcertTransportLog::FirstColumnId("AvatarColourColumnId");

void SConcertTransportLog::Construct(const FArguments& InArgs, TSharedRef<IConcertLogSource> LogSource)
{
	PagedLogList = MakeShared<FPagedFilteredConcertLogList>(MoveTemp(LogSource), InArgs._Filter);
	LogTokenizer = MakeShared<FConcertLogTokenizer>();
	HighlightText = MakeShared<FText>();

	GetClientInfoFunc = InArgs._GetClientInfo;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.6, 0.6, 0.6)))
		.Padding(2)
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			[
				InArgs._Filter ? InArgs._Filter->BuildFilterWidgets() : SNullWidget::NullWidget
			]

			+SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(0, 5, 0, 0)
			[
				CreateTableView()
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SConcertTransportLogFooter, PagedLogList.ToSharedRef())
				.ExtendViewOptions(this, &SConcertTransportLog::ExtendViewOptions)
			]
		]
	];

	// Subscribe to events
	PagedLogList->OnPageViewChanged().AddSP(this, &SConcertTransportLog::OnPageViewChanged);
	if (InArgs._Filter)
	{
		InArgs._Filter->GetTextSearchFilter()->OnSearchTextChanged().AddSP(this, &SConcertTransportLog::OnSearchTextChanged);
	}
	UMultiUserServerColumnVisibilitySettings::GetSettings()->OnTransportLogColumnVisibility().AddSP(this, &SConcertTransportLog::OnColumnVisibilitySettingsChanged);
	
	UE::ConcertSharedSlate::RestoreColumnVisibilityState(HeaderRow.ToSharedRef(), UMultiUserServerColumnVisibilitySettings::GetSettings()->GetTransportLogColumnVisibility());
}

TSharedRef<SWidget> SConcertTransportLog::CreateTableView()
{
	return SAssignNew(LogView, SListView<TSharedPtr<FConcertLogEntry>>)
		.ListItemsSource(&PagedLogList->GetPageView())
		.OnGenerateRow(this, &SConcertTransportLog::OnGenerateActivityRowWidget)
		.SelectionMode(ESelectionMode::None)
		.HeaderRow(CreateHeaderRow());
}

TSharedRef<SHeaderRow> SConcertTransportLog::CreateHeaderRow()
{
	HeaderRow = SNew(SHeaderRow)
		.OnHiddenColumnsListChanged_Lambda([this]()
		{
			if (!bIsUpdatingColumnVisibility)
			{
				UMultiUserServerColumnVisibilitySettings::GetSettings()->SetTransportLogColumnVisibility(
				UE::ConcertSharedSlate::SnapshotColumnVisibilityState(HeaderRow.ToSharedRef())
				);
			}
		})
		// Create tiny dummy row showing avatar colour to handle the case when a user hides all columns
		+SHeaderRow::Column(FirstColumnId)
			.DefaultLabel(FText::GetEmpty())
			.FixedWidth(8)
			// Cannot be hidden
			.ShouldGenerateWidget(true)
			.ToolTipText(LOCTEXT("AvatarColumnToolTipText", "The colour of the avatar is affected by log"))
		;

	// The following properties are useless to the user
	const TSet<const FProperty*> SkippedProperties = {
		// Used by code only
		FConcertLog::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConcertLog, CustomPayloadUncompressedByteSize))
	};
	
	for (TFieldIterator<const FProperty> PropertyIt(FConcertLog::StaticStruct()); PropertyIt; ++PropertyIt)
	{
		if (!PropertyIt->HasAnyPropertyFlags(CPF_Transient) && !SkippedProperties.Contains(*PropertyIt))
		{
			const FString PropertyName = PropertyIt->GetAuthoredName();
			const FName ColumnId = PropertyIt->GetFName();
			HeaderRow->AddColumn(SHeaderRow::FColumn::FArguments()
				.ColumnId(ColumnId)
				.DefaultLabel(FText::FromString(PropertyName))
				.HAlignCell(HAlign_Center)
				// Add option to hide
				.OnGetMenuContent_Lambda([this, ColumnId]()
				{
					return UE::ConcertSharedSlate::MakeHideColumnContextMenu(HeaderRow.ToSharedRef(), ColumnId);
				}));
		}
	}

	TGuardValue<bool> DoNotSave(bIsUpdatingColumnVisibility, true);
	const TSet<FName> HiddenByDefault = {
		GET_MEMBER_NAME_CHECKED(FConcertLog, Frame),
		GET_MEMBER_NAME_CHECKED(FConcertLog, MessageId),
		GET_MEMBER_NAME_CHECKED(FConcertLog, MessageOrderIndex),
		GET_MEMBER_NAME_CHECKED(FConcertLog, ChannelId),
		GET_MEMBER_NAME_CHECKED(FConcertLog, CustomPayloadTypename),
		GET_MEMBER_NAME_CHECKED(FConcertLog, StringPayload)
	};
	for (const FName ColumnID : HiddenByDefault)
	{
		HeaderRow->SetShowGeneratedColumn(ColumnID, false);
	}
	
	return HeaderRow.ToSharedRef();
}

TSharedRef<ITableRow> SConcertTransportLog::OnGenerateActivityRowWidget(TSharedPtr<FConcertLogEntry> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SConcertTransportLogRow, Item, OwnerTable, LogTokenizer.ToSharedRef(), HighlightText.ToSharedRef())
		.GetClientInfo(GetClientInfoFunc);
}

void SConcertTransportLog::ExtendViewOptions(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AutoScroll", "Auto Scroll"),
		LOCTEXT("AutoScroll_Tooltip", "Automatically scroll as new logs arrive (affects last page)"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ bAutoScroll = !bAutoScroll; }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked::CreateLambda([this] { return bAutoScroll; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
			LOCTEXT("DisplayTimestampInRelativeTime", "Display Relative Time"),
			TAttribute<FText>::CreateLambda([this]()
			{
				const bool bIsVisible = HeaderRow->IsColumnVisible(GET_MEMBER_NAME_CHECKED(FConcertLog, Timestamp));
				return bIsVisible
					? LOCTEXT("DisplayTimestampInRelativeTime.Tooltip.Visible", "Display the Last Modified column in relative time?")
					: LOCTEXT("DisplayTimestampInRelativeTime.Tooltip.Hidden", "Disabled because the Timestamp column is hidden.");
			}),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SConcertTransportLog::OnFilterMenuChecked),
				FCanExecuteAction::CreateLambda([this] { return HeaderRow->IsColumnVisible(GET_MEMBER_NAME_CHECKED(FConcertLog, Timestamp)); }),
				FIsActionChecked::CreateLambda([this] { return UConcertTransportLogSettings::GetSettings()->TimestampTimeFormat == ETimeFormat::Relative; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

					
	MenuBuilder.AddSeparator();
	UE::ConcertSharedSlate::AddEntriesForShowingHiddenRows(HeaderRow.ToSharedRef(), MenuBuilder);
}

void SConcertTransportLog::OnFilterMenuChecked()
{
	UConcertTransportLogSettings* Settings = UConcertTransportLogSettings::GetSettings();
	
	switch (Settings->TimestampTimeFormat)
	{
	case ETimeFormat::Relative:
		Settings->TimestampTimeFormat = ETimeFormat::Absolute;
		break;
	case ETimeFormat::Absolute: 
		Settings->TimestampTimeFormat = ETimeFormat::Relative;
		break;
	default:
		checkNoEntry();
	}

	Settings->SaveConfig();
}

void SConcertTransportLog::OnPageViewChanged(const TArray<TSharedPtr<FConcertLogEntry>>&)
{
	LogView->RequestListRefresh();

	if (bAutoScroll)
	{
		LogView->ScrollToBottom();
	}
}

void SConcertTransportLog::OnSearchTextChanged(const FText& NewSearchText)
{
	*HighlightText = NewSearchText;
}

void SConcertTransportLog::OnColumnVisibilitySettingsChanged(const FColumnVisibilitySnapshot& ColumnSnapshot)
{
	TGuardValue<bool> GuardValue(bIsUpdatingColumnVisibility, true);
	UE::ConcertSharedSlate::RestoreColumnVisibilityState(HeaderRow.ToSharedRef(), ColumnSnapshot);
}

#undef LOCTEXT_NAMESPACE