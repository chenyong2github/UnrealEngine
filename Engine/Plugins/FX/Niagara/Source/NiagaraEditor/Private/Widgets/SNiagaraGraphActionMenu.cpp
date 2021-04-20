// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraGraphActionMenu.h"
#include "EdGraphSchema_Niagara.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "NiagaraActions.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorUtilities.h"
#include "SNiagaraGraphActionWidget.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SItemSelector.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SNiagaraScriptSourceFilter.h"

#define LOCTEXT_NAMESPACE "NiagaraGraphActionMenu"

bool SNiagaraGraphActionMenu::bLibraryOnly = true;

SNiagaraGraphActionMenu::~SNiagaraGraphActionMenu()
{
	OnClosedCallback.ExecuteIfBound();
}

void SNiagaraGraphActionMenu::Construct( const FArguments& InArgs )
{
	this->GraphObj = InArgs._GraphObj;
	this->DraggedFromPins = InArgs._DraggedFromPins;
	this->NewNodePosition = InArgs._NewNodePosition;
	this->OnClosedCallback = InArgs._OnClosedCallback;
	this->AutoExpandActionMenu = InArgs._AutoExpandActionMenu;

	SAssignNew(FilterBox, SNiagaraSourceFilterBox)
    .OnFiltersChanged(this, &SNiagaraGraphActionMenu::TriggerRefresh);
	
	// Build the widget layout
	SBorder::Construct( SBorder::FArguments()
		.BorderImage( FEditorStyle::GetBrush("Menu.Background") )
		.Padding(5)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
	        .Padding(1.0f)
	        .AutoHeight()
	        [
	            SAssignNew(LibraryOnlyToggle, SNiagaraLibraryOnlyToggleHeader)
	            .HeaderLabelText(LOCTEXT("LibraryOnlyTitle", "Edit value"))
	            .LibraryOnly(this, &SNiagaraGraphActionMenu::GetLibraryOnly)
	            .LibraryOnlyChanged(this, &SNiagaraGraphActionMenu::SetLibraryOnly)
	        ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                FilterBox.ToSharedRef()
            ]
            + SVerticalBox::Slot()
            [   
				// Achieving fixed width by nesting items within a fixed width box.
				SNew(SBox)
				.WidthOverride(450)
				.HeightOverride(400)
				[
					SAssignNew(ActionSelector, SNiagaraMenuActionSelector)
					.Items(CollectAllActions())
					.OnGetCategoriesForItem(this, &SNiagaraGraphActionMenu::OnGetCategoriesForItem)
					.OnGetSectionsForItem(this, &SNiagaraGraphActionMenu::OnGetSectionsForItem)
					.OnCompareSectionsForEquality(this, &SNiagaraGraphActionMenu::OnCompareSectionsForEquality)
					.OnCompareSectionsForSorting(this, &SNiagaraGraphActionMenu::OnCompareSectionsForSorting)
					.OnCompareCategoriesForEquality(this, &SNiagaraGraphActionMenu::OnCompareCategoriesForEquality)
					.OnCompareCategoriesForSorting(this, &SNiagaraGraphActionMenu::OnCompareCategoriesForSorting)
					.OnCompareItemsForSorting(this, &SNiagaraGraphActionMenu::OnCompareItemsForSorting)
					.OnDoesItemMatchFilterText_Lambda([](const FText& FilterText, const TSharedPtr<FNiagaraAction_NewNode>& Item)
					{
                        return FNiagaraEditorUtilities::DoesItemMatchFilterText(FilterText, Item);              
					})
					.OnGenerateWidgetForSection(this, &SNiagaraGraphActionMenu::OnGenerateWidgetForSection)
					.OnGenerateWidgetForCategory(this, &SNiagaraGraphActionMenu::OnGenerateWidgetForCategory)
					.OnGenerateWidgetForItem(this, &SNiagaraGraphActionMenu::OnGenerateWidgetForItem)
					.OnGetItemWeight_Lambda([](const TSharedPtr<FNiagaraAction_NewNode>& InCurrentAction, const TArray<FString>& InFilterTerms)
					{
						return FNiagaraEditorUtilities::GetWeightForItem(InCurrentAction, InFilterTerms);
					})
					.OnItemActivated(this, &SNiagaraGraphActionMenu::OnItemActivated)
					.AllowMultiselect(false)
					.OnDoesItemPassCustomFilter(this, &SNiagaraGraphActionMenu::DoesItemPassCustomFilter)
					.ClickActivateMode(EItemSelectorClickActivateMode::SingleClick)
					.ExpandInitially(false)
					.OnGetSectionData_Lambda([](const ENiagaraMenuSections& Section)
					{
						if(Section == ENiagaraMenuSections::Suggested)
						{
                            return SNiagaraMenuActionSelector::FSectionData(SNiagaraMenuActionSelector::FSectionData::List, true);
                        }

                        return SNiagaraMenuActionSelector::FSectionData(SNiagaraMenuActionSelector::FSectionData::Tree, false);
					})
				]
			]
		]
	);
}


TArray<TSharedPtr<FNiagaraAction_NewNode>> SNiagaraGraphActionMenu::CollectAllActions()
{
	OwnerOfTemporaries =  NewObject<UEdGraph>((UObject*)GetTransientPackage());
	
	const UEdGraphSchema_Niagara* Schema = Cast<UEdGraphSchema_Niagara>(GraphObj->GetSchema());

	TArray<TSharedPtr<FNiagaraAction_NewNode>> Actions = Schema->GetGraphActions(GraphObj, DraggedFromPins.Num() > 0 ? DraggedFromPins[0] : nullptr, OwnerOfTemporaries);
	return Actions;
}

TSharedRef<SWidget> SNiagaraGraphActionMenu::GetFilterTextBox()
{
	return ActionSelector->GetSearchBox();
}

TArray<FString> SNiagaraGraphActionMenu::OnGetCategoriesForItem(const TSharedPtr<FNiagaraAction_NewNode>& Item)
{	
	return Item->Categories;
}

TArray<ENiagaraMenuSections> SNiagaraGraphActionMenu::OnGetSectionsForItem(const TSharedPtr<FNiagaraAction_NewNode>& Item)
{
	TArray<ENiagaraMenuSections> Sections;
	if(ActionSelector->IsSearching())
	{
		if(Item->Section == ENiagaraMenuSections::Suggested)
		{
			return { ENiagaraMenuSections::General, ENiagaraMenuSections::Suggested };
		}
		
		return {Item->Section};
	}

	return { ENiagaraMenuSections::General };
}

bool SNiagaraGraphActionMenu::OnCompareSectionsForEquality(const ENiagaraMenuSections& SectionA, const ENiagaraMenuSections& SectionB)
{
	return SectionA == SectionB;
}

bool SNiagaraGraphActionMenu::OnCompareSectionsForSorting(const ENiagaraMenuSections& SectionA, const ENiagaraMenuSections& SectionB)
{
	return SectionA < SectionB;
}

bool SNiagaraGraphActionMenu::OnCompareCategoriesForEquality(const FString& CategoryA, const FString& CategoryB)
{
	return CategoryA.Compare(CategoryB) == 0;
}

bool SNiagaraGraphActionMenu::OnCompareCategoriesForSorting(const FString& CategoryA,
	const FString& CategoryB)
{
	return CategoryA.Compare(CategoryB) == -1;
}

bool SNiagaraGraphActionMenu::OnCompareItemsForEquality(const TSharedPtr<FNiagaraAction_NewNode>& ItemA, const TSharedPtr<FNiagaraAction_NewNode>& ItemB)
{
	return ItemA->DisplayName.EqualTo(ItemB->DisplayName);
}

bool SNiagaraGraphActionMenu::OnCompareItemsForSorting(const TSharedPtr<FNiagaraAction_NewNode>& ItemA, const TSharedPtr<FNiagaraAction_NewNode>& ItemB)
{
	return ItemA->DisplayName.CompareTo(ItemB->DisplayName) == -1;
}

TSharedRef<SWidget> SNiagaraGraphActionMenu::OnGenerateWidgetForSection(const ENiagaraMenuSections& Section)
{
	UEnum* SectionEnum = StaticEnum<ENiagaraMenuSections>();
	FText TextContent = SectionEnum->GetDisplayNameTextByValue((int64) Section);
	
	return SNew(STextBlock)
		.Text(TextContent)
		.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerAssetCategoryText");
}

TSharedRef<SWidget> SNiagaraGraphActionMenu::OnGenerateWidgetForCategory(const FString& Category)
{
	FText TextContent = FText::FromString(Category);

	return SNew(SRichTextBlock)
		.Text(TextContent)
		.DecoratorStyleSet(&FEditorStyle::Get())
		.TextStyle(FNiagaraEditorStyle::Get(), "ActionMenu.HeadingTextBlock");
}

TSharedRef<SWidget> SNiagaraGraphActionMenu::OnGenerateWidgetForItem(const TSharedPtr<FNiagaraAction_NewNode>& Item)
{
	FCreateNiagaraWidgetForActionData ActionData(Item);
	ActionData.HighlightText = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &SNiagaraGraphActionMenu::GetFilterText));
	return SNew(SNiagaraActionWidget, ActionData);
}

void SNiagaraGraphActionMenu::OnItemActivated(const TSharedPtr<FNiagaraAction_NewNode>& Item)
{
	Item->CreateNode(GraphObj, DraggedFromPins, NewNodePosition);
	FSlateApplication::Get().DismissAllMenus();	
}

void SNiagaraGraphActionMenu::TriggerRefresh(const TMap<EScriptSource, bool>& SourceState)
{
	ActionSelector->RefreshAllItems();

	TArray<bool> States;
	SourceState.GenerateValueArray(States);

	int32 NumActive = 0;
	for(bool& State : States)
	{
		if(State == true)
		{
			NumActive++;
		}
	}

	// whenever we have less than the last (so with 4 valid filters, at most 3) entry of filters, we expand the tree.
	if(NumActive < (int32) EScriptSource::Unknown)
	{
		ActionSelector->ExpandTree();
	}
}

void SNiagaraGraphActionMenu::SetLibraryOnly(bool bInIsLibraryOnly)
{
	bLibraryOnly = bInIsLibraryOnly;
	ActionSelector->RefreshAllItems(true);
}

bool SNiagaraGraphActionMenu::DoesItemPassCustomFilter(const TSharedPtr<FNiagaraAction_NewNode>& Item)
{
	bool bLibraryConditionFulfilled = (bLibraryOnly && Item->bIsInLibrary) || !bLibraryOnly;
	return FilterBox->IsFilterActive(Item->SourceData.Source) && bLibraryConditionFulfilled;
}

#undef LOCTEXT_NAMESPACE
