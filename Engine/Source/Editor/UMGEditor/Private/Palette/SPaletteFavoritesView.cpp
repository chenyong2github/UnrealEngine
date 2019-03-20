// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Palette/SPaletteFavoritesView.h"
#include "Palette/SPaletteView.h"
#include "Palette/SPaletteViewModel.h"

#include "UMGEditorProjectSettings.h"

SPaletteFavoritesView::~SPaletteFavoritesView()
{
	PaletteViewModel->OnFavoritesUpdated.RemoveAll(this);
}

void SPaletteFavoritesView::Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
{
	// Get the list of favorites from the Palette view model.
	PaletteViewModel = InBlueprintEditor->GetPaletteViewModel();	

	SAssignNew(WidgetTemplatesView, SListView<TSharedPtr<FWidgetViewModel>>)
		.ListItemsSource(&(PaletteViewModel->GetFavoritesViewModels()))
		.ItemHeight(1.0f)
		.OnGenerateRow(this, &SPaletteFavoritesView::OnGenerateWidgetTemplateItem)
		.SelectionMode(ESelectionMode::Single);

	ChildSlot
		[
			WidgetTemplatesView.ToSharedRef()
		];

	PaletteViewModel->OnFavoritesUpdated.AddRaw(this, &SPaletteFavoritesView::OnFavoriteListUpdated);
}

void SPaletteFavoritesView::OnFavoriteListUpdated()
{
	WidgetTemplatesView->RequestListRefresh();
}


TSharedRef<ITableRow> SPaletteFavoritesView::OnGenerateWidgetTemplateItem(TSharedPtr<FWidgetViewModel> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return Item->BuildRow(OwnerTable);
}
