// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Filter/SLevelSnapshotsEditorFilters.h"

#include "Widgets/SLevelSnapshotsEditorFilterRow.h"

#include "EditorStyleSet.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorFilters::~SLevelSnapshotsEditorFilters()
{
}

void SLevelSnapshotsEditorFilters::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SAssignNew(FilterRowsList, STreeView<TSharedPtr<FLevelSnapshotsEditorFilterRow>>)
				.TreeItemsSource(reinterpret_cast<TArray<TSharedPtr<FLevelSnapshotsEditorFilterRow>>*>(&FilterRowGroups))
				.ItemHeight(24.0f)
				.OnGenerateRow(this, &SLevelSnapshotsEditorFilters::OnGenerateRow)
				.OnGetChildren(this, &SLevelSnapshotsEditorFilters::OnGetGroupChildren)
				.OnSelectionChanged(this, &SLevelSnapshotsEditorFilters::OnSelectionChanged)
				.ClearSelectionOnClick(false)
			]
			// Add button
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0, 0, 0)
					.HAlign(HAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "RoundButton")
						.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
						.ContentPadding(FMargin(2, 0))
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(FMargin(0, 1))
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("Plus"))
							]
						]
					]
				]
		];

	Refresh();
}

TSharedRef<ITableRow> SLevelSnapshotsEditorFilters::OnGenerateRow(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InFilterRow->GetType() == FLevelSnapshotsEditorFilterRow::Group)
	{
		return SNew(SLevelSnapshotsEditorFilterRowGroup, OwnerTable, InFilterRow->AsGroup(), SharedThis<SLevelSnapshotsEditorFilters>(this));
	}
	
	return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
		[
			SNew(STextBlock).Text(LOCTEXT("ErrorRowGeneration", "ErrorRowGeneration"))
		];
}

void SLevelSnapshotsEditorFilters::OnGetGroupChildren(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, TArray<TSharedPtr<FLevelSnapshotsEditorFilterRow>>& OutRows)
{
	if (InFilterRow.IsValid())
	{
		InFilterRow->GetNodeChildren(OutRows);
	}
}

void SLevelSnapshotsEditorFilters::OnSelectionChanged(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, ESelectInfo::Type SelectInfo)
{
}

void SLevelSnapshotsEditorFilters::Refresh()
{
	RefreshGroups();
}

void SLevelSnapshotsEditorFilters::RefreshGroups()
{
	// TODO. Add test groups

	FilterRowGroups.Empty();

	{
		TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> NewGroup = MakeShared<FLevelSnapshotsEditorFilterRowGroup>(TEXT("First"), SharedThis(this));
		FilterRowGroups.Add(NewGroup);
	}
	{
		TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> NewGroup = MakeShared<FLevelSnapshotsEditorFilterRowGroup>(TEXT("Second"), SharedThis(this));
		FilterRowGroups.Add(NewGroup);
	}
	{
		TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> NewGroup = MakeShared<FLevelSnapshotsEditorFilterRowGroup>(TEXT("Third"), SharedThis(this));
		FilterRowGroups.Add(NewGroup);
	}
}

void SLevelSnapshotsEditorFilterRowGroup::Tick(const FGeometry&, const double, const float)
{
}

void SLevelSnapshotsEditorFilterRowGroup::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FLevelSnapshotsEditorFilterRowGroup>& FieldGroup, const TSharedPtr<SLevelSnapshotsEditorFilters>& OwnerPanel)
{
	ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(8.0f, 8.0f))
			[
				SNew(SLevelSnapshotsEditorFilterRow)
			]
		];

	STableRow<TSharedPtr<FLevelSnapshotsEditorFilterRowGroup>>::ConstructInternal(
		STableRow::FArguments()
		.ShowSelection(false),
		InOwnerTableView
	);
}

TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> FLevelSnapshotsEditorFilterRowGroup::AsGroup()
{
	return SharedThis(this);
}

#undef LOCTEXT_NAMESPACE
