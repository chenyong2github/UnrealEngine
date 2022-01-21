// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBlueprintNamespaceEntry.h"
#include "BlueprintNamespaceRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSuggestionTextBox.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SBlueprintNamespaceEntry"

float SBlueprintNamespaceEntry::NamespaceListBorderPadding = 1.0f;
float SBlueprintNamespaceEntry::NamespaceListMinDesiredWidth = 350.0f;

void SBlueprintNamespaceEntry::Construct(const FArguments& InArgs)
{
	CurrentNamespace = InArgs._CurrentNamespace;
	OnNamespaceSelected = InArgs._OnNamespaceSelected;
	OnFilterNamespaceList = InArgs._OnFilterNamespaceList;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SAssignNew(TextBox, SSuggestionTextBox)
			.Font(InArgs._Font)
			.ForegroundColor(FSlateColor::UseForeground())
			.Visibility(InArgs._AllowTextEntry ? EVisibility::Visible : EVisibility::Collapsed)
			.Text(FText::FromString(CurrentNamespace))
			.OnTextChanged(this, &SBlueprintNamespaceEntry::OnTextChanged)
			.OnTextCommitted(this, &SBlueprintNamespaceEntry::OnTextCommitted)
			.OnShowingSuggestions(this, &SBlueprintNamespaceEntry::OnShowingSuggestions)
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(ComboButton, SComboButton)
			.CollapseMenuOnParentFocus(true)
			.OnGetMenuContent(this, &SBlueprintNamespaceEntry::OnGetNamespaceListMenuContent)
			.ButtonContent()
			[
				InArgs._ButtonContent.Widget
			]
		]
	];
}

void SBlueprintNamespaceEntry::SetCurrentNamespace(const FString& InNamespace)
{
	// Pass through the text box in order to validate the string before committing it to the current value.
	if (TextBox.IsValid())
	{
		TextBox->SetText(FText::FromString(InNamespace));
	}
}

void SBlueprintNamespaceEntry::OnTextChanged(const FText& InText)
{
	// Note: Empty string is valid (i.e. global namespace).
	bool bIsValidString = true;

	// Only allow alphanumeric characters, '.' and '_'.
	FString NewString = InText.ToString();
	for (const TCHAR& NewChar : NewString)
	{
		if (!FChar::IsAlnum(NewChar) && NewChar != TEXT('_') && NewChar != TEXT('.'))
		{
			bIsValidString = false;
			break;
		}
	}

	FString ErrorText;
	if (bIsValidString)
	{
		// Keep the current namespace in sync with the last-known valid text box value.
		CurrentNamespace = MoveTemp(NewString);
	}
	else
	{
		ErrorText = LOCTEXT("InvalidNamespaceIdentifierStringError", "Invalid namespace identifier string.").ToString();
	}

	// Set the error text regardless of whether or not the path is valid; this will clear the error state if the string is valid.
	if (TextBox.IsValid())
	{
		TextBox->SetError(ErrorText);
	}
}

void SBlueprintNamespaceEntry::OnTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	// Not using the current textbox value here because it might be invalid, and we want to revert to the last-known valid namespace string on commit.
	SelectNamespace(CurrentNamespace);
}

void SBlueprintNamespaceEntry::OnShowingSuggestions(const FString& InputText, TArray<FString>& OutSuggestions)
{
	int32 PathEnd;
	FString CurrentPath;
	FString CurrentName;
	if (InputText.FindLastChar(TEXT('.'), PathEnd))
	{
		CurrentPath = InputText.LeftChop(InputText.Len() - PathEnd);
		CurrentName = InputText.RightChop(PathEnd + 1);
	}
	else
	{
		CurrentName = InputText;
	}

	// Find all names (path segments) that fall under the current path prefix.
	TArray<FName> SuggestedNames;
	FBlueprintNamespaceRegistry::Get().GetNamesUnderPath(CurrentPath, SuggestedNames);

	// Sort the list alphabetically.
	Algo::Sort(SuggestedNames, FNameLexicalLess());

	// Build the suggestion set based on the set of matching names we found above.
	TStringBuilder<128> PathBuilder;
	for (FName SuggestedName : SuggestedNames)
	{
		FString SuggestedNameAsString = SuggestedName.ToString();
		if (CurrentName.IsEmpty() || SuggestedNameAsString.StartsWith(CurrentName))
		{
			if (CurrentPath.Len() > 0)
			{
				PathBuilder += CurrentPath;
				PathBuilder += TEXT(".");
			}

			PathBuilder += SuggestedNameAsString;

			FString SuggestedNamespace = PathBuilder.ToString();
			OutSuggestions.Add(MoveTemp(SuggestedNamespace));

			PathBuilder.Reset();
		}
	}
}

TSharedRef<SWidget> SBlueprintNamespaceEntry::OnGetNamespaceListMenuContent()
{
	// Find and filter all registered paths.
	PopulateNamespaceList();

	// Construct the list view widget that we'll use for the menu content.
	SAssignNew(ListView, SListView<TSharedPtr<FString>>)
		.SelectionMode(ESelectionMode::SingleToggle)
		.ListItemsSource(&ListItems)
		.OnGenerateRow(this, &SBlueprintNamespaceEntry::OnGenerateRowForNamespaceList)
		.OnSelectionChanged(this, &SBlueprintNamespaceEntry::OnNamespaceListSelectionChanged);

	// If the current namespace is non-empty, look for a matching item in the set.
	if (!CurrentNamespace.IsEmpty())
	{
		const TSharedPtr<FString>* CurrentItemPtr = ListItems.FindByPredicate([&CurrentNamespace = this->CurrentNamespace](const TSharedPtr<FString>& Item)
		{
			return Item.IsValid() && *Item == CurrentNamespace;
		});

		// If we found a match, make it the initial selection.
		if (CurrentItemPtr)
		{
			ListView->SetSelection(*CurrentItemPtr);
		}
	}

	return SNew(SBorder)
		.Padding(NamespaceListBorderPadding)
		[
			SNew(SBox)
			.MinDesiredWidth(NamespaceListMinDesiredWidth)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(SearchBox, SSearchBox)
					.OnTextChanged(this, &SBlueprintNamespaceEntry::OnNamespaceListFilterTextChanged)
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SSeparator)
				]
				+SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					ListView.ToSharedRef()
				]
			]
		];
}

TSharedRef<ITableRow> SBlueprintNamespaceEntry::OnGenerateRowForNamespaceList(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	check(Item.IsValid());

	// Check for an empty list and add a single (disabled) item if found.
	bool bIsEnabled = true;
	FText ItemText = FText::FromString(*Item);
	if (ItemText.IsEmpty() && ListItems.Num() == 1)
	{
		bIsEnabled = false;
		ItemText = LOCTEXT("BlueprintNamespaceList_NoItems", "No Matching Items");
	}

	// Construct a new row widget, highlighting any text that matches the search filter.
	return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
		.IsEnabled(bIsEnabled)
		[
			SNew(STextBlock)
			.Text(ItemText)
			.HighlightText(bIsEnabled && SearchBox.IsValid() ? SearchBox->GetText() : FText::GetEmpty())
		];
}

void SBlueprintNamespaceEntry::OnNamespaceListFilterTextChanged(const FText& InText)
{
	// Gather/filter all registered paths.
	PopulateNamespaceList();

	// Refresh the namespace item list view.
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SBlueprintNamespaceEntry::OnNamespaceListSelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
{
	// These actions should not trigger a selection.
	if (SelectInfo == ESelectInfo::OnNavigation || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	if (Item.IsValid())
	{
		SelectNamespace(*Item);
	}

	// Clear the search filter text.
	if (SearchBox.IsValid())
	{
		SearchBox->SetText(FText::GetEmpty());
	}

	// Close the combo button menu after a selection.
	if (ComboButton.IsValid())
	{
		ComboButton->SetIsOpen(false);
	}

	// Switch focus back to the text box if present and visible.
	if (TextBox.IsValid() && TextBox->GetVisibility() == EVisibility::Visible)
	{
		FSlateApplication::Get().SetKeyboardFocus(TextBox);
		FSlateApplication::Get().SetUserFocus(0, TextBox);
	}
}

void SBlueprintNamespaceEntry::PopulateNamespaceList()
{
	// Clear the current list.
	ListItems.Empty();

	// Gather the full set of registered namespace paths.
	TArray<FString> AllPaths;
	FBlueprintNamespaceRegistry::Get().GetAllRegisteredPaths(AllPaths);

	// Invoke the delegate to allow owners to filter the list as needed.
	OnFilterNamespaceList.ExecuteIfBound(AllPaths);

	// Sort the list alphabetically.
	AllPaths.Sort();

	// Set up an expression evaluator to further trim the list according to the search filter.
	FTextFilterExpressionEvaluator SearchFilter(ETextFilterExpressionEvaluatorMode::BasicString);
	SearchFilter.SetFilterText(SearchBox.IsValid() ? SearchBox->GetText() : FText::GetEmpty());

	// Build the source item list for the list view widget.
	for (const FString& Path : AllPaths)
	{
		// Only include items that match the current search filter text.
		if (SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(Path)))
		{
			ListItems.Add(MakeShared<FString>(Path));
		}
	}

	// If no items were added, we signal this by adding a single blank entry.
	if (ListItems.Num() == 0)
	{
		ListItems.Add(MakeShared<FString>());
	}
}

void SBlueprintNamespaceEntry::SelectNamespace(const FString& InNamespace)
{
	if (TextBox.IsValid())
	{
		// Update the textbox to reflect the selected value. Note that this should also clear any error state via OnTextChanged().
		TextBox->SetText(FText::FromString(InNamespace));
	}

	// Invoke the delegate in response to the new selection.
	OnNamespaceSelected.ExecuteIfBound(InNamespace);
}

#undef LOCTEXT_NAMESPACE