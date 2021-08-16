// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/SLibraryView.h"
#include "Library/SLibraryViewModel.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBorder.h"

#if WITH_EDITOR
	#include "EditorStyleSet.h"
#endif // WITH_EDITOR



#include "DragDrop/WidgetTemplateDragDropOp.h"

#include "Templates/WidgetTemplateClass.h"
#include "Templates/WidgetTemplateBlueprintClass.h"

#include "AssetRegistryModule.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"

#include "Settings/ContentBrowserSettings.h"
#include "WidgetBlueprintEditorUtils.h"



#include "UMGEditorProjectSettings.h"

#define LOCTEXT_NAMESPACE "UMG"

FText SLibraryViewItem::GetFavoriteToggleToolTipText() const
{
	if (GetFavoritedState() == ECheckBoxState::Checked)
	{
		return LOCTEXT("Unfavorite", "Click to remove this widget from your favorites.");
	}
	return LOCTEXT("Favorite", "Click to add this widget to your favorites.");
}

ECheckBoxState SLibraryViewItem::GetFavoritedState() const
{
	if (WidgetViewModel->IsFavorite())
	{
		return ECheckBoxState::Checked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SLibraryViewItem::OnFavoriteToggled(ECheckBoxState InNewState)
{
	if (InNewState == ECheckBoxState::Checked)
	{
		//Add to favorites
		WidgetViewModel->AddToFavorites();
	}
	else
	{
		//Remove from favorites
		WidgetViewModel->RemoveFromFavorites();
	}
}

EVisibility SLibraryViewItem::GetFavoritedStateVisibility() const
{
	return GetFavoritedState() == ECheckBoxState::Checked || IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
}

void SLibraryViewItem::Construct(const FArguments& InArgs, TSharedPtr<FWidgetTemplateViewModel> InWidgetViewModel)
{
	WidgetViewModel = InWidgetViewModel;

	ChildSlot
		[
			SNew(SHorizontalBox)
			.ToolTip(WidgetViewModel->Template->GetToolTip())
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[	
				SNew(SCheckBox)
				.ToolTipText(this, &SLibraryViewItem::GetFavoriteToggleToolTipText)
				.IsChecked(this, &SLibraryViewItem::GetFavoritedState)
				.OnCheckStateChanged(this, &SLibraryViewItem::OnFavoriteToggled)
				.Style(FEditorStyle::Get(), "UMGEditor.Library.FavoriteToggleStyle")
				.Visibility(this, &SLibraryViewItem::GetFavoritedStateVisibility)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(WidgetViewModel->Template->GetIcon())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InWidgetViewModel->GetName())
				.HighlightText(InArgs._HighlightText)
			]
		];
}

FReply SLibraryViewItem::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	return WidgetViewModel->Template->OnDoubleClicked();
};

void SLibraryView::Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
{
	BlueprintEditor = InBlueprintEditor;

	UBlueprint* BP = InBlueprintEditor->GetBlueprintObj();
	LibraryViewModel = InBlueprintEditor->GetLibraryViewModel();

	// Register to the update of the viewmodel.
	LibraryViewModel->OnUpdating.AddRaw(this, &SLibraryView::OnViewModelUpdating);
	LibraryViewModel->OnUpdated.AddRaw(this, &SLibraryView::OnViewModelUpdated);

	WidgetFilter = MakeShareable(new WidgetViewModelTextFilter(
		WidgetViewModelTextFilter::FItemToStringArray::CreateSP(this, &SLibraryView::GetWidgetFilterStrings)));

	FilterHandler = MakeShareable(new LibraryFilterHandler());
	FilterHandler->SetFilter(WidgetFilter.Get());
	FilterHandler->SetRootItems(&(LibraryViewModel->GetWidgetViewModels()), &TreeWidgetViewModels);
	FilterHandler->SetGetChildrenDelegate(LibraryFilterHandler::FOnGetChildren::CreateRaw(this, &SLibraryView::OnGetChildren));

	SAssignNew(WidgetTemplatesView, STreeView< TSharedPtr<FWidgetViewModel> >)
		.ItemHeight(1.0f)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SLibraryView::OnGenerateWidgetTemplateItem)
		.OnGetChildren(FilterHandler.ToSharedRef(), &LibraryFilterHandler::OnGetFilteredChildren)
		.OnSelectionChanged(this, &SLibraryView::WidgetLibrary_OnSelectionChanged)
		.TreeItemsSource(&TreeWidgetViewModels);
		

	FilterHandler->SetTreeView(WidgetTemplatesView.Get());

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(4)
		.AutoHeight()
		[
			SAssignNew(SearchBoxPtr, SSearchBox)
			.HintText(LOCTEXT("SearchTemplates", "Search Library"))
			.OnTextChanged(this, &SLibraryView::OnSearchChanged)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBorder, WidgetTemplatesView.ToSharedRef())
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
				.Padding(0)
				[
					WidgetTemplatesView.ToSharedRef()
				]
			]
		]
	];

	bRefreshRequested = true;

	LibraryViewModel->Update();
	LoadItemExpansion();
}

SLibraryView::~SLibraryView()
{
	// Unregister to the update of the viewmodel.
	LibraryViewModel->OnUpdating.RemoveAll(this);
	LibraryViewModel->OnUpdated.RemoveAll(this);

	// If the filter is enabled, disable it before saving the expanded items since
	// filtering expands all items by default.
	if (FilterHandler->GetIsEnabled())
	{
		FilterHandler->SetIsEnabled(false);
		FilterHandler->RefreshAndFilterTree();
	}

	SaveItemExpansion();
}

void SLibraryView::OnSearchChanged(const FText& InFilterText)
{
	bRefreshRequested = true;
	FilterHandler->SetIsEnabled(!InFilterText.IsEmpty());
	WidgetFilter->SetRawFilterText(InFilterText);
	SearchBoxPtr->SetError(WidgetFilter->GetFilterErrorText());
	LibraryViewModel->SetSearchText(InFilterText);
}

void SLibraryView::WidgetLibrary_OnSelectionChanged(TSharedPtr<FWidgetViewModel> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (!SelectedItem.IsValid()) 
	{
		return;
	}

	// Reset the selected
	BlueprintEditor.Pin()->SetSelectedTemplate(nullptr);
	BlueprintEditor.Pin()->SetSelectedUserWidget(FAssetData());

	// If it's not a template, return
	if (!SelectedItem->IsTemplate())
	{
		return;
	}

	TSharedPtr<FWidgetTemplateViewModel> SelectedTemplate = StaticCastSharedPtr<FWidgetTemplateViewModel>(SelectedItem);
	if (SelectedTemplate.IsValid())
	{
		TSharedPtr<FWidgetTemplateClass> TemplateClass = StaticCastSharedPtr<FWidgetTemplateClass>(SelectedTemplate->Template);
		if (TemplateClass.IsValid())
		{
			if (TemplateClass->GetWidgetClass().IsValid())
			{
				BlueprintEditor.Pin()->SetSelectedTemplate(TemplateClass->GetWidgetClass());
			}
			else
			{
				TSharedPtr<FWidgetTemplateBlueprintClass> UserCreatedTemplate = StaticCastSharedPtr<FWidgetTemplateBlueprintClass>(TemplateClass);
				if (UserCreatedTemplate.IsValid())
				{
					// Then pass in the asset data of selected widget
					FAssetData UserCreatedWidget = UserCreatedTemplate->GetWidgetAssetData();
					BlueprintEditor.Pin()->SetSelectedUserWidget(UserCreatedWidget);
				}
			}
		}
	}
}

TSharedPtr<FWidgetTemplate> SLibraryView::GetSelectedTemplateWidget() const
{
	TArray<TSharedPtr<FWidgetViewModel>> SelectedTemplates = WidgetTemplatesView.Get()->GetSelectedItems();
	if (SelectedTemplates.Num() == 1)
	{
		TSharedPtr<FWidgetTemplateViewModel> TemplateViewModel = StaticCastSharedPtr<FWidgetTemplateViewModel>(SelectedTemplates[0]);
		if (TemplateViewModel.IsValid())
		{
			return TemplateViewModel->Template;
		}
	}
	return nullptr;
}

void SLibraryView::LoadItemExpansion()
{
	// Restore the expansion state of the widget groups.
	for ( TSharedPtr<FWidgetViewModel>& ViewModel : LibraryViewModel->GetWidgetViewModels())
	{
		bool IsExpanded;
		if ( GConfig->GetBool(TEXT("WidgetTemplatesExpanded"), *ViewModel->GetName().ToString(), IsExpanded, GEditorPerProjectIni) && IsExpanded )
		{
			WidgetTemplatesView->SetItemExpansion(ViewModel, true);
		}
	}
}

void SLibraryView::SaveItemExpansion()
{
	// Restore the expansion state of the widget groups.
	for ( TSharedPtr<FWidgetViewModel>& ViewModel : LibraryViewModel->GetWidgetViewModels() )
	{
		const bool IsExpanded = WidgetTemplatesView->IsItemExpanded(ViewModel);
		GConfig->SetBool(TEXT("WidgetTemplatesExpanded"), *ViewModel->GetName().ToString(), IsExpanded, GEditorPerProjectIni);
	}
}

void SLibraryView::OnGetChildren(TSharedPtr<FWidgetViewModel> Item, TArray< TSharedPtr<FWidgetViewModel> >& Children)
{
	return Item->GetChildren(Children);
}

TSharedRef<ITableRow> SLibraryView::OnGenerateWidgetTemplateItem(TSharedPtr<FWidgetViewModel> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return Item->BuildRow(OwnerTable);
}

void SLibraryView::OnViewModelUpdating()
{
	// Save the old expanded items temporarily
	WidgetTemplatesView->GetExpandedItems(ExpandedItems);
}

void SLibraryView::OnViewModelUpdated()
{
	bRefreshRequested = true;

	// Restore the expansion state
	for (TSharedPtr<FWidgetViewModel>& ExpandedItem : ExpandedItems)
	{
		for (TSharedPtr<FWidgetViewModel>& ViewModel : LibraryViewModel->GetWidgetViewModels())
		{
			if (ViewModel->GetName().EqualTo(ExpandedItem->GetName()) || ViewModel->ShouldForceExpansion())
			{
				WidgetTemplatesView->SetItemExpansion(ViewModel, true);
			}
		}
	}
	ExpandedItems.Reset();
}

void SLibraryView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshRequested)
	{
		bRefreshRequested = false;
		FilterHandler->RefreshAndFilterTree();
	}
}

void SLibraryView::GetWidgetFilterStrings(TSharedPtr<FWidgetViewModel> WidgetViewModel, TArray<FString>& OutStrings)
{
	WidgetViewModel->GetFilterStrings(OutStrings);
}

#undef LOCTEXT_NAMESPACE
