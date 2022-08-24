// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPackageTransmissionTable.h"

#include "ConcertHeaderRowUtils.h"
#include "Model/IPackageTransmissionEntrySource.h"
#include "Model/PackageTransmissionEntry.h"
#include "Settings/MultiUserServerColumnVisibilitySettings.h"
#include "SPackageTransmissionTableFooter.h"
#include "SPackageTransmissionTableRow.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI.SPackageTransmissionTable"

namespace UE::MultiUserServer
{
	void SPackageTransmissionTable::Construct(const FArguments& InArgs, TSharedRef<IPackageTransmissionEntrySource> InPackageEntrySource, TSharedRef<FPackageTransmissionEntryTokenizer> InTokenizer)
	{
		PackageEntrySource = MoveTemp(InPackageEntrySource);
		Tokenizer = MoveTemp(InTokenizer);

		HighlightText = InArgs._HighlightText;
		CanScrollToLogDelegate = InArgs._CanScrollToLog;
		ScrollToLogDelegate = InArgs._ScrollToLog;
		
		ChildSlot
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(0, 5, 0, 0)
			[
				CreateTableView()
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SPackageTransmissionTableFooter, PackageEntrySource.ToSharedRef())
				.ExtendViewOptions(this, &SPackageTransmissionTable::ExtendViewOptions)
				.TotalUnfilteredNum(InArgs._TotalUnfilteredNum)
			]
		];

		UMultiUserServerColumnVisibilitySettings::GetSettings()->OnOnPackageTransmissionColumnVisibilityChanged().AddSP(this, &SPackageTransmissionTable::OnColumnVisibilitySettingsChanged);
		ConcertSharedSlate::RestoreColumnVisibilityState(HeaderRow.ToSharedRef(), UMultiUserServerColumnVisibilitySettings::GetSettings()->GetPackageTransmissionColumnVisibility());

		PackageEntrySource->OnPackageEntriesModified().AddSP(this, &SPackageTransmissionTable::OnPackageEntriesModified);
		PackageEntrySource->OnPackageEntriesAdded().AddSP(this, &SPackageTransmissionTable::OnPackageArrayChanged);
	}

	SPackageTransmissionTable::~SPackageTransmissionTable()
	{
		UMultiUserServerColumnVisibilitySettings::GetSettings()->OnOnPackageTransmissionColumnVisibilityChanged().RemoveAll(this);
		PackageEntrySource->OnPackageEntriesModified().RemoveAll(this);
		PackageEntrySource->OnPackageEntriesAdded().RemoveAll(this);
	}

	TSharedRef<SWidget> SPackageTransmissionTable::CreateTableView()
	{
		return SAssignNew(TableView, SListView<TSharedPtr<FPackageTransmissionEntry>>)
			.ListItemsSource(&PackageEntrySource->GetEntries())
			.OnGenerateRow(this, &SPackageTransmissionTable::OnGenerateActivityRowWidget)
			.SelectionMode(ESelectionMode::Multi)
			.HeaderRow(CreateHeaderRow());
	}

	TSharedRef<SHeaderRow> SPackageTransmissionTable::CreateHeaderRow()
	{
		HeaderRow = SNew(SHeaderRow)
		.OnHiddenColumnsListChanged_Lambda([this]()
		{
			if (!bIsUpdatingColumnVisibility)
			{
				UMultiUserServerColumnVisibilitySettings::GetSettings()->SetPackageTransmissionColumnVisibility(
				UE::ConcertSharedSlate::SnapshotColumnVisibilityState(HeaderRow.ToSharedRef())
				);
			}
		});

		const TSet<FName> CannotHideColumns = { SPackageTransmissionTableRow::TransmissionStateColumn };
		for (FName ColumnName : SPackageTransmissionTableRow::AllColumns)
		{
			const bool bCannotHide = CannotHideColumns.Contains(ColumnName);
			SHeaderRow::FColumn::FArguments Args = SHeaderRow::FColumn::FArguments()
				.ColumnId(ColumnName)
				.HAlignCell(HAlign_Center)
				.DefaultLabel(SPackageTransmissionTableRow::ColumnsDisplayText[ColumnName]);

			if (bCannotHide)
			{
				Args.ShouldGenerateWidget(bCannotHide);
			}
			else
			{
				Args.OnGetMenuContent_Lambda([this]()
				{
					return ConcertSharedSlate::MakeHideColumnContextMenu(HeaderRow.ToSharedRef(), SPackageTransmissionTableRow::TimeColumn);
				});
			}
			
			HeaderRow->AddColumn(Args);
		}

		TGuardValue<bool> DoNotSave(bIsUpdatingColumnVisibility, true);
		RestoreDefaultColumnVisibilities();

		return HeaderRow.ToSharedRef();
	}

	TSharedRef<ITableRow> SPackageTransmissionTable::OnGenerateActivityRowWidget(TSharedPtr<FPackageTransmissionEntry> Item, const TSharedRef<STableViewBase>& OwnerTable) const
	{
		return SNew(SPackageTransmissionTableRow, Item, OwnerTable, Tokenizer.ToSharedRef())
			.HighlightText(HighlightText)
			.CanScrollToLog(CanScrollToLogDelegate)
			.ScrollToLog(ScrollToLogDelegate);
	}

	void SPackageTransmissionTable::OnColumnVisibilitySettingsChanged(const FColumnVisibilitySnapshot& ColumnSnapshot)
	{
		TGuardValue<bool> GuardValue(bIsUpdatingColumnVisibility, true);
		ConcertSharedSlate::RestoreColumnVisibilityState(HeaderRow.ToSharedRef(), ColumnSnapshot);
	}

	void SPackageTransmissionTable::ExtendViewOptions(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("SelectAll", "Show all"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					for(FName ColumnId : SPackageTransmissionTableRow::AllColumns)
					{
						HeaderRow->SetShowGeneratedColumn(ColumnId, true);
					}
				}),
				FCanExecuteAction::CreateLambda([] { return true; })),
			NAME_None,
			EUserInterfaceActionType::Button
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideAll", "Hide all"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					for(FName ColumnId : SPackageTransmissionTableRow::AllColumns)
					{
						HeaderRow->SetShowGeneratedColumn(ColumnId, false);
					}
				}),
				FCanExecuteAction::CreateLambda([] { return true; })),
			NAME_None,
			EUserInterfaceActionType::Button
		);
		MenuBuilder.AddMenuEntry(
				LOCTEXT("RestoreDefaultColumnVisibility", "Restore columns visibility"),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SPackageTransmissionTable::RestoreDefaultColumnVisibilities),
					FCanExecuteAction::CreateLambda([] { return true; })),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		MenuBuilder.AddSeparator();
		ConcertSharedSlate::AddEntriesForShowingHiddenRows(HeaderRow.ToSharedRef(), MenuBuilder);
	}

	void SPackageTransmissionTable::RestoreDefaultColumnVisibilities()
	{
		const TSet<FName> HiddenByDefault = {
			SPackageTransmissionTableRow::OriginColumn,
			SPackageTransmissionTableRow::DestinationColumn,
			SPackageTransmissionTableRow::PackagePathColumn
		};
		for (FName ColumnName : SPackageTransmissionTableRow::AllColumns)
		{
			HeaderRow->SetShowGeneratedColumn(ColumnName, !HiddenByDefault.Contains(ColumnName));
		}
	}

	void SPackageTransmissionTable::OnPackageEntriesModified(const TSet<FPackageTransmissionId>& Set) const
	{
		TableView->RequestListRefresh();
	}

	void SPackageTransmissionTable::OnPackageArrayChanged(uint32 NumAdded) const
	{
		TableView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE