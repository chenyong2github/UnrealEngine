// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraStack.h"

#include "NiagaraEditorModule.h"
#include "NiagaraEditorCommands.h"
#include "EditorStyleSet.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEmitterHandle.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackModuleItemOutputCollection.h"
#include "ViewModels/Stack/NiagaraStackModuleItemLinkedInputCollection.h"
#include "ViewModels/Stack/NiagaraStackModuleItemOutput.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "ViewModels/Stack/NiagaraStackInputCategory.h" 
#include "ViewModels/Stack/NiagaraStackPropertyRow.h"
#include "ViewModels/Stack/NiagaraStackItemFooter.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackParameterStoreEntry.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "NiagaraEmitter.h"
#include "IDetailTreeNode.h"
#include "Stack/NiagaraStackPropertyRowUtilities.h"
#include "Stack/SNiagaraStackFunctionInputName.h"
#include "Stack/SNiagaraStackFunctionInputValue.h"
#include "Stack/SNiagaraStackItem.h"
#include "Stack/SNiagaraStackItemFooter.h"
#include "Stack/SNiagaraStackItemGroup.h"
#include "Stack/SNiagaraStackModuleItem.h"
#include "Stack/SNiagaraStackParameterStoreEntryName.h"
#include "Stack/SNiagaraStackParameterStoreEntryValue.h"
#include "Stack/SNiagaraStackTableRow.h"
#include "Stack/SNiagaraStackIssueIcon.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Stack/SNiagaraStackErrorItem.h"
#include "NiagaraStackEditorData.h"
#include "ScopedTransaction.h"
#include "Widgets/Layout/SWrapBox.h"
#include "ViewModels/Stack/NiagaraStackRoot.h"
#include "NiagaraStackCommandContext.h"
#include "NiagaraEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorFontGlyphs.h"
#include "Framework/Commands/UICommandList.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "Widgets/SNiagaraParameterName.h"

#define LOCTEXT_NAMESPACE "NiagaraStack"

class SNiagaraStackEmitterHeader : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackEmitterHeader) {}
		SLATE_ATTRIBUTE(EVisibility, IssueIconVisibility);
		SLATE_EVENT(FSimpleDelegate, OnCycleThroughIssues);
		SLATE_ARGUMENT(TSharedPtr<SNiagaraStack>, ParentStack)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraEmitterHandleViewModel> InEmitterHandleViewModel, UNiagaraStackEntry* InRootEntry, UNiagaraStackViewModel* InStackViewModel)
	{
		EmitterHandleViewModel = InEmitterHandleViewModel;
		OnCycleThroughIssues = InArgs._OnCycleThroughIssues;
		StackViewModel = InStackViewModel;
		TopLevelViewModel = StackViewModel->GetTopLevelViewModelForEntry(*InRootEntry);
		ParentStackPtr = InArgs._ParentStack;
		ChildSlot
		[
			SNew(SVerticalBox)

			//~ Enable check box, view source emitter button, and external header controls.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
				//~ Enabled
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 4, 0, 0)
				.VAlign(VAlign_Top)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("EnabledToolTip", "Toggles whether this emitter is enabled. Disabled emitters don't simulate or render."))
					.IsChecked(EmitterHandleViewModel.ToSharedRef(), &FNiagaraEmitterHandleViewModel::GetIsEnabledCheckState)
					.OnCheckStateChanged(EmitterHandleViewModel.ToSharedRef(), &FNiagaraEmitterHandleViewModel::OnIsEnabledCheckStateChanged)
				]
				+ SHorizontalBox::Slot()
				.Padding(2)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				[
					// Name and Source Emitter Name
					SNew(SWrapBox)
					.Clipping(EWidgetClipping::ClipToBoundsAlways) 
					.UseAllottedSize(true)
					+ SWrapBox::Slot()
					[
				
						SAssignNew(EmitterNameTextBlock, SInlineEditableTextBlock)
						.ToolTipText(this, &SNiagaraStackEmitterHeader::GetEmitterNameToolTip)
						.Style(FNiagaraEditorStyle::Get(), "NiagaraEditor.HeadingInlineEditableText") 
						.Clipping(EWidgetClipping::ClipToBoundsAlways)
						.Text(EmitterHandleViewModel.ToSharedRef(), &FNiagaraEmitterHandleViewModel::GetNameText)
						.OnTextCommitted(EmitterHandleViewModel.ToSharedRef(), &FNiagaraEmitterHandleViewModel::OnNameTextComitted)
						.OnVerifyTextChanged(EmitterHandleViewModel.ToSharedRef(), &FNiagaraEmitterHandleViewModel::VerifyNameTextChanged)
						.IsReadOnly(EmitterHandleViewModel->CanRenameEmitter() == false)
					]
					+ SWrapBox::Slot()
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.ToolTipText(this, &SNiagaraStackEmitterHeader::GetEmitterNameToolTip)
						.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.SubduedHeadingTextBox")
						.Clipping(EWidgetClipping::ClipToBoundsAlways)
						.Text(EmitterHandleViewModel->GetEmitterViewModel(), &FNiagaraEmitterViewModel::GetParentNameText)
						.Visibility(this, &SNiagaraStackEmitterHeader::GetSourceEmitterNameVisibility) 
					]
				]
				// Issue Icon
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(0, 0, 2, 0)
				[
					SNew(SNiagaraStackIssueIcon, StackViewModel, InRootEntry)
					.Visibility(InArgs._IssueIconVisibility)
					.OnClicked(this, &SNiagaraStackEmitterHeader::OnIssueIconClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(0, 0, 2, 0)
				[
					SAssignNew(SettingsAnchor, SMenuAnchor)
					.Placement(MenuPlacement_MenuLeft)
					.OnGetMenuContent(this, &SNiagaraStackEmitterHeader::OnGetContent)
					[
						SNew(SButton)
						.ForegroundColor(FSlateColor::UseForeground())
						.ButtonStyle(FEditorStyle::Get(), TEXT("FlatButton"))
						.OnClicked(this, &SNiagaraStackEmitterHeader::OpenSubmenu)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Fill)
						[
							SNew(STextBlock)
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.16"))
							.Text(FEditorFontGlyphs::Cogs)
						]
					]
				]
			]

			//~ Stats
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(2)
			[
				SNew(STextBlock)
				.Text(EmitterHandleViewModel->GetEmitterViewModel(), &FNiagaraEmitterViewModel::GetStatsText)
			]
		];
	}

	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
	{
		if (EmitterHandleViewModel->GetIsRenamePending())
		{
			EmitterHandleViewModel->SetIsRenamePending(false);
			EmitterNameTextBlock->EnterEditingMode();
		}
	}

private:
	FText GetEmitterNameToolTip() const
	{
		if (EmitterHandleViewModel->GetEmitterViewModel()->HasParentEmitter())
		{
			// We are looking at this Emitter in a System Asset and it has a valid parent Emitter
			return FText::Format(LOCTEXT("EmitterNameAndPath", "{0}\nParent: {1}"), EmitterHandleViewModel->GetNameText(), EmitterHandleViewModel->GetEmitterViewModel()->GetParentPathNameText());
		}
		else
		{
			// We are looking at this Emitter in an Emitter Asset or we are looking at this Emitter in a System Asset and it does not have a valid parent Emitter
			return EmitterHandleViewModel->GetNameText();
		}
	}

	EVisibility GetSourceEmitterNameVisibility() const
	{
		bool bIsRenamed = false;
		if (EmitterHandleViewModel->GetEmitterViewModel()->HasParentEmitter())
		{
			const FText CurrentNameText = EmitterHandleViewModel->GetNameText();
			const FText ParentNameText = EmitterHandleViewModel->GetEmitterViewModel()->GetParentNameText();
			bIsRenamed = CurrentNameText.EqualTo(ParentNameText) == false;
		}
		return bIsRenamed ? EVisibility::Visible : EVisibility::Collapsed;
	}

	EVisibility GetOpenSourceEmitterVisibility() const
	{
		return EmitterHandleViewModel->GetEmitterViewModel()->GetEmitter()->GetParent() != nullptr ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FReply OnIssueIconClicked() const
	{
		StackViewModel->OnCycleThroughIssues(TopLevelViewModel);
		OnCycleThroughIssues.ExecuteIfBound();
		return FReply::Handled();
	}

	FReply OpenSubmenu()
	{
		SettingsAnchor->SetIsOpen(!SettingsAnchor->IsOpen());
		return FReply::Handled();
	}

	TSharedRef<SWidget> OnGetContent() const
	{
		if (ParentStackPtr.IsValid())
		{
			return ParentStackPtr.Pin()->GenerateStackMenu(TopLevelViewModel).ToSharedRef();
		}
		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel;
	TSharedPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModel;

	TSharedPtr<SInlineEditableTextBlock> EmitterNameTextBlock;
	FSimpleDelegate OnCycleThroughIssues;
	UNiagaraStackViewModel* StackViewModel;
	TWeakPtr<SNiagaraStack> ParentStackPtr;
	TSharedPtr<SMenuAnchor> SettingsAnchor;
};

const float SpacerHeight = 6;

void SNiagaraStack::Construct(const FArguments& InArgs, UNiagaraStackViewModel* InStackViewModel)
{
	StackViewModel = InStackViewModel;
	StackViewModel->OnStructureChanged().AddSP(this, &SNiagaraStack::StackStructureChanged);
	StackViewModel->OnExpansionChanged().AddSP(this, &SNiagaraStack::OnStackExpansionChanged);
	StackViewModel->OnSearchCompleted().AddSP(this, &SNiagaraStack::OnStackSearchComplete); 
	NameColumnWidth = .3f;
	ContentColumnWidth = .7f;
	StackCommandContext = MakeShared<FNiagaraStackCommandContext>();
	bSynchronizeExpansionPending = true;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(1, 1, 1, 4)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(5)
			[
				ConstructHeaderWidget()
			]
		]
		+ SVerticalBox::Slot()
		.Padding(1)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.BackgroundColor"))
			[
				SAssignNew(StackTree, STreeView<UNiagaraStackEntry*>)
				.OnGenerateRow(this, &SNiagaraStack::OnGenerateRowForStackItem)
				.OnGetChildren(this, &SNiagaraStack::OnGetChildren)
				.TreeItemsSource(&StackViewModel->GetRootEntryAsArray())
				.OnTreeViewScrolled(this, &SNiagaraStack::StackTreeScrolled)
				.OnSelectionChanged(this, &SNiagaraStack::StackTreeSelectionChanged)
				.SelectionMode(ESelectionMode::Multi)
				.OnItemToString_Debug_Static(&FNiagaraStackEditorWidgetsUtilities::StackEntryToStringForListDebug)
			]
		]
	];

	StackTree->SetScrollOffset(StackViewModel->GetLastScrollPosition());

	SynchronizeTreeExpansion();
}

void SNiagaraStack::SynchronizeTreeExpansion()
{
	bSynchronizeExpansionPending = false;
	TArray<UNiagaraStackEntry*> EntriesToProcess(StackViewModel->GetRootEntryAsArray());
	while (EntriesToProcess.Num() > 0)
	{
		UNiagaraStackEntry* EntryToProcess = EntriesToProcess[0];
		EntriesToProcess.RemoveAtSwap(0);

		if (EntryToProcess->GetIsExpanded())
		{
			StackTree->SetItemExpansion(EntryToProcess, true);
			EntryToProcess->GetFilteredChildren(EntriesToProcess);
		}
		else
		{
			StackTree->SetItemExpansion(EntryToProcess, false);
		}
	}
}

TSharedRef<SWidget> SNiagaraStack::ConstructHeaderWidget()
{
	const FVector2D ViewOptionsShadowOffset = FNiagaraEditorWidgetsStyle::Get().GetVector("NiagaraEditor.Stack.ViewOptionsShadowOffset");

	return SNew(SVerticalBox)
		//~ Top level object list view
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		[
			SAssignNew(HeaderList, SListView<TSharedRef<UNiagaraStackViewModel::FTopLevelViewModel>>)
			.ListItemsSource(&StackViewModel->GetTopLevelViewModels())
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SNiagaraStack::OnGenerateRowForTopLevelObject)
		]
		
		//~ Search, view options
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(2, 4, 2, 4)
		[
			// Search box
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("StackSearchBoxHint", "Search the stack"))
				.SearchResultData(this, &SNiagaraStack::GetSearchResultData)
				.IsSearching(this, &SNiagaraStack::GetIsSearching)
				.OnTextChanged(this, &SNiagaraStack::OnSearchTextChanged)
				.DelayChangeNotificationsWhileTyping(true)
				.OnTextCommitted(this, &SNiagaraStack::OnSearchBoxTextCommitted)
				.OnSearch(this, &SNiagaraStack::OnSearchBoxSearch)
			]
			// View options
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(this, &SNiagaraStack::GetViewOptionsIconBrush)
				[
					SNew(SComboButton)
					.ComboButtonStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.ViewOptionsButton")
					.ForegroundColor(FSlateColor::UseForeground())
					.ToolTipText(LOCTEXT("ViewOptionsToolTip", "View Options"))
					.OnGetMenuContent(this, &SNiagaraStack::GetViewOptionsMenu)
					.ContentPadding(1)
					.MenuPlacement(MenuPlacement_BelowRightAnchor)
					.ButtonContent()
					[
						SNew(SOverlay)
						// drop shadow
						+ SOverlay::Slot()
						.VAlign(VAlign_Top)
						.Padding(ViewOptionsShadowOffset.X, ViewOptionsShadowOffset.Y, 0, 0)
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("GenericViewButton"))
							.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.ViewOptionsShadowColor"))
						]
						+ SOverlay::Slot()
						.VAlign(VAlign_Top)
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("GenericViewButton"))
							.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
						]
					]
				]
			]
		];
}

const FSlateBrush* SNiagaraStack::GetViewOptionsIconBrush() const
{
	bool bIsDefault = StackViewModel->GetShowAllAdvanced() == false &&
		StackViewModel->GetShowLinkedInputs() == false &&
		StackViewModel->GetShowOutputs() == false &&
		StackViewModel->GetShowOnlyIssues() == false;

	return bIsDefault ? 
		FCoreStyle::Get().GetBrush("NoBrush") :
		FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.HighlightedButtonBrush");
}

void SNiagaraStack::OnSearchTextChanged(const FText& SearchText)
{
	StackViewModel->OnSearchTextChanged(SearchText);
}

static void ExpandAllInPath(const TArray<UNiagaraStackEntry*>& EntryPath)
{
	for (UNiagaraStackEntry* Entry : EntryPath)
	{
		if (!Entry->IsA<UNiagaraStackRoot>())
		{
			Entry->SetIsExpanded(true);
		}
	}
}

FReply SNiagaraStack::ScrollToNextMatch()
{
	int NextMatchIndex = StackViewModel->GetCurrentFocusedMatchIndex() + 1;
	TArray<UNiagaraStackViewModel::FSearchResult> CurrentSearchResults = StackViewModel->GetCurrentSearchResults();
	if (CurrentSearchResults.Num() != 0)
	{
		if (NextMatchIndex >= CurrentSearchResults.Num())
		{
			NextMatchIndex = 0;
		}

		ExpandAllInPath(CurrentSearchResults[NextMatchIndex].EntryPath);
	}

	AddSearchScrollOffset(1);
	return FReply::Handled();
}

FReply SNiagaraStack::ScrollToPreviousMatch()
{
	const int PreviousMatchIndex = StackViewModel->GetCurrentFocusedMatchIndex() - 1;
	TArray<UNiagaraStackViewModel::FSearchResult> CurrentSearchResults = StackViewModel->GetCurrentSearchResults();
	if (CurrentSearchResults.Num() != 0)
	{
		if (PreviousMatchIndex >= 0)
		{
			ExpandAllInPath(CurrentSearchResults[PreviousMatchIndex].EntryPath);
		}
		else
		{
			ExpandAllInPath(CurrentSearchResults.Last().EntryPath);
		}
	}

	// move current match to the previous one in the StackTree, wrap around
	AddSearchScrollOffset(-1);
	return FReply::Handled();
}

void SNiagaraStack::AddSearchScrollOffset(int NumberOfSteps)
{
	if (StackViewModel->IsSearching() || StackViewModel->GetCurrentSearchResults().Num() == 0 || NumberOfSteps == 0)
	{
		return;
	}

	StackViewModel->AddSearchScrollOffset(NumberOfSteps);

	StackTree->RequestScrollIntoView(StackViewModel->GetCurrentFocusedEntry());
}

TOptional<SSearchBox::FSearchResultData> SNiagaraStack::GetSearchResultData() const
{
	if (StackViewModel->GetCurrentSearchText().IsEmpty())
	{
		return TOptional<SSearchBox::FSearchResultData>();
	}
	return TOptional<SSearchBox::FSearchResultData>({ StackViewModel->GetCurrentSearchResults().Num(), StackViewModel->GetCurrentFocusedMatchIndex() + 1 });
}

bool SNiagaraStack::GetIsSearching() const
{
	return StackViewModel->IsSearching();
}

bool SNiagaraStack::IsEntryFocusedInSearch(UNiagaraStackEntry* Entry) const
{
	if (StackViewModel && Entry && StackViewModel->GetCurrentFocusedEntry() == Entry)
	{
		return true;
	}
	return false;
}

void SNiagaraStack::ShowEmitterInContentBrowser(TWeakPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModelWeak)
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();
	if (EmitterHandleViewModel.IsValid())
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> Assets;
		if (EmitterHandleViewModel->GetEmitterViewModel()->HasParentEmitter())
		{
			Assets.Add(FAssetData(EmitterHandleViewModel->GetEmitterViewModel()->GetParentEmitter()));
			ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
		}
	}
}

void CollapseEntriesRecursive(TArray<UNiagaraStackEntry*> Entries)
{
	for (UNiagaraStackEntry* Entry : Entries)
	{
		if (Entry->GetCanExpand())
		{
			Entry->SetIsExpanded(false);
		}
		
		TArray<UNiagaraStackEntry*> Children;
		Entry->GetUnfilteredChildren(Children);
		CollapseEntriesRecursive(Children);
	}
}

void SNiagaraStack::CollapseAll()
{
	CollapseEntriesRecursive(StackViewModel->GetRootEntryAsArray());
}

TSharedRef<SWidget> SNiagaraStack::GetViewOptionsMenu() const
{
	FMenuBuilder MenuBuilder(false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowAllAdvancedLabel", "Show All Advanced"),
		LOCTEXT("ShowAllAdvancedToolTip", "Forces all advanced items to be showing in the stack."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]() { StackViewModel->SetShowAllAdvanced(!StackViewModel->GetShowAllAdvanced()); }),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([=]() { return StackViewModel->GetShowAllAdvanced() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })),
		NAME_None, EUserInterfaceActionType::Check);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowParameterReadsLabel", "Show Parameter Reads"),
		LOCTEXT("ShowParameterReadsToolTip", "Whether or not to show the parameters that a module reads from."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]() { StackViewModel->SetShowLinkedInputs(!StackViewModel->GetShowLinkedInputs()); }),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([=]() { return StackViewModel->GetShowLinkedInputs() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })),
		NAME_None, EUserInterfaceActionType::Check);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowParameterWritesLabel", "Show Parameter Writes"),
		LOCTEXT("ShowParameterWritesToolTip", "Whether or not to show parameters that a module writes to."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]() { StackViewModel->SetShowOutputs(!StackViewModel->GetShowOutputs()); }),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([=]() { return StackViewModel->GetShowOutputs() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })),
		NAME_None, EUserInterfaceActionType::Check);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ShowIssuesLabel", "Show Only Issues"),
		LOCTEXT("ShowIssuesToolTip", "Hides all modules except those that have unresolved issues."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]() { StackViewModel->SetShowOnlyIssues(!StackViewModel->GetShowOnlyIssues()); }),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([=]() { return StackViewModel->GetShowOnlyIssues() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })),
		NAME_None, EUserInterfaceActionType::Check);

	return MenuBuilder.MakeWidget();
}

FReply SNiagaraStack::OnRowDragDetected(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent, UNiagaraStackEntry* InStackEntry)
{
	if (InStackEntry->CanDrag())
	{
		TArray<UNiagaraStackEntry*> DraggedEntries;
		DraggedEntries.Add(InStackEntry);
		return FReply::Handled().BeginDragDrop(FNiagaraStackEditorWidgetsUtilities::ConstructDragDropOperationForStackEntries(DraggedEntries));
	}
	return FReply::Unhandled();
}

void  SNiagaraStack::OnRowDragLeave(FDragDropEvent const& InDragDropEvent)
{
	FNiagaraStackEditorWidgetsUtilities::HandleDragLeave(InDragDropEvent);
}

TOptional<EItemDropZone> SNiagaraStack::OnRowCanAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry)
{
	return FNiagaraStackEditorWidgetsUtilities::RequestDropForStackEntry(InDragDropEvent, InDropZone, InTargetEntry, UNiagaraStackEntry::EDropOptions::None);
}

FReply SNiagaraStack::OnRowAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry)
{
	bool bHandled = FNiagaraStackEditorWidgetsUtilities::HandleDropForStackEntry(InDragDropEvent, InDropZone, InTargetEntry, UNiagaraStackEntry::EDropOptions::None);
	return bHandled ? FReply::Handled() : FReply::Unhandled();
}

void SNiagaraStack::OnStackSearchComplete()
{
	ExpandSearchResults();
	ScrollToNextMatch();
}

void SNiagaraStack::ExpandSearchResults()
{
	for (auto SearchResult : StackViewModel->GetCurrentSearchResults())
	{
		ExpandAllInPath(SearchResult.EntryPath);
	}

	bSynchronizeExpansionPending = true;
}

void SNiagaraStack::OnSearchBoxTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter) // hasn't been autojumped yet or we hit enter
	{
		AddSearchScrollOffset(+1);
	}
}

void SNiagaraStack::OnSearchBoxSearch(SSearchBox::SearchDirection Direction)
{
	if (Direction == SSearchBox::Next)
	{
		ScrollToNextMatch();
	}
	else if (Direction == SSearchBox::Previous)
	{
		ScrollToPreviousMatch();
	}
}

FSlateColor SNiagaraStack::GetTextColorForItem(UNiagaraStackEntry* Item) const
{
	if (IsEntryFocusedInSearch(Item))
	{
		return FSlateColor(FLinearColor(FColor::Orange));
	}
	return FSlateColor::UseForeground();
}

TSharedRef<ITableRow> SNiagaraStack::OnGenerateRowForStackItem(UNiagaraStackEntry* Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SNiagaraStackTableRow> Container = ConstructContainerForItem(Item);
	FRowWidgets RowWidgets = ConstructNameAndValueWidgetsForItem(Item, Container);
	Container->SetNameAndValueContent(RowWidgets.NameWidget, RowWidgets.ValueWidget);
	return Container;
}

TSharedRef<ITableRow> SNiagaraStack::OnGenerateRowForTopLevelObject(TSharedRef<UNiagaraStackViewModel::FTopLevelViewModel> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedPtr<SWidget> Content;
	if (Item->SystemViewModel.IsValid())
	{
		Content = SNew(SHorizontalBox)
			// System name
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.HeadingTextBlock")
				.Text(Item->SystemViewModel.ToSharedRef(), &FNiagaraSystemViewModel::GetDisplayName)
			]
			// Issue Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 2, 0)
			[
				SNew(SNiagaraStackIssueIcon, StackViewModel, Item->RootEntry.Get())
				.Visibility(this, &SNiagaraStack::GetIssueIconVisibility)
				.OnClicked(this, &SNiagaraStack::OnCycleThroughSystemIssues, Item->SystemViewModel)
			];
	}
	else if (Item->EmitterHandleViewModel.IsValid())
	{
		Content = SNew(SNiagaraStackEmitterHeader, Item->EmitterHandleViewModel.ToSharedRef(), Item->RootEntry.Get(), StackViewModel)
			.IssueIconVisibility(this, &SNiagaraStack::GetIssueIconVisibility)
			.OnCycleThroughIssues(this, &SNiagaraStack::OnCycleThroughIssues)
			.ParentStack(SharedThis(this));
	}

	return SNew(STableRow<TSharedRef<UNiagaraStackViewModel::FTopLevelViewModel>>, OwnerTable)
		[
			Content.ToSharedRef()
		];
}

TSharedPtr<SWidget> SNiagaraStack::GenerateStackMenu(TWeakPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModelWeak)
{
	TSharedPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModel = TopLevelViewModelWeak.Pin();
	if (TopLevelViewModel.IsValid())
	{
		TSharedPtr<FUICommandList> GraphCommandList = TopLevelViewModel->RootEntry->GetSystemViewModel()->GetOverviewGraphViewModel()->GetCommands();
		FMenuBuilder MenuBuilder(true, GraphCommandList);

		FNiagaraEditorUtilities::AddEmitterContextMenuActions(MenuBuilder, TopLevelViewModel->EmitterHandleViewModel);

		{
			MenuBuilder.BeginSection("EmitterEditSection", LOCTEXT("Edit", "Edit"));

			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);

			MenuBuilder.EndSection();
		}
		MenuBuilder.BeginSection("StackActions", LOCTEXT("StackActions", "Stack Actions"));
		{
			if (StackViewModel->HasDismissedStackIssues())
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("UndismissIssues", "Undismiss All Stack Issues"),
					LOCTEXT("ShowAssetInContentBrowserToolTip", "Undismiss all issues that were previously dismissed for this stack, if any"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateUObject(StackViewModel, &UNiagaraStackViewModel::UndismissAllIssues)));
			}

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CollapseStack", "Collapse All"),
				LOCTEXT("CollapseStackToolTip", "Collapses every row in the stack."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SNiagaraStack::CollapseAll)));

			TSharedPtr<FUICommandInfo> CollapseToHeadersCommand = FNiagaraEditorModule::Get().Commands().CollapseStackToHeaders;
			MenuBuilder.AddMenuEntry(
				CollapseToHeadersCommand->GetLabel(),
				CollapseToHeadersCommand->GetDescription(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateUObject(StackViewModel, &UNiagaraStackViewModel::CollapseToHeaders)));
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}
	return SNullWidget::NullWidget;
}

TSharedRef<SNiagaraStackTableRow> SNiagaraStack::ConstructContainerForItem(UNiagaraStackEntry* Item)
{
	float LeftContentPadding = 4;
	float RightContentPadding = 6;
	TAttribute<FMargin> RowPadding = FMargin(0, 0, 0, 0);
	FMargin ContentPadding(LeftContentPadding, 0, RightContentPadding, 0);
	FLinearColor ItemBackgroundColor;
	FLinearColor ItemForegroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.ForegroundColor");
	bool bIsCategoryIconHighlighted;
	bool bShowExecutionCategoryIcon;
	switch (Item->GetStackRowStyle())
	{
	case UNiagaraStackEntry::EStackRowStyle::None:
		ItemBackgroundColor = FLinearColor::Transparent;
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	case UNiagaraStackEntry::EStackRowStyle::GroupHeader:
		ContentPadding = FMargin(LeftContentPadding, 3, 0, 3);
		ItemBackgroundColor = FLinearColor::Transparent;
		ItemForegroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.GroupForegroundColor");
		bIsCategoryIconHighlighted = true;
		bShowExecutionCategoryIcon = true;
		break;
	case UNiagaraStackEntry::EStackRowStyle::GroupFooter:
		RowPadding = FMargin(0, 0, 0, 6);
		ItemBackgroundColor = FLinearColor::Transparent;
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	case UNiagaraStackEntry::EStackRowStyle::ItemHeader:
		RowPadding.Bind(TAttribute<FMargin>::FGetter::CreateLambda([Item]()
		{
			if (Item->GetIsExpanded())
			{
				return FMargin(0, 2, 0, 0);
			}
			else
			{
				return FMargin(0, 2, 0, 2);
			}
		}));
		ContentPadding = FMargin(LeftContentPadding, 2, 2, 2);
		ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.HeaderBackgroundColor");
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = true;
		break;
	case UNiagaraStackEntry::EStackRowStyle::ItemContent:
		ContentPadding = FMargin(LeftContentPadding, 3, RightContentPadding, 3);
		ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.ContentBackgroundColor");
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	case UNiagaraStackEntry::EStackRowStyle::ItemContentAdvanced:
		ContentPadding = FMargin(LeftContentPadding, 3, RightContentPadding, 3);
		ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.ContentAdvancedBackgroundColor");
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	case UNiagaraStackEntry::EStackRowStyle::ItemFooter:
		RowPadding = FMargin(0, 0, 0, 2);
		ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.FooterBackgroundColor");
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	case UNiagaraStackEntry::EStackRowStyle::ItemCategory:
		ContentPadding = FMargin(LeftContentPadding, 3, RightContentPadding, 3);
		ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.ContentBackgroundColor");
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	case UNiagaraStackEntry::EStackRowStyle::StackIssue:

		switch (Item->GetIssueSeverity())
		{
			case EStackIssueSeverity::Error:
				ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.ErrorBackgroundColor");
				break;
			case EStackIssueSeverity::Warning:
				ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.WarningBackgroundColor");
				break;
			case EStackIssueSeverity::Info:
				ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.InfoBackgroundColor");
				break;
			case EStackIssueSeverity::CustomNote:
				ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.CustomNoteBackgroundColor");
				break;
			default:
				checkf(false, TEXT("Issue severity not set for stack issue."));
		}

		ContentPadding = FMargin(LeftContentPadding, 3, RightContentPadding, 3);
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	default:
		ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.UnknownColor");
		bIsCategoryIconHighlighted = false;
		bShowExecutionCategoryIcon = false;
		break;
	}

	return SNew(SNiagaraStackTableRow, StackViewModel, Item, StackCommandContext.ToSharedRef(), StackTree.ToSharedRef())
		.RowPadding(RowPadding)
		.ContentPadding(ContentPadding)
		.ItemBackgroundColor(ItemBackgroundColor)
		.ItemForegroundColor(ItemForegroundColor)
		.IsCategoryIconHighlighted(bIsCategoryIconHighlighted)
		.ShowExecutionCategoryIcon(bShowExecutionCategoryIcon)
		.NameColumnWidth(this, &SNiagaraStack::GetNameColumnWidth)
		.OnNameColumnWidthChanged(this, &SNiagaraStack::OnNameColumnWidthChanged)
		.ValueColumnWidth(this, &SNiagaraStack::GetContentColumnWidth)
		.OnValueColumnWidthChanged(this, &SNiagaraStack::OnContentColumnWidthChanged)
		.OnDragDetected(this, &SNiagaraStack::OnRowDragDetected, Item)
		.OnDragLeave(this, &SNiagaraStack::OnRowDragLeave)
		.OnCanAcceptDrop(this, &SNiagaraStack::OnRowCanAcceptDrop)
		.OnAcceptDrop(this, &SNiagaraStack::OnRowAcceptDrop)
		.IssueIconVisibility(this, &SNiagaraStack::GetIssueIconVisibility);
}


FReply SNiagaraStack::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (StackCommandContext->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SNiagaraStack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Update the stack view model and synchronize the expansion state before the parent tick to ensure that the state is up to date before ticking child widgets.
	if (StackViewModel)
	{
		StackViewModel->Tick();
	}
	if (bSynchronizeExpansionPending)
	{
		SynchronizeTreeExpansion();
	}

	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}


SNiagaraStack::FRowWidgets SNiagaraStack::ConstructNameAndValueWidgetsForItem(UNiagaraStackEntry* Item, TSharedRef<SNiagaraStackTableRow> Container)
{
	if (Item->IsA<UNiagaraStackItemGroup>())
	{
		return FRowWidgets(SNew(SNiagaraStackItemGroup, *CastChecked<UNiagaraStackItemGroup>(Item), StackViewModel));
	}
	else if (Item->IsA<UNiagaraStackModuleItem>())
	{
		TSharedRef<SNiagaraStackModuleItem> ModuleItemWidget = SNew(SNiagaraStackModuleItem, *CastChecked<UNiagaraStackModuleItem>(Item), StackViewModel);
		Container->AddFillRowContextMenuHandler(SNiagaraStackTableRow::FOnFillRowContextMenu::CreateSP(ModuleItemWidget, &SNiagaraStackModuleItem::FillRowContextMenu));
		return FRowWidgets(ModuleItemWidget);
	}
	else if (Item->IsA<UNiagaraStackFunctionInput>())
	{
		UNiagaraStackFunctionInput* FunctionInput = CastChecked<UNiagaraStackFunctionInput>(Item);

		TSharedRef<SNiagaraStackFunctionInputName> FunctionInputNameWidget =
			SNew(SNiagaraStackFunctionInputName, FunctionInput, StackViewModel)
			.IsSelected(Container, &SNiagaraStackTableRow::IsSelected);
		Container->AddFillRowContextMenuHandler(SNiagaraStackTableRow::FOnFillRowContextMenu::CreateSP(FunctionInputNameWidget, &SNiagaraStackFunctionInputName::FillRowContextMenu));

		return FRowWidgets(FunctionInputNameWidget,
			SNew(SNiagaraStackFunctionInputValue, FunctionInput));
	}
	else if (Item->IsA<UNiagaraStackErrorItem>())
	{
		return FRowWidgets(SNew(SNiagaraStackErrorItem, CastChecked<UNiagaraStackErrorItem>(Item), StackViewModel));
	}
	else if (Item->IsA<UNiagaraStackErrorItemLongDescription>())
	{
		Container->SetOverrideNameAlignment(EHorizontalAlignment::HAlign_Fill, EVerticalAlignment::VAlign_Center);
		return FRowWidgets(SNew(STextBlock)
			.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.ParameterText")
			.ToolTipText_UObject(Item, &UNiagaraStackEntry::GetTooltipText)
			.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
			.ColorAndOpacity(this, &SNiagaraStack::GetTextColorForItem, Item)
			.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
			.AutoWrapText(true));
	}
	else if (Item->IsA<UNiagaraStackErrorItemFix>())
	{
		return FRowWidgets(SNew(SNiagaraStackErrorItemFix, CastChecked<UNiagaraStackErrorItemFix>(Item), StackViewModel));
	}
	else if (Item->IsA<UNiagaraStackItemFooter>())
	{
		UNiagaraStackItemFooter* ItemExpander = CastChecked<UNiagaraStackItemFooter>(Item);
		return FRowWidgets(SNew(SNiagaraStackItemFooter, *ItemExpander));
	}
	else if (Item->IsA<UNiagaraStackParameterStoreEntry>())
	{
		UNiagaraStackParameterStoreEntry* StackEntry = CastChecked<UNiagaraStackParameterStoreEntry>(Item);
		return FRowWidgets(
			SNew(SNiagaraStackParameterStoreEntryName, StackEntry, StackViewModel)
			.IsSelected(Container, &SNiagaraStackTableRow::IsSelected),
			SNew(SNiagaraStackParameterStoreEntryValue, StackEntry));
	}
	else if (Item->IsA<UNiagaraStackInputCategory>())
	{
		return FRowWidgets(SNew(STextBlock)
			.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.CategoryText")
			.ToolTipText_UObject(Item, &UNiagaraStackEntry::GetTooltipText)
			.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
			.ColorAndOpacity(this, &SNiagaraStack::GetTextColorForItem, Item)
			.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
			.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetOwnerIsEnabled),
			SNullWidget::NullWidget);
	}
	else if (Item->IsA<UNiagaraStackModuleItemOutput>())
	{
		UNiagaraStackModuleItemOutput* ModuleItemOutput = CastChecked<UNiagaraStackModuleItemOutput>(Item);
		return FRowWidgets(
			SNew(SNiagaraParameterName)
				.ReadOnlyTextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.DefaultText")
				.ToolTipText_UObject(Item, &UNiagaraStackEntry::GetTooltipText)
				.ParameterName(ModuleItemOutput->GetOutputParameterHandle().GetParameterHandleString())
				.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetOwnerIsEnabled)
				.IsReadOnly(true)
			);
	}
	else if (Item->IsA<UNiagaraStackFunctionInputCollection>() ||
		Item->IsA<UNiagaraStackModuleItemOutputCollection>() ||
		Item->IsA<UNiagaraStackModuleItemLinkedInputCollection>())
	{
		return FRowWidgets(
			SNew(STextBlock)
			.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.DefaultText")
			.ToolTipText_UObject(Item, &UNiagaraStackEntry::GetTooltipText)
			.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
			.ColorAndOpacity(this, &SNiagaraStack::GetTextColorForItem, Item)
			.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
			.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetOwnerIsEnabled),
			SNullWidget::NullWidget);
	}
	else if (Item->IsA<UNiagaraStackPropertyRow>())
	{
		UNiagaraStackPropertyRow* PropertyRow = CastChecked<UNiagaraStackPropertyRow>(Item);
		FNodeWidgets PropertyRowWidgets = PropertyRow->GetDetailTreeNode()->CreateNodeWidgets();

		TAttribute<bool> IsEnabled;
		IsEnabled.BindUObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled);

		Container->AddFillRowContextMenuHandler(FNiagaraStackPropertyRowUtilities::CreateOnFillRowContextMenu(PropertyRow->GetDetailTreeNode()->CreatePropertyHandle(), PropertyRowWidgets.Actions));

		if (PropertyRowWidgets.WholeRowWidget.IsValid())
		{
			Container->SetOverrideNameWidth(PropertyRowWidgets.WholeRowWidgetLayoutData.MinWidth, PropertyRowWidgets.WholeRowWidgetLayoutData.MaxWidth);
			Container->SetOverrideNameAlignment(PropertyRowWidgets.WholeRowWidgetLayoutData.HorizontalAlignment, PropertyRowWidgets.WholeRowWidgetLayoutData.VerticalAlignment);
			PropertyRowWidgets.WholeRowWidget->SetEnabled(IsEnabled);
			return FRowWidgets(PropertyRowWidgets.WholeRowWidget.ToSharedRef());
		}
		else
		{
			Container->SetOverrideNameWidth(PropertyRowWidgets.NameWidgetLayoutData.MinWidth, PropertyRowWidgets.NameWidgetLayoutData.MaxWidth);
			Container->SetOverrideNameAlignment(PropertyRowWidgets.NameWidgetLayoutData.HorizontalAlignment, PropertyRowWidgets.NameWidgetLayoutData.VerticalAlignment);
			Container->SetOverrideValueWidth(PropertyRowWidgets.ValueWidgetLayoutData.MinWidth, PropertyRowWidgets.ValueWidgetLayoutData.MaxWidth);
			Container->SetOverrideValueAlignment(PropertyRowWidgets.ValueWidgetLayoutData.HorizontalAlignment, PropertyRowWidgets.ValueWidgetLayoutData.VerticalAlignment);
			PropertyRowWidgets.NameWidget->SetEnabled(IsEnabled);
			PropertyRowWidgets.ValueWidget->SetEnabled(IsEnabled);
			return FRowWidgets(PropertyRowWidgets.NameWidget.ToSharedRef(), PropertyRowWidgets.ValueWidget.ToSharedRef());
		}
	}
	else if (Item->IsA<UNiagaraStackItem>())
	{
		UNiagaraStackItem* StackItem = CastChecked<UNiagaraStackItem>(Item);
		return FRowWidgets(SNew(SNiagaraStackItem, *StackItem, StackViewModel));
	}
	else if (Item->IsA<UNiagaraStackItemTextContent>())
	{
		Container->SetContentPadding(FMargin(5));
		UNiagaraStackItemTextContent* ItemTextContent = CastChecked<UNiagaraStackItemTextContent>(Item);
		return FRowWidgets(SNew(STextBlock)
			.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.TextContentText")
			.Text(ItemTextContent->GetDisplayName())
			.AutoWrapText(true)
			.Justification(ETextJustify::Center));
	}
	else
	{
		return FRowWidgets(SNullWidget::NullWidget);
	}
}

void SNiagaraStack::OnGetChildren(UNiagaraStackEntry* Item, TArray<UNiagaraStackEntry*>& Children)
{
	Item->GetFilteredChildren(Children);
}

void SNiagaraStack::StackTreeScrolled(double ScrollValue)
{
	StackViewModel->SetLastScrollPosition(ScrollValue);
}

void SNiagaraStack::StackTreeSelectionChanged(UNiagaraStackEntry* InNewSelection, ESelectInfo::Type SelectInfo)
{
	TArray<UNiagaraStackEntry*> SelectedStackEntries;
	StackTree->GetSelectedItems(SelectedStackEntries);
	StackCommandContext->SetSelectedEntries(SelectedStackEntries);
}

float SNiagaraStack::GetNameColumnWidth() const
{
	return NameColumnWidth;
}

float SNiagaraStack::GetContentColumnWidth() const
{
	return ContentColumnWidth;
}

void SNiagaraStack::OnNameColumnWidthChanged(float Width)
{
	NameColumnWidth = Width;
}

void SNiagaraStack::OnContentColumnWidthChanged(float Width)
{
	ContentColumnWidth = Width;
}

void SNiagaraStack::OnStackExpansionChanged()
{
	bSynchronizeExpansionPending = true;
}

void SNiagaraStack::StackStructureChanged(ENiagaraStructureChangedFlags Flags)
{
	bSynchronizeExpansionPending = true;
	StackTree->RequestTreeRefresh();
	HeaderList->RequestListRefresh();
}

EVisibility SNiagaraStack::GetIssueIconVisibility() const
{
	return StackViewModel->HasIssues() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SNiagaraStack::OnCycleThroughSystemIssues(TSharedPtr<FNiagaraSystemViewModel> SystemViewModel)
{
	UNiagaraStackEntry* RootEntry = SystemViewModel->GetSystemStackViewModel()->GetRootEntry();
	if (RootEntry != nullptr)
	{
		TSharedPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModel = StackViewModel->GetTopLevelViewModelForEntry(*RootEntry);
		StackViewModel->OnCycleThroughIssues(TopLevelViewModel);
		OnCycleThroughIssues();
	}

	return FReply::Handled();
}

void SNiagaraStack::OnCycleThroughIssues()
{
	UNiagaraStackEntry* StackEntry = StackViewModel->GetCurrentFocusedIssue();

	TArray<UNiagaraStackEntry*> EntryPath;
	StackViewModel->GetPathForEntry(StackEntry, EntryPath);
	EntryPath.Add(StackEntry);

	ExpandAllInPath(EntryPath);
	SynchronizeTreeExpansion();

	StackTree->RequestScrollIntoView(StackEntry);
}

#undef LOCTEXT_NAMESPACE
