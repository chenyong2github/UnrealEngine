// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/SListView.h"

class FWidgetBlueprintEditor;
class FWidgetViewModel;
class FPaletteViewModel;

class SPaletteFavoritesView : public SCompoundWidget
{
public:
	~SPaletteFavoritesView();

	SLATE_BEGIN_ARGS(SPaletteFavoritesView){}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor);
	TSharedRef<ITableRow> OnGenerateWidgetTemplateItem(TSharedPtr<FWidgetViewModel> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnFavoriteListUpdated();

private:

	TSharedPtr<SListView<TSharedPtr<FWidgetViewModel>>> WidgetTemplatesView;
	TSharedPtr<FPaletteViewModel> PaletteViewModel;
};
