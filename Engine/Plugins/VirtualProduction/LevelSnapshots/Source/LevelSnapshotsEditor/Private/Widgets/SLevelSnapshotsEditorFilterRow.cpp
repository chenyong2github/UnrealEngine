// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterRow.h"

#include "ConjunctionFilter.h"
#include "FavoriteFilterDragDrop.h"
#include "LevelSnapshotsEditorStyle.h"
#include "SLevelSnapshotsEditorFilterList.h"
#include "SLevelSnapshotsEditorFilters.h"

#include "EditorStyleSet.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void SLevelSnapshotsEditorFilterRow::Construct(
	const FArguments& InArgs, 
	const TSharedRef<SLevelSnapshotsEditorFilters>& InEditorFilters,
	UConjunctionFilter* InManagedFilter,
	const bool bShouldShowOrInFront
)
{
	EditorFiltersWidgetWeakPtr = InEditorFilters;
	OnClickRemoveRow = InArgs._OnClickRemoveRow;
	ManagedFilterWeakPtr = InManagedFilter;

	TSharedRef<SWidget> FrontOfRow = [bShouldShowOrInFront]()
	{
		if (bShouldShowOrInFront)
		{
			return
				SNew(SBox)
					.WidthOverride(30.f)
				[
					SNew(STextBlock)
	                    .TextStyle( FLevelSnapshotsEditorStyle::Get(), "LevelSnapshotsEditor.FilterRow.Or")
	                    .Text(LOCTEXT("FilterRow.Or", "OR"))
	                    .WrapTextAt(128.0f)
				];
		}
		return
			SNew(SBox)
				.WidthOverride(30.f);
	}();
	
	ChildSlot
		[
			SNew(SHorizontalBox)

			// OR in front of row
			+ SHorizontalBox::Slot()
			.Padding(7.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				FrontOfRow
			]

			// Row
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.FillWidth(1.f)
			[
				SNew(SBorder)
				.Padding(FMargin(5.0f, 5.f))
				.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.GroupBorder"))
				[
					SNew(SHorizontalBox)

					// Filters
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.Padding(5.f, 5.f)
					.FillWidth(1.f)
					[
						SAssignNew(FilterList, SLevelSnapshotsEditorFilterList, InManagedFilter, InEditorFilters->GetFiltersModel().ToSharedRef())
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Top)
					.AutoWidth()
					[
						SNew(SHorizontalBox)

						// Remove Button
						+ SHorizontalBox::Slot()
						.Padding(0.f, 0.f)
						.AutoWidth()
						[
							SNew(SButton)
							.OnClicked_Lambda([this]()
							{
								OnClickRemoveRow.ExecuteIfBound(SharedThis(this));
								return FReply::Handled();
							})
							.ButtonStyle(FLevelSnapshotsEditorStyle::Get(), "LevelSnapshotsEditor.RemoveFilterButton")
							[
								SNew(STextBlock)
								.TextStyle(FLevelSnapshotsEditorStyle::Get(), "LevelSnapshotsEditor.Button.TextStyle")
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
								.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
							]
						]
					]
				]
			]			
		];
}

const TWeakObjectPtr<UConjunctionFilter>& SLevelSnapshotsEditorFilterRow::GetManagedFilter()
{
	return ManagedFilterWeakPtr;
}

void SLevelSnapshotsEditorFilterRow::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FFavoriteFilterDragDrop> DragDrop = DragDropEvent.GetOperationAs<FFavoriteFilterDragDrop>())
	{
		DragDrop->OnEnterRow(SharedThis(this));
	}
}

void SLevelSnapshotsEditorFilterRow::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FFavoriteFilterDragDrop> DragDrop = DragDropEvent.GetOperationAs<FFavoriteFilterDragDrop>())
	{
		DragDrop->OnLeaveRow(SharedThis(this));
	}
}

FReply SLevelSnapshotsEditorFilterRow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FFavoriteFilterDragDrop> DragDrop = DragDropEvent.GetOperationAs<FFavoriteFilterDragDrop>())
	{
		const bool bDropResult = DragDrop->OnDropOnRow(SharedThis(this));
		return bDropResult ? FReply::Handled().EndDragDrop() : FReply::Unhandled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE