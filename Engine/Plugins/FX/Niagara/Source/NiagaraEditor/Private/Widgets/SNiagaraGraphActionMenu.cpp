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
					.OnDoesItemMatchFilterText(this, &SNiagaraGraphActionMenu::OnDoesItemMatchFilterText)
					.OnGenerateWidgetForSection(this, &SNiagaraGraphActionMenu::OnGenerateWidgetForSection)
					.OnGenerateWidgetForCategory(this, &SNiagaraGraphActionMenu::OnGenerateWidgetForCategory)
					.OnGenerateWidgetForItem(this, &SNiagaraGraphActionMenu::OnGenerateWidgetForItem)
					.OnGetItemWeightForSelection(this, &SNiagaraGraphActionMenu::OnGetItemWeightForSelection)
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

bool SNiagaraGraphActionMenu::OnDoesItemMatchFilterText(const FText& FilterText, const TSharedPtr<FNiagaraAction_NewNode>& Item)
{
	if(Item->DisplayName.ToString().Contains(FilterText.ToString()))
	{
		return true;
	}
	if(Item->ToolTip.ToString().Contains(FilterText.ToString()))
	{
		return true;
	}
	if(Item->Keywords.ToString().Contains(FilterText.ToString()))
	{
		return true;
	}
	
	for(const FString& Category : Item->Categories)
	{
		if(Category.Contains(FilterText.ToString()))
		{
			return true;
		}
	}

	return false;
}

int32 SNiagaraGraphActionMenu::OnGetItemWeightForSelection(const TSharedPtr<FNiagaraAction_NewNode>& InCurrentAction, const TArray<FString>& InFilterTerms, const TArray<FString>& InSanitizedFilterTerms) const
{
	// The overall 'weight'
	int32 TotalWeight = 0;

	// Some simple weight figures to help find the most appropriate match
	const int32 WholeWordMultiplier = 3;
	const int32 WholeMatchWeightMultiplier = 2;
	const int32 WholeMatchLocalizedWeightMultiplier = 3;
	const int32 DescriptionWeight = 10;
	const int32 CategoryWeight = 1;
	const int32 NodeTitleWeight = 1;
	const int32 KeywordWeight = 4;
	
	// Helper array
	struct FArrayWithWeight
	{
		FArrayWithWeight(const TArray< FString >* InArray, int32 InWeight)
			: Array(InArray)
			, Weight(InWeight)
		{
		}

		const TArray< FString >* Array;
		int32 Weight;
	};

	// Setup an array of arrays so we can do a weighted search			
	TArray<FArrayWithWeight> WeightedArrayList;

	// Combine the actions string, separate with \n so terms don't run into each other, and remove the spaces (incase the user is searching for a variable)
	// In the case of groups containing multiple actions, they will have been created and added at the same place in the code, using the same description
	// and keywords, so we only need to use the first one for filtering.
	const FString& SearchText = InCurrentAction->FullSearchString;

	// First the localized keywords
	TArray<FString> KeywordsArray = {InCurrentAction->Keywords.ToString()};	
	const int32 NonLocalizedFirstIndex = WeightedArrayList.Add(FArrayWithWeight(&KeywordsArray, KeywordWeight));

	// The localized description
	TArray<FString> TooltipArray = {InCurrentAction->ToolTip.ToString()};
	WeightedArrayList.Add(FArrayWithWeight(&TooltipArray, DescriptionWeight));

	// The localized category
	TArray<FString> CategoryArray = InCurrentAction->Categories;
	WeightedArrayList.Add(FArrayWithWeight(&CategoryArray, CategoryWeight));

	// Now iterate through all the filter terms and calculate a 'weight' using the values and multipliers
	const FString* EachTerm = nullptr;
	const FString* EachTermSanitized = nullptr;
	for (int32 FilterIndex = 0; FilterIndex < InFilterTerms.Num(); ++FilterIndex)
	{
		EachTerm = &InFilterTerms[FilterIndex];
		EachTermSanitized = &InSanitizedFilterTerms[FilterIndex];
		if (SearchText.Contains(*EachTerm, ESearchCase::CaseSensitive))
		{
			TotalWeight += 2;
		}
		else if (SearchText.Contains(*EachTermSanitized, ESearchCase::CaseSensitive))
		{
			TotalWeight++;
		}
		// Now check the weighted lists	(We could further improve the hit weight by checking consecutive word matches)
		for (int32 iFindCount = 0; iFindCount < WeightedArrayList.Num(); iFindCount++)
		{
			int32 WeightPerList = 0;
			const TArray<FString>& KeywordArray = *WeightedArrayList[iFindCount].Array;
			int32 EachWeight = WeightedArrayList[iFindCount].Weight;
			int32 WholeMatchCount = 0;
			int32 WholeMatchMultiplier = (iFindCount < NonLocalizedFirstIndex) ? WholeMatchLocalizedWeightMultiplier : WholeMatchWeightMultiplier;

			for (int32 iEachWord = 0; iEachWord < KeywordArray.Num(); iEachWord++)
			{
				// If we get an exact match weight the find count to get exact matches higher priority
				if (KeywordArray[iEachWord].StartsWith(*EachTerm, ESearchCase::CaseSensitive))
				{
					if (KeywordArray[iEachWord].EndsWith(*EachTermSanitized, ESearchCase::CaseSensitive))
					// if (iEachWord == 0)
					{
						// WeightPerList += EachWeight * WholeMatchMultiplier;
						WeightPerList += EachWeight * WholeWordMultiplier;
					}
					else
					{
						WeightPerList += EachWeight;
					}
					WholeMatchCount++;
				}
				else if (KeywordArray[iEachWord].Contains(*EachTerm, ESearchCase::CaseSensitive))
				{
					WeightPerList += EachWeight;
				}
				if (KeywordArray[iEachWord].StartsWith(*EachTermSanitized, ESearchCase::CaseSensitive))
				{
					// if (iEachWord == 0)
					if (KeywordArray[iEachWord].EndsWith(*EachTermSanitized, ESearchCase::CaseSensitive))
					{
						WeightPerList += EachWeight * WholeWordMultiplier;
					}
					else
					{
						WeightPerList += EachWeight;
					}
					WholeMatchCount++;
				}
				else if (KeywordArray[iEachWord].Contains(*EachTermSanitized, ESearchCase::CaseSensitive))
				{
					WeightPerList += EachWeight / 2;
				}
			}
			// Increase the weight if theres a larger % of matches in the keyword list
			if (WholeMatchCount != 0)
			{
				int32 PercentAdjust = (100 / KeywordArray.Num()) * WholeMatchCount;
				WeightPerList *= PercentAdjust;
			}
			TotalWeight += WeightPerList;
		}
	}
	return TotalWeight;
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
	return SNew(SHorizontalBox)
	.ToolTipText(Item->ToolTip)
	+ SHorizontalBox::Slot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	.AutoWidth()
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush(TEXT("NoBorder")))
		.Padding(3.f)
		[
			SNew(STextBlock)
			.Text(Item->DisplayName)
			.WrapTextAt(300.f)
			.HighlightText(this, &SNiagaraGraphActionMenu::GetFilterText)
			.TextStyle(FNiagaraEditorStyle::Get(), "ActionMenu.ActionTextBlock")
		]
	]
	+ SHorizontalBox::Slot()
	.FillWidth(1.f)
	[
		SNew(SSpacer)
	]
	+ SHorizontalBox::Slot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Fill)
	[
		SNew(SSeparator)
		.SeparatorImage(FNiagaraEditorStyle::Get().GetBrush("MenuSeparator"))
		.Orientation(EOrientation::Orient_Vertical)
		.Visibility(Item->SourceData.bDisplaySource ? EVisibility::Visible : EVisibility::Collapsed)
	]
	+ SHorizontalBox::Slot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	.Padding(5, 0, 0, 0)
	.AutoWidth()
	[
		SNew(SBox)
		.WidthOverride(90.f)
		.Visibility(Item->SourceData.bDisplaySource ? EVisibility::Visible : EVisibility::Collapsed)
		[
			SNew(STextBlock)
			.Text(Item->SourceData.SourceText)
			.ColorAndOpacity(FNiagaraEditorUtilities::GetScriptSourceColor(Item->SourceData.Source))
			.TextStyle(FNiagaraEditorStyle::Get(), "GraphActionMenu.ActionSourceTextBlock")
		]
	];
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
