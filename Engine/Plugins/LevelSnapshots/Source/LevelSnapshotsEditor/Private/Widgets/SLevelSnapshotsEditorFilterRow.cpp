// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterRow.h"

#include "Widgets/SLevelSnapshotsEditorFilterList.h"
#include "Views/Filter/SLevelSnapshotsEditorFilters.h"
#include "Views/Filter/LevelSnapshotsEditorFilterClass.h"

#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorStyle.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorFilterRow::~SLevelSnapshotsEditorFilterRow()
{
}

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

					// Filter
					+ SHorizontalBox::Slot()
					.Padding(0.f, 0.f)
					.AutoWidth()
					[
						SNew( SComboButton )
						.ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
						.ForegroundColor(FLinearColor::White)
						.ContentPadding(0)
						.ToolTipText( LOCTEXT( "AddFilterToolTip", "Add an asset filter." ) )
						.OnGetMenuContent( this, &SLevelSnapshotsEditorFilterRow::MakeAddFilterMenu )
						.HasDownArrow( true )
						.ContentPadding( FMargin( 1, 0 ) )
						.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ContentBrowserFiltersCombo")))
						.Visibility(EVisibility::Visible)
						.ButtonContent()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
								.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
								.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
								.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
								.Text(LOCTEXT("Filters", "Filters"))
							]
						]
					]

					// Search
					+SHorizontalBox::Slot()
					.FillWidth(1)
					.Padding(5.f, 0.f)
					[
						SAssignNew(SearchBox, SSearchBox)
					]

					// Remove Button
					+ SHorizontalBox::Slot()
					.Padding(0.f, 0.f)
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

TSharedRef<SWidget> SLevelSnapshotsEditorFilterRow::MakeAddFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/true);

	MenuBuilder.BeginSection("ContentBrowserResetFilters");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("FilterListResetFilters", "Reset Filters"),
			LOCTEXT("FilterListResetToolTip", "Resets current filter selection"),
			FSlateIcon(),
			FUIAction()
			);
	}
	MenuBuilder.EndSection();

	TSharedPtr<SLevelSnapshotsEditorFilters> EditorFilters = EditorFiltersPtr.Pin();
	check(EditorFilters.IsValid());

	const TMap<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>& FilterClasses = EditorFilters->GetFilterClasses();

	// Add Basic Filters
	TArray<FText> BasicFilters;

	// TODO. Add Basic Filters
	TArray<TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>> BlueprintFilters;
	TArray<TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>> CPPFilters;

	MenuBuilder.BeginSection("BasicFilters");
	{
		for (const TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>> FilterClassPair : FilterClasses)
		{
			if (FilterClassPair.Value->IsBasicClass())
			{
				TSubclassOf<ULevelSnapshotFilter> Class = FilterClassPair.Key;

				MenuBuilder.AddMenuEntry(
						FText::FromName(FilterClassPair.Key->GetFName()),
						FText::FromName(FilterClassPair.Key->GetFName()),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorFilterRow::OnAddFilter, Class, FilterClassPair.Key->GetFName())),
						NAME_None,
						EUserInterfaceActionType::ToggleButton
					);
			}
			else if (FilterClassPair.Value->IsBlueprintClass())
			{
				BlueprintFilters.Add(TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>(FilterClassPair.Key, FilterClassPair.Value));
			}
			else
			{
				CPPFilters.Add(TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>(FilterClassPair.Key, FilterClassPair.Value));
			}
		}
	}
	MenuBuilder.EndSection();

	// TODO. Advanced Filters
	MenuBuilder.BeginSection("AdvancedFilters");
		{
			FText ParentFilter = FText::FromString("Blueprints");

			MenuBuilder.AddSubMenu(
				TAttribute<FText>(ParentFilter),
				TAttribute<FText>(FText::Format(LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), ParentFilter)),
				FNewMenuDelegate::CreateSP(this, &SLevelSnapshotsEditorFilterRow::CreateFiltersMenuCategory, BlueprintFilters), // Use movetemp here
				FUIAction(
					FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorFilterRow::FilterByTypeCategoryClicked, ParentFilter),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SLevelSnapshotsEditorFilterRow::IsFilterTypeCategoryInUse, ParentFilter)
					),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}

		{
			FText ParentFilter = FText::FromString("CPP");

			MenuBuilder.AddSubMenu(
				TAttribute<FText>(ParentFilter),
				TAttribute<FText>(FText::Format(LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), ParentFilter)),
				FNewMenuDelegate::CreateSP(this, &SLevelSnapshotsEditorFilterRow::CreateFiltersMenuCategory, CPPFilters), // Use movetemp here
				FUIAction(
					FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorFilterRow::FilterByTypeCategoryClicked, ParentFilter),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SLevelSnapshotsEditorFilterRow::IsFilterTypeCategoryInUse, ParentFilter)
					),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
				);
		}
	MenuBuilder.EndSection();

	return
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		[
			MenuBuilder.MakeWidget()
		];
}

void SLevelSnapshotsEditorFilterRow::OnAddFilter(TSubclassOf<ULevelSnapshotFilter> InClass, FName InName)
{
	TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> FieldGroup = FieldGroupPtr.Pin();
	check(FieldGroup.IsValid());

	ULevelSnapshotEditorFilterGroup* FilterGroupObject = FieldGroup->GetFilterGroupObject();
	if (FilterGroupObject != nullptr)
	{
		ULevelSnapshotFilter* SnapshotFilter = FilterGroupObject->AddOrFindFilter(InClass, InName);

		FilterList->AddFilter(InName, SnapshotFilter);
	}
}

void SLevelSnapshotsEditorFilterRow::CreateFiltersMenuCategory(FMenuBuilder& MenuBuilder, TArray<TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>> Filters)
{
	for (const TPair<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>> FilterPair : Filters)
	{
		TSubclassOf<ULevelSnapshotFilter> Class = FilterPair.Key;

		MenuBuilder.AddMenuEntry(
			FText::FromName(FilterPair.Key->GetFName()),
			FText::FromName(FilterPair.Key->GetFName()),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorFilterRow::OnAddFilter, Class, FilterPair.Key->GetFName())), // Move Temp ?
			NAME_None,
			EUserInterfaceActionType::ToggleButton
			);
	}
}

void SLevelSnapshotsEditorFilterRow::FilterByTypeCategoryClicked(FText ParentCategory)
{
}

bool SLevelSnapshotsEditorFilterRow::IsFilterTypeCategoryInUse(FText ParentCategory)
{
	return false;
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

#undef LOCTEXT_NAMESPACE
