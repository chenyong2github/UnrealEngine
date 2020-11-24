// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterRow.h"

#include "Widgets/SLevelSnapshotsEditorFilterList.h"
#include "Views/Filter/SLevelSnapshotsEditorFilters.h"
#include "Views/Filter/LevelSnapshotsEditorFilterClass.h"

#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorStyle.h"

#include "EditorStyleSet.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"

void SLevelSnapshotsEditorFilterRow::Construct(const FArguments& InArgs, const TSharedRef<SLevelSnapshotsEditorFilters>& InEditorFilters, const TSharedRef<FLevelSnapshotsEditorFilterRowGroup>& InFieldGroup)
{
	EditorFiltersPtr = InEditorFilters;
	FieldGroupPtr = InFieldGroup;

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
						//.Visibility_Raw(this, &SFieldGroup::GetVisibilityAccordingToEditMode)
						.OnClicked(this, &SLevelSnapshotsEditorFilterRow::RemoveFilter)
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
					SAssignNew(FilterList, SLevelSnapshotsEditorFilterList, InEditorFilters->GetFiltersModel())
				]
			]
		];

	// Restore all filters from Object
	{
		if (ULevelSnapshotEditorFilterGroup* FilterGroup = InFieldGroup->GetFilterGroupObject())
		{
			for (const TPair<FName, ULevelSnapshotFilter*>& FilterGroupsPair : FilterGroup->Filters)
			{
				FilterList->AddFilter(FilterGroupsPair.Key, FilterGroupsPair.Value);
			}
		}
	}
}

FReply SLevelSnapshotsEditorFilterRow::RemoveFilter()
{
	TSharedPtr<SLevelSnapshotsEditorFilters> EditorFilters = EditorFiltersPtr.Pin();
	check(EditorFilters.IsValid());

	TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> FieldGroup = FieldGroupPtr.Pin();
	check(FieldGroup.IsValid());
	
	EditorFilters->RemoveFilterRow(FieldGroup);

	return FReply::Handled();
}
