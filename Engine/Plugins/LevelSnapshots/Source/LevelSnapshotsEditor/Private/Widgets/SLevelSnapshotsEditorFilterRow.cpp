// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterRow.h"

#include "ConjunctionFilter.h"
#include "FavoriteFilterDragDrop.h"
#include "SLevelSnapshotsEditorFilterList.h"
#include "SLevelSnapshotsEditorFilters.h"

#include "LevelSnapshotsEditorStyle.h"

#include "EditorStyleSet.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

void SLevelSnapshotsEditorFilterRow::Construct(
	const FArguments& InArgs, 
	const TSharedRef<SLevelSnapshotsEditorFilters>& InEditorFilters,
	UConjunctionFilter* InManagedFilter
)
{
	OnClickRemoveRow = InArgs._OnClickRemoveRow;
	ManagedFilterWeakPtr = InManagedFilter;
	
	ChildSlot
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
					SAssignNew(FilterList, SLevelSnapshotsEditorFilterList, InManagedFilter, InEditorFilters->GetFiltersModel())
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
						.OnClicked(FOnClicked::CreateLambda([this]()
						{
							OnClickRemoveRow.ExecuteIfBound(SharedThis(this)); ;
							return FReply::Handled();
						}))
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
