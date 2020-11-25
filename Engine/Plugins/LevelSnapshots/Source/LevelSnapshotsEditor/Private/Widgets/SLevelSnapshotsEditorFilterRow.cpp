// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterRow.h"

#include "ConjunctionFilter.h"
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
	
	ChildSlot
		[
			SNew(SBorder)
			.Padding(FMargin(5.0f, 5.f))
			.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.GroupBorder"))
			[
				SNew(SVerticalBox)

				// Search and commands
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					// Remove Button
					+ SHorizontalBox::Slot()
					.Padding(0.f, 0.f)
					.HAlign(HAlign_Right)
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

				// Filters
				+ SVerticalBox::Slot()
				.Padding(5.f, 5.f)
				.AutoHeight()
				[
					SAssignNew(FilterList, SLevelSnapshotsEditorFilterList, InManagedFilter, InEditorFilters->GetFiltersModel())
				]
			]
		];
}

const TWeakObjectPtr<UConjunctionFilter>& SLevelSnapshotsEditorFilterRow::GetManagedFilter()
{
	return ManagedFilterWeakPtr;
}
