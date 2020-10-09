// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterRow.h"

#include "Widgets/SLevelSnapshotsEditorFilterList.h"

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

void SLevelSnapshotsEditorFilterRow::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
			[
				SNew(SVerticalBox)

				// Search and commands
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 2)
				[
					SNew(SHorizontalBox)

					// Filter
					+ SHorizontalBox::Slot()
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
							.Padding(2,0,0,0)
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
					[
						SAssignNew(SearchBox, SSearchBox)
					]
				]

				// Filters
				+ SVerticalBox::Slot()
				.Padding(5.f, 5.f)
				.AutoHeight()
				[
					SNew(SLevelSnapshotsEditorFilterList)
				]
			]
		];
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
			FUIAction(FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorFilterRow::OnResetFilters))
			);
	}
	MenuBuilder.EndSection();

	// Add Basic Filters
	TArray<FText> BasicFilters;

	BasicFilters.Add(LOCTEXT("Location", "Location"));
	BasicFilters.Add(LOCTEXT("Rotation", "Rotation"));
	BasicFilters.Add(LOCTEXT("Scale", "Scale"));
	
	MenuBuilder.BeginSection("BasicFilters");
	{
		for (const FText& FilterText : BasicFilters)
		{
			MenuBuilder.AddMenuEntry(
				FilterText,
				FText::Format(LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), FilterText),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorFilterRow::OnResetFilters))
				);
		}
	}
	MenuBuilder.EndSection();

	// TODO. Add Basic Filters
	TMap<FString, TArray<FText>> AdvancedFilters;

	AdvancedFilters.Add("Blueprint", 
		{
			{ LOCTEXT("MyCustomBlueprintFilter1", "MyCustomBlueprintFilter1") },
			{ LOCTEXT("MyCustomBlueprintFilter2", "MyCustomBlueprintFilter2") },
			{ LOCTEXT("MyCustomBlueprintFilter2", "MyCustomBlueprintFilter2") }
		});

	AdvancedFilters.Add("CPP",
		{
			{ LOCTEXT("MyCustomCPPFilter1", "MyCustomCPPFilter1") },
			{ LOCTEXT("MyCustomCPPFilter2", "MyCustomCPPFilter2") },
			{ LOCTEXT("MyCustomCPPFilter2", "MyCustomCPPFilter2") }
		});

	// TODO. Advanced Filters
	MenuBuilder.BeginSection("AdvancedFilters");
		for (const TPair<FString, TArray<FText>>& AdvancedFilter : AdvancedFilters)
		{
			FText ParentFilter = FText::FromString(AdvancedFilter.Key);

			MenuBuilder.AddSubMenu(
				TAttribute<FText>(ParentFilter),
				TAttribute<FText>(FText::Format(LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), ParentFilter)),
				FNewMenuDelegate::CreateSP(this, &SLevelSnapshotsEditorFilterRow::CreateFiltersMenuCategory, AdvancedFilter.Value),
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

void SLevelSnapshotsEditorFilterRow::OnResetFilters()
{
}

void SLevelSnapshotsEditorFilterRow::CreateFiltersMenuCategory(FMenuBuilder& MenuBuilder, TArray<FText> AdvancedFilter)
{
	for (const FText& FilterText : AdvancedFilter)
	{
		MenuBuilder.AddMenuEntry(
			FilterText,
			FText::Format(LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), FilterText),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorFilterRow::OnResetFilters))
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

#undef LOCTEXT_NAMESPACE
