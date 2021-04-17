// Copyright Epic Games, Inc. All Rights Reserved.
#include "Stack/SNiagaraStackItemGroupAddMenu.h"
#include "EditorStyleSet.h"
#include "NiagaraActions.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/Stack/INiagaraStackItemGroupAddUtilities.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraph/EdGraph.h"
#include "SGraphActionMenu.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SItemSelector.h"
#include "Widgets/SNiagaraLibraryOnlyToggleHeader.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "NiagaraStackItemGroupAddMenu"

bool SNiagaraStackItemGroupAddMenu::bLibraryOnly = true;

void SNiagaraStackItemGroupAddMenu::Construct(const FArguments& InArgs, INiagaraStackItemGroupAddUtilities* InAddUtilities, int32 InInsertIndex)
{
	TSharedPtr<SNiagaraLibraryOnlyToggleHeader> LibraryOnlyToggle;

	AddUtilities = InAddUtilities;
	InsertIndex = InInsertIndex;
	bSetFocusOnNextTick = true;

	SAssignNew(SourceFilter, SNiagaraSourceFilterBox)
    .OnFiltersChanged(this, &SNiagaraStackItemGroupAddMenu::TriggerRefresh);
	
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(1.0f)
			.AutoHeight()
			[
				SAssignNew(LibraryOnlyToggle, SNiagaraLibraryOnlyToggleHeader)
				.HeaderLabelText(FText::Format(LOCTEXT("AddToGroupFormatTitle", "Add new {0}"), AddUtilities->GetAddItemName()))
				.LibraryOnly(this, &SNiagaraStackItemGroupAddMenu::GetLibraryOnly)
				.LibraryOnlyChanged(this, &SNiagaraStackItemGroupAddMenu::SetLibraryOnly)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
            [
				SourceFilter.ToSharedRef()
            ]
			+SVerticalBox::Slot()
			[
				SNew(SBox)
				.WidthOverride(450)
				.HeightOverride(400)
				[
					SAssignNew(ActionSelector, SNiagaraStackAddSelector)
					.Items(CollectActions())
					.OnGetCategoriesForItem(this, &SNiagaraStackItemGroupAddMenu::OnGetCategoriesForItem)
	                .OnGetSectionsForItem(this, &SNiagaraStackItemGroupAddMenu::OnGetSectionsForItem)
	                .OnCompareSectionsForEquality(this, &SNiagaraStackItemGroupAddMenu::OnCompareSectionsForEquality)
	                .OnCompareSectionsForSorting(this, &SNiagaraStackItemGroupAddMenu::OnCompareSectionsForSorting)
	                .OnCompareCategoriesForEquality(this, &SNiagaraStackItemGroupAddMenu::OnCompareCategoriesForEquality)
	                .OnCompareCategoriesForSorting(this, &SNiagaraStackItemGroupAddMenu::OnCompareCategoriesForSorting)
	                .OnCompareItemsForSorting(this, &SNiagaraStackItemGroupAddMenu::OnCompareItemsForSorting)
	                .OnDoesItemMatchFilterText(this, &SNiagaraStackItemGroupAddMenu::OnDoesItemMatchFilterText)
	                .OnGenerateWidgetForSection(this, &SNiagaraStackItemGroupAddMenu::OnGenerateWidgetForSection)
	                .OnGenerateWidgetForCategory(this, &SNiagaraStackItemGroupAddMenu::OnGenerateWidgetForCategory)
	                .OnGenerateWidgetForItem(this, &SNiagaraStackItemGroupAddMenu::OnGenerateWidgetForItem)
	                .OnGetItemWeightForSelection(this, &SNiagaraStackItemGroupAddMenu::OnGetItemWeightForSelection)
	                .OnItemActivated(this, &SNiagaraStackItemGroupAddMenu::OnItemActivated)
	                .AllowMultiselect(false)
	                .OnDoesItemPassCustomFilter(this, &SNiagaraStackItemGroupAddMenu::DoesItemPassCustomFilter)
	                .ClickActivateMode(EItemSelectorClickActivateMode::SingleClick)
	                .ExpandInitially(false)
	                .OnGetSectionData_Lambda([](const ENiagaraMenuSections& Section)
	                {
	                    if(Section == ENiagaraMenuSections::Suggested)
	                    {
	                        return SNiagaraStackAddSelector::FSectionData(SNiagaraStackAddSelector::FSectionData::List, true);
	                    }

	                    return SNiagaraStackAddSelector::FSectionData(SNiagaraStackAddSelector::FSectionData::Tree, false);
	                })
				]
			]
		]
	];
}

TSharedPtr<SWidget> SNiagaraStackItemGroupAddMenu::GetFilterTextBox()
{
	return ActionSelector->GetSearchBox();
}

TArray<TSharedPtr<FNiagaraMenuAction_Generic>> SNiagaraStackItemGroupAddMenu::CollectActions()
{
	TArray<TSharedPtr<FNiagaraMenuAction_Generic>> OutActions;
	
	FNiagaraStackItemGroupAddOptions AddOptions;
	AddOptions.bIncludeDeprecated = false;
	AddOptions.bIncludeNonLibrary = true;

	TArray<TSharedRef<INiagaraStackItemGroupAddAction>> AddActions;
	AddUtilities->GenerateAddActions(AddActions, AddOptions);

	for (TSharedRef<INiagaraStackItemGroupAddAction> AddAction : AddActions)
	{
		TSharedPtr<FNiagaraMenuAction_Generic> NewNodeAction(
            new FNiagaraMenuAction_Generic(
            FNiagaraMenuAction_Generic::FOnExecuteAction::CreateRaw(AddUtilities, &INiagaraStackItemGroupAddUtilities::ExecuteAddAction, AddAction, InsertIndex),
            AddAction->GetDisplayName(), AddAction->GetSuggested() ? ENiagaraMenuSections::Suggested : ENiagaraMenuSections::General, AddAction->GetCategories(), AddAction->GetDescription(), AddAction->GetKeywords()));
		NewNodeAction->SourceData = AddAction->GetSourceData();
		NewNodeAction->bIsInLibrary = AddAction->IsInLibrary();
		OutActions.Add(NewNodeAction);
	}

	return OutActions;
}

void SNiagaraStackItemGroupAddMenu::OnItemActivated(const TSharedPtr<FNiagaraMenuAction_Generic>& SelectedAction)
{
	TSharedPtr<FNiagaraMenuAction_Generic> CurrentAction = StaticCastSharedPtr<FNiagaraMenuAction_Generic>(SelectedAction);

	if (CurrentAction.IsValid())
	{
		FSlateApplication::Get().DismissAllMenus();
		CurrentAction->Execute();
	}	
}

void SNiagaraStackItemGroupAddMenu::TriggerRefresh(const TMap<EScriptSource, bool>& SourceState)
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

bool SNiagaraStackItemGroupAddMenu::GetLibraryOnly() const
{
	return bLibraryOnly;
}

void SNiagaraStackItemGroupAddMenu::SetLibraryOnly(bool bInLibraryOnly)
{
	bLibraryOnly = bInLibraryOnly;
	ActionSelector->RefreshAllItems(true);
}

TArray<FString> SNiagaraStackItemGroupAddMenu::OnGetCategoriesForItem(
	const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
{
	return Item->Categories;
}

TArray<ENiagaraMenuSections> SNiagaraStackItemGroupAddMenu::OnGetSectionsForItem(
	const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
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

bool SNiagaraStackItemGroupAddMenu::OnCompareSectionsForEquality(const ENiagaraMenuSections& SectionA,
	const ENiagaraMenuSections& SectionB)
{
	return SectionA == SectionB;
}

bool SNiagaraStackItemGroupAddMenu::OnCompareSectionsForSorting(const ENiagaraMenuSections& SectionA,
	const ENiagaraMenuSections& SectionB)
{
	return SectionA < SectionB;
}

bool SNiagaraStackItemGroupAddMenu::OnCompareCategoriesForEquality(const FString& CategoryA, const FString& CategoryB)
{
	return CategoryA.Compare(CategoryB) == 0;
}

bool SNiagaraStackItemGroupAddMenu::OnCompareCategoriesForSorting(const FString& CategoryA, const FString& CategoryB)
{
	return CategoryA.Compare(CategoryB) == -1;
}

bool SNiagaraStackItemGroupAddMenu::OnCompareItemsForEquality(const TSharedPtr<FNiagaraMenuAction_Generic>& ItemA,
	const TSharedPtr<FNiagaraMenuAction_Generic>& ItemB)
{
	return ItemA->DisplayName.EqualTo(ItemB->DisplayName);
}

bool SNiagaraStackItemGroupAddMenu::OnCompareItemsForSorting(const TSharedPtr<FNiagaraMenuAction_Generic>& ItemA,
	const TSharedPtr<FNiagaraMenuAction_Generic>& ItemB)
{
	return ItemA->DisplayName.CompareTo(ItemB->DisplayName) == -1;
}

bool SNiagaraStackItemGroupAddMenu::OnDoesItemMatchFilterText(const FText& FilterText,
	const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
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

	for(FString& Category : Item->Categories)
	{
		if(Category.Contains(FilterText.ToString()))
		{
			return true;
		}
	}

	return false;
}

int32 SNiagaraStackItemGroupAddMenu::OnGetItemWeightForSelection(const TSharedPtr<FNiagaraMenuAction_Generic>& InCurrentAction,
	const TArray<FString>& InFilterTerms, const TArray<FString>& InSanitizedFilterTerms) const
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

TSharedRef<SWidget> SNiagaraStackItemGroupAddMenu::OnGenerateWidgetForSection(const ENiagaraMenuSections& Section)
{
	UEnum* SectionEnum = StaticEnum<ENiagaraMenuSections>();
	FText TextContent = SectionEnum->GetDisplayNameTextByValue((int64) Section);
	
	return SNew(STextBlock)
        .Text(TextContent)
        .TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerAssetCategoryText");
}

TSharedRef<SWidget> SNiagaraStackItemGroupAddMenu::OnGenerateWidgetForCategory(const FString& Category)
{
	FText TextContent = FText::FromString(Category);

	return SNew(SRichTextBlock)
        .Text(TextContent)
        .DecoratorStyleSet(&FEditorStyle::Get())
        .TextStyle(FNiagaraEditorStyle::Get(), "ActionMenu.HeadingTextBlock");
}

TSharedRef<SWidget> SNiagaraStackItemGroupAddMenu::OnGenerateWidgetForItem(
	const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
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
            .HighlightText(this, &SNiagaraStackItemGroupAddMenu::GetFilterText)
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

bool SNiagaraStackItemGroupAddMenu::DoesItemPassCustomFilter(const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
{
	bool bLibraryConditionFulfilled = (bLibraryOnly && Item->bIsInLibrary) || !bLibraryOnly;
	return SourceFilter->IsFilterActive(Item->SourceData.Source) && bLibraryConditionFulfilled;
}

#undef LOCTEXT_NAMESPACE
