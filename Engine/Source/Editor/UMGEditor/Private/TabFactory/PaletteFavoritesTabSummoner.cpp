// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TabFactory/PaletteFavoritesTabSummoner.h"
#include "Palette/SPaletteFavoritesView.h"
#include "UMGStyle.h"

#define LOCTEXT_NAMESPACE "UMG"

const FName FPaletteFavoritesTabSummoner::TabID(TEXT("WidgetTemplatesFavorites"));

FPaletteFavoritesTabSummoner::FPaletteFavoritesTabSummoner(TSharedPtr<class FWidgetBlueprintEditor> InBlueprintEditor)
		: FWorkflowTabFactory(TabID, InBlueprintEditor)
		, BlueprintEditor(InBlueprintEditor)
{
	TabLabel = LOCTEXT("WidgetTemplatesFavoritesTabLabel", "Palette Favorites");
	TabIcon = FSlateIcon(FUMGStyle::GetStyleSetName(), "Palette.TabIcon");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("WidgetTemplatesFavorites_ViewMenu_Desc", "Palette Favorites");
	ViewMenuTooltip = LOCTEXT("WidgetTemplatesFavorites_ViewMenu_ToolTip", "Show the Palette Favorites");
}

TSharedRef<SWidget> FPaletteFavoritesTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FWidgetBlueprintEditor> BlueprintEditorPtr = StaticCastSharedPtr<FWidgetBlueprintEditor>(BlueprintEditor.Pin());

	return SNew(SPaletteFavoritesView, BlueprintEditorPtr);
		
}

#undef LOCTEXT_NAMESPACE 
