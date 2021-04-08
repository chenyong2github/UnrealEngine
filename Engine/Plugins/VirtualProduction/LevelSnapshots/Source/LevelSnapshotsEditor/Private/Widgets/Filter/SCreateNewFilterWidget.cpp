// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCreateNewFilterWidget.h"

#include "ConjunctionFilter.h"
#include "EditorStyleSet.h"

#include "FavoriteFilterContainer.h"
#include "SFilterSearchMenu.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void SCreateNewFilterWidget::Construct(const FArguments& InArgs, UFavoriteFilterContainer* InAvailableFilters, UConjunctionFilter* InFilterToAddTo)
{
	if (!ensure(InAvailableFilters && InFilterToAddTo))
	{
		return;
	}
	
	AvailableFilters = InAvailableFilters;
	FilterToAddTo = InFilterToAddTo;

	ChildSlot
	[
		SNew(SBorder)
		.Padding(2.f)
		.BorderBackgroundColor( FLinearColor(0.2f, 0.2f, 0.2f, 0.2f) )
		.BorderImage(FEditorStyle::GetBrush("ContentBrowser.FilterButtonBorder"))
		[
			SAssignNew(ComboButton, SComboButton)
	        .ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
	        .ForegroundColor(FLinearColor::White)
	        .ContentPadding(0)
	        .ToolTipText( LOCTEXT( "SelectFilterToUseToolTip", "Select filters you want to use." ) )
	        .OnGetMenuContent(FOnGetContent::CreateLambda([this, InAvailableFilters, InFilterToAddTo]()
			{
	        	const TSharedRef<SFilterSearchMenu> Result = SNew(SFilterSearchMenu, InAvailableFilters)
	        		.OnSelectFilter_Lambda([this, InFilterToAddTo](const TSubclassOf<ULevelSnapshotFilter>& SelectedFilter)
	        		{
	        			InFilterToAddTo->CreateChild(SelectedFilter);
	        		});
	        	ComboButton->SetMenuContentWidgetToFocus(Result->GetSearchBox());
	        	return Result;
	        }))
	        .HasDownArrow( true )
	        .ContentPadding( FMargin( 1, 0 ) )
	        .Visibility(EVisibility::Visible)
	        .ButtonContent()
	        [
	            SNew(STextBlock)
	               .Text(LOCTEXT("AddFilter", "Add filter"))
	        ]
		]
		
	];
}

#undef LOCTEXT_NAMESPACE