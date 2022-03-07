// Copyright Epic Games, Inc. All Rights Reserved.
#include "SOptimusShaderTextDocumentTextBox.h"

#include "SOptimusShaderTextSearchWidget.h"

#include "OptimusEditorStyle.h"
#include "EditorStyleSet.h" 

#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"

#define LOCTEXT_NAMESPACE "OptimusShaderTextDocumentTextBox"

FOptimusShaderTextEditorDocumentTextBoxCommands::FOptimusShaderTextEditorDocumentTextBoxCommands() 
	: TCommands<FOptimusShaderTextEditorDocumentTextBoxCommands>(
		"OptimusShaderTextEditorDocumentTextBox", // Context name for fast lookup
		NSLOCTEXT("Contexts", "OptimusShaderTextEditorDocumentTextBox", "Deformer Shader Text Editor Document TextBox"), // Localized context name for displaying
		NAME_None,
		FEditorStyle::GetStyleSetName()
	)
{
}

void FOptimusShaderTextEditorDocumentTextBoxCommands::RegisterCommands()
{
	UI_COMMAND(Search, "Search", "Search for a String", EUserInterfaceActionType::Button, FInputChord(EKeys::F, EModifierKey::Control));
}

SOptimusShaderTextDocumentTextBox::SOptimusShaderTextDocumentTextBox()
	:bIsSearchBarHidden(true)
	,CommandList(MakeShared<FUICommandList>())
{
}

SOptimusShaderTextDocumentTextBox::~SOptimusShaderTextDocumentTextBox()
{
}

void SOptimusShaderTextDocumentTextBox::Construct(const FArguments& InArgs)
{
	RegisterCommands();
	
	const TSharedPtr<SScrollBar> HScrollBar =
		SNew(SScrollBar)
		.Orientation(EOrientation::Orient_Horizontal);
	
	const TSharedPtr<SScrollBar> VScrollBar =
		SNew(SScrollBar)
		.Orientation(EOrientation::Orient_Vertical);
	
	const FTextBlockStyle &TextStyle = FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextEditor.NormalText");
	const FSlateFontInfo &Font = TextStyle.Font;
	
	Text =
		SNew(SMultiLineEditableText)
			.Font(Font)
			.TextStyle(&TextStyle)
			.Text(InArgs._Text)
			.OnTextChanged(InArgs._OnTextChanged)
			.OnKeyCharHandler(this, &SOptimusShaderTextDocumentTextBox::OnTextKeyChar)
			// By default, the Tab key gets routed to "next widget". We want to disable that behaviour.
			.OnIsTypedCharValid_Lambda([](const TCHAR InChar) { return true; })
			.Marshaller(InArgs._Marshaller)
			.AutoWrapText(false)
			.ClearTextSelectionOnFocusLoss(false)
			.AllowContextMenu(true)
			.IsReadOnly(InArgs._IsReadOnly)
			.HScrollBar(HScrollBar)
			.VScrollBar(VScrollBar);

	SearchBar =
		SNew(SOptimusShaderTextSearchWidget)
		.OnTextChanged(this, &SOptimusShaderTextDocumentTextBox::OnSearchTextChanged)
		.OnTextCommitted(this, &SOptimusShaderTextDocumentTextBox::OnSearchTextCommitted)
		.SearchResultData(this, &SOptimusShaderTextDocumentTextBox::GetSearchResultData)
		.OnResultNavigationButtonClicked(this, &SOptimusShaderTextDocumentTextBox::OnSearchResultNavigationButtonClicked);

	ChildSlot
	[
		SAssignNew(TabBody, SVerticalBox)
		+ SVerticalBox::Slot()
		[
			SNew(SBorder)
			.BorderImage(FOptimusEditorStyle::Get().GetBrush("TextEditor.Border"))
			.BorderBackgroundColor(FLinearColor::Black)
			[
				SNew(SGridPanel)
				.FillColumn(0,1.0f)
				.FillRow(0,1.0f)
				+SGridPanel::Slot(0,0)
				[
					Text.ToSharedRef()
				]
				+SGridPanel::Slot(1,0)
				[
					VScrollBar.ToSharedRef()
				]
				+SGridPanel::Slot(0,1)
				[
					HScrollBar.ToSharedRef()
				]
			]		
		]
	];
}

void SOptimusShaderTextDocumentTextBox::RegisterCommands()
{
	
	const FOptimusShaderTextEditorDocumentTextBoxCommands& Commands = FOptimusShaderTextEditorDocumentTextBoxCommands::Get();
	
	CommandList->MapAction(
		Commands.Search,
		FExecuteAction::CreateSP(this, &SOptimusShaderTextDocumentTextBox::OnTriggerSearch)
	);
}

FReply SOptimusShaderTextDocumentTextBox::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	
	if (Key == EKeys::Escape)
	{
		if (HandleEscape())
		{
			return FReply::Handled();
		}
	}

	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

bool SOptimusShaderTextDocumentTextBox::HandleEscape()
{
	if (HideSearchBar())
	{
		return true;
	}

	return false;
}

void SOptimusShaderTextDocumentTextBox::ShowSearchBar()
{
	if (bIsSearchBarHidden)
	{
		bIsSearchBarHidden = false;
		
		TabBody->InsertSlot(0)
		.AutoHeight()
		[
			SearchBar.ToSharedRef()
		];
	}
}

bool SOptimusShaderTextDocumentTextBox::HideSearchBar() 
{
	if (!bIsSearchBarHidden)
	{
		bIsSearchBarHidden = true;
		SearchBar->ClearSearchText();
		TabBody->RemoveSlot(SearchBar.ToSharedRef());
			
		FSlateApplication::Get().ForEachUser([&](FSlateUser& User) {
			User.SetFocus(Text.ToSharedRef(), EFocusCause::SetDirectly);
		});
		return true;
	}
	
	return false;
}

void SOptimusShaderTextDocumentTextBox::OnTriggerSearch()
{
	ShowSearchBar();

	FText SelectedText = Text->GetSelectedText();
	
	// we start the search from the beginning of current selection.
	// goto clears the selection, but it will be restored by the first search
	Text->GoTo(Text->GetSelection().GetBeginning());

	SearchBar->TriggerSearch(SelectedText);
}

void SOptimusShaderTextDocumentTextBox::Refresh() const
{
	Text->Refresh();
}


void SOptimusShaderTextDocumentTextBox::OnSearchTextChanged(const FText& InTextToSearch)
{
	Text->SetSearchText(InTextToSearch);
}

void SOptimusShaderTextDocumentTextBox::OnSearchTextCommitted(const FText& InTextToSearch, ETextCommit::Type InCommitType)
{
	if (!InTextToSearch.EqualTo(Text->GetSearchText()))
	{
		Text->SetSearchText(InTextToSearch);
	}
	else
	{
		if (InCommitType == ETextCommit::Type::OnEnter)
		{
			Text->AdvanceSearch(false);
		}
	}
}

TOptional<SSearchBox::FSearchResultData> SOptimusShaderTextDocumentTextBox::GetSearchResultData() const
{
	FText SearchText = Text->GetSearchText();
	
	if (!SearchText.IsEmpty())
	{
		SSearchBox::FSearchResultData Result;
		Result.CurrentSearchResultIndex = Text->GetSearchResultIndex();
		Result.NumSearchResults = Text->GetNumSearchResults();
		
		return Result;
	}
		
	return TOptional<SSearchBox::FSearchResultData>();
}

void SOptimusShaderTextDocumentTextBox::OnSearchResultNavigationButtonClicked(SSearchBox::SearchDirection InDirection)
{
	Text->AdvanceSearch(InDirection == SSearchBox::SearchDirection::Previous);
}

FReply SOptimusShaderTextDocumentTextBox::OnTextKeyChar(const FGeometry& MyGeometry,
	const FCharacterEvent& InCharacterEvent)
{
	if (Text->IsTextReadOnly())
	{
		return FReply::Unhandled();
	}

	const TCHAR Character = InCharacterEvent.GetCharacter();
	if (Character == TEXT('\t'))
	{
		// Tab to nearest 4.
		Text->InsertTextAtCursor(TEXT("    "));
		return FReply::Handled();
	}
	else if (Character == TEXT('\n') || Character == TEXT('\r'))
	{
		// Figure out if we need to auto-indent.
		FString CurrentLine;
		Text->GetCurrentTextLine(CurrentLine);

		// See what the open/close curly brace balance is.
		int32 BraceBalance = 0;
		for (TCHAR Char : CurrentLine)
		{
			BraceBalance += (Char == TEXT('{'));
			BraceBalance -= (Char == TEXT('}'));
		}

		return FReply::Handled();
	}
	else
	{
		// Let SMultiLineEditableText::OnKeyChar handle it.
		return FReply::Unhandled();
	}
}


#undef LOCTEXT_NAMESPACE
