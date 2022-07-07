// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/SFilterSearchBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "CustomTextFilter"

void SFilterSearchBox::Construct( const FArguments& InArgs )
{
	MaxSearchHistory = InArgs._MaxSearchHistory;
	OnTextChanged = InArgs._OnTextChanged;
	OnTextCommitted = InArgs._OnTextCommitted;
	bShowSearchHistory = InArgs._ShowSearchHistory;
	OnSaveSearchClicked = InArgs._OnSaveSearchClicked;

	// Default text shown when there are no items in the search history
	EmptySearchHistoryText = MakeShareable(new FText(LOCTEXT("EmptySearchHistoryText", "The Search History is Empty")));
	SearchHistory.Add(EmptySearchHistoryText);
	
	ChildSlot
	[
		SAssignNew(SearchHistoryBox, SMenuAnchor)
		.Placement(EMenuPlacement::MenuPlacement_ComboBoxRight)
		[
			SNew(SOverlay)
			
			+ SOverlay::Slot()
			[
				SAssignNew(SearchBox, SSearchBox)
				.InitialText(InArgs._InitialText)
				.HintText(InArgs._HintText)
				.OnTextChanged(this, &SFilterSearchBox::HandleTextChanged)
				.OnTextCommitted(this, &SFilterSearchBox::HandleTextCommitted)
				.SelectAllTextWhenFocused( false )
				.DelayChangeNotificationsWhileTyping( InArgs._DelayChangeNotificationsWhileTyping )
				.OnKeyDownHandler(InArgs._OnKeyDownHandler)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					// Button to save the currently occuring search 
					SNew(SButton)
					.ContentPadding(0)
					.Visibility_Lambda([this]()
					{
						// Only visible if there is a search active currently and the OnSaveSearchClicked delegate is bound
						return this->GetText().IsEmpty() || !this->OnSaveSearchClicked.IsBound() ? EVisibility::Collapsed : EVisibility::Visible;
					})
					.ButtonStyle(FAppStyle::Get(), "HoverOnlyButton")
					.OnClicked_Lambda([this]()
					{
						this->OnSaveSearchClicked.ExecuteIfBound(this->GetText());
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					// Chevron to open the search history dropdown
					SNew(SButton)
					.ContentPadding(0)
					.ButtonStyle(FAppStyle::Get(), "HoverOnlyButton")
					.ClickMethod(EButtonClickMethod::MouseDown)
					.OnClicked(this, &SFilterSearchBox::OnClickedSearchHistory)
					.ToolTipText(LOCTEXT("SearchHistoryToolTipText", "Click to show the Search History"))
					.Visibility(this, &SFilterSearchBox::GetSearchHistoryVisibility)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.ChevronDown"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
		]
		.MenuContent
		(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Menu.Background"))
			.Padding( FMargin(2) )
			[
				SAssignNew(SearchHistoryListView, SListView< TSharedPtr<FText> >)
				.ListItemsSource(&SearchHistory)
				.SelectionMode( ESelectionMode::Single )
				.OnGenerateRow(this, &SFilterSearchBox::MakeSearchHistoryRowWidget)
				.OnSelectionChanged( this, &SFilterSearchBox::OnSelectionChanged)
				.ItemHeight(18)
				.ScrollbarDragFocusCause(EFocusCause::SetDirectly) 
			]
		)
	];
}

bool SFilterSearchBox::SupportsKeyboardFocus() const
{
	return SearchBox->SupportsKeyboardFocus();
}

bool SFilterSearchBox::HasKeyboardFocus() const
{
	// Since keyboard focus is forwarded to our editable text, we will test it instead
	return SearchBox->HasKeyboardFocus();
}

FReply SFilterSearchBox::OnFocusReceived( const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent )
{
	// Forward keyboard focus to our editable text widget
	return FReply::Handled().SetUserFocus(SearchBox.ToSharedRef(), InFocusEvent.GetCause());
}

/** Handler for when text in the editable text box changed */
void SFilterSearchBox::HandleTextChanged(const FText& NewText)
{
	OnTextChanged.ExecuteIfBound(NewText);
}

/** Handler for when text in the editable text box changed */
void SFilterSearchBox::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	// Set the text and execute the delegate
	SetText(NewText);
	OnTextCommitted.ExecuteIfBound(NewText, CommitType);
	
	UpdateSearchHistory(NewText);
}

void SFilterSearchBox::SetText(const TAttribute< FText >& InNewText)
{
	SearchBox->SetText(InNewText);
}

FText SFilterSearchBox::GetText() const
{
	return SearchBox->GetText();
}

void SFilterSearchBox::SetError( const FText& InError )
{
	SearchBox->SetError(InError);
}

void SFilterSearchBox::SetError( const FString& InError )
{
	SearchBox->SetError(InError);
}

void SFilterSearchBox::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	// Update the search history on focus loss (for cases with SearchBoxes that don't commit on Enter)
	UpdateSearchHistory(SearchBox->GetText());
}

/** Called by SListView when the selection changes in the search history list */
void SFilterSearchBox::OnSelectionChanged( TSharedPtr<FText> NewValue, ESelectInfo::Type SelectInfo )
{
	/* Make sure the user can only select an item in the history using the mouse, and that they cannot select the
	 * Placeholder text for empty search history
	 */
	if(SelectInfo != ESelectInfo::OnNavigation && NewValue && NewValue != EmptySearchHistoryText)
	{
		SearchBox->SetText(*NewValue);
		SearchHistoryBox->SetIsOpen(false);
	}
}

TSharedRef<ITableRow> SFilterSearchBox::MakeSearchHistoryRowWidget(TSharedPtr<FText> SearchText, const TSharedRef<STableViewBase>& OwnerTable)
{
	bool bIsEmptySearchHistory = (SearchText == EmptySearchHistoryText);
	EHorizontalAlignment TextAlignment = (bIsEmptySearchHistory) ? HAlign_Center : HAlign_Left;

	TSharedPtr<SHorizontalBox> RowWidget = SNew(SHorizontalBox);

	// The actual search text
	RowWidget->AddSlot()
	.HAlign(TextAlignment)
	.VAlign(VAlign_Center)
	.FillWidth(1.0)
	[
		SNew(STextBlock)
		.Text(*SearchText.Get())
	];
	
	return SNew(STableRow< TSharedPtr<FString> >, OwnerTable)
			.ShowSelection(!bIsEmptySearchHistory)
			[
				RowWidget.ToSharedRef()
			];
}

void SFilterSearchBox::UpdateSearchHistory(const FText &NewText)
{
	// Don't save empty searches
	if(NewText.IsEmpty())
	{
		return;
	}

	// If there is only one item, and it is the placeholder empty search text, remove it
	if(SearchHistory.Num() == 1 && SearchHistory[0] == EmptySearchHistoryText)
	{
		SearchHistory.Empty();
	}

	// Remove any existing occurances of the current search text, we will re-add it to the top if so
	SearchHistory.RemoveAll([&NewText](TSharedPtr<FText> SearchHistoryText)
	{
		if(SearchHistoryText->CompareTo(NewText) == 0)
		{
			return true;
		}
		return false;
	});

	// Insert the current search as the most recent in the history
	SearchHistory.Insert(MakeShareable(new FText(NewText)), 0);

	// Prune old entries until we are at the Max Search History limit
	while( SearchHistory.Num() > MaxSearchHistory)
	{
		SearchHistory.RemoveAt( SearchHistory.Num()-1 );
	}

	SearchHistoryListView->RequestListRefresh();
}

FReply SFilterSearchBox::OnClickedSearchHistory()
{
	if(SearchHistoryBox->ShouldOpenDueToClick() && !SearchHistory.IsEmpty())
	{
		SearchHistoryBox->SetIsOpen(true);
		SearchHistoryListView->ClearSelection();
	}
	else
	{
		SearchHistoryBox->SetIsOpen(false);
	}
	
	return FReply::Handled();
}

void SFilterSearchBox::SetOnSaveSearchHandler(FOnSaveSearchClicked InOnSaveSearchHandler)
{
	OnSaveSearchClicked = InOnSaveSearchHandler;
}

EVisibility SFilterSearchBox::GetSearchHistoryVisibility() const
{
	return bShowSearchHistory.Get() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
