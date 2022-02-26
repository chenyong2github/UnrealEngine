// Copyright Epic Games, Inc. All Rights Reserved.
#include "SOptimusShaderTextDocumentSubTab.h"

#include "SOptimusShaderTextSearchWidget.h"

#include "OptimusEditorStyle.h"
#include "EditorStyleSet.h" 

#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"


#define LOCTEXT_NAMESPACE "OptimusShaderTextDocumentSubTab"

FOptimusShaderTextEditorDocumentSubTabCommands::FOptimusShaderTextEditorDocumentSubTabCommands() 
	: TCommands<FOptimusShaderTextEditorDocumentSubTabCommands>(
		"OptimusShaderTextEditorDocumentSubTab", // Context name for fast lookup
		NSLOCTEXT("Contexts", "OptimusShaderTextEditorDocumentSubTab", "Deformer Shader Text Editor Document Sub Tab"), // Localized context name for displaying
		NAME_None,
		FEditorStyle::GetStyleSetName()
	)
{
}

void FOptimusShaderTextEditorDocumentSubTabCommands::RegisterCommands()
{
	UI_COMMAND(Search, "Search", "Search for a String", EUserInterfaceActionType::Button, FInputChord(EKeys::F, EModifierKey::Control));
}

SOptimusShaderTextDocumentSubTab::SOptimusShaderTextDocumentSubTab()
	:bIsSearchBarHidden(true)
	,CommandList(MakeShared<FUICommandList>())
{
}

SOptimusShaderTextDocumentSubTab::~SOptimusShaderTextDocumentSubTab()
{
}

void SOptimusShaderTextDocumentSubTab::Construct(const FArguments& InArgs, TSharedPtr<SDockTab> InParentTab)
{
	check(InParentTab.IsValid());
	ParentTab = InParentTab;
	
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
			.OnKeyCharHandler(this, &SOptimusShaderTextDocumentSubTab::OnTextKeyChar)
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
		.OnTextChanged(this, &SOptimusShaderTextDocumentSubTab::OnSearchTextChanged)
		.OnTextCommitted(this, &SOptimusShaderTextDocumentSubTab::OnSearchTextCommitted)
		.SearchResultData(this, &SOptimusShaderTextDocumentSubTab::GetSearchResultData)
		.OnResultNavigationButtonClicked(this, &SOptimusShaderTextDocumentSubTab::OnSearchResultNavigationButtonClicked);

	ChildSlot
	[
		SAssignNew(Area, SExpandableArea)
		.AreaTitle(InArgs._TabTitle)
		.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
		.InitiallyCollapsed(false)
		.OnAreaExpansionChanged(this, &SOptimusShaderTextDocumentSubTab::OnTabContentExpansionChanged)
		.BodyContent()
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
		]	
	];
}

void SOptimusShaderTextDocumentSubTab::RegisterCommands()
{
	
	const FOptimusShaderTextEditorDocumentSubTabCommands& Commands = FOptimusShaderTextEditorDocumentSubTabCommands::Get();
	
	CommandList->MapAction(
		Commands.Search,
		FExecuteAction::CreateSP(this, &SOptimusShaderTextDocumentSubTab::OnTriggerSearch)
	);
}

FReply SOptimusShaderTextDocumentSubTab::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!IsExpanded())
	{
		return FReply::Unhandled();
	}
	
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

bool SOptimusShaderTextDocumentSubTab::HandleEscape()
{
	if (HideSearchBar())
	{
		return true;
	}

	return false;
}

void SOptimusShaderTextDocumentSubTab::ShowSearchBar()
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

bool SOptimusShaderTextDocumentSubTab::HideSearchBar() 
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

void SOptimusShaderTextDocumentSubTab::OnTriggerSearch()
{
	ShowSearchBar();

	FText SelectedText = Text->GetSelectedText();
	
	// we start the search from the beginning of current selection.
	// goto clears the selection, but it will be restored by the first search
	Text->GoTo(Text->GetSelection().GetBeginning());

	SearchBar->TriggerSearch(SelectedText);
}

bool SOptimusShaderTextDocumentSubTab::IsExpanded() const
{
	return Area->IsExpanded();
}

void SOptimusShaderTextDocumentSubTab::Refresh() const
{
	Text->Refresh();
}

void SOptimusShaderTextDocumentSubTab::OnTabContentExpansionChanged(bool bIsExpanded)
{
	if (ensure(ParentTab.IsValid()))
	{
		if (bIsExpanded)
		{
			ParentTab.Pin()->SetShouldAutosize(false);
		}
		else
		{
			ParentTab.Pin()->SetShouldAutosize(true);
		}
	}
}

void SOptimusShaderTextDocumentSubTab::OnSearchTextChanged(const FText& InTextToSearch)
{
	Text->SetSearchText(InTextToSearch);
}

void SOptimusShaderTextDocumentSubTab::OnSearchTextCommitted(const FText& InTextToSearch, ETextCommit::Type InCommitType)
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

TOptional<SSearchBox::FSearchResultData> SOptimusShaderTextDocumentSubTab::GetSearchResultData() const
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

void SOptimusShaderTextDocumentSubTab::OnSearchResultNavigationButtonClicked(SSearchBox::SearchDirection InDirection)
{
	Text->AdvanceSearch(InDirection == SSearchBox::SearchDirection::Previous);
}

FReply SOptimusShaderTextDocumentSubTab::OnTextKeyChar(const FGeometry& MyGeometry,
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
