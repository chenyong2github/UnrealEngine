// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraScratchPad.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"
#include "Widgets/SDynamicLayoutBox.h"
#include "Widgets/SItemSelector.h"
#include "Widgets/SNiagaraSelectedObjectsDetails.h"
#include "Widgets/SVerticalResizeBox.h"
#include "Widgets/SNiagaraScriptGraph.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraScratchPadCommandContext.h"

#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"

#define LOCTEXT_NAMESPACE "NiagaraScratchPad"

FName ScriptSelectorName = "ScriptSelector";
FName ScriptEditorName = "ScriptEditor";
FName SelectionEditorName = "SelectionEditor";
FName WideLayoutName = "Wide";
FName NarrowLayoutName = "Narrow";

class SNiagaraScratchPadScriptRow : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPadScriptRow) {}
		SLATE_ATTRIBUTE(bool, IsSelected);
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		UNiagaraScratchPadViewModel* InScratchPadViewModel,
		TSharedRef<FNiagaraScratchPadScriptViewModel> InScriptViewModel,
		TSharedPtr<FNiagaraScratchPadCommandContext> InCommandContext)
	{
		ScratchPadViewModel = InScratchPadViewModel;
		ScriptViewModel = InScriptViewModel;
		CommandContext = InCommandContext;
		IsSelected = InArgs._IsSelected;

		ChildSlot
		[
			SAssignNew(NameEditableText, SInlineEditableTextBlock)
			.Text(this, &SNiagaraScratchPadScriptRow::GetNameText)
			.IsSelected(this, &SNiagaraScratchPadScriptRow::GetIsSelected)
			.OnTextCommitted(this, &SNiagaraScratchPadScriptRow::OnNameTextCommitted)
		];
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		if (ScriptViewModel->GetIsPendingRename())
		{
			ScriptViewModel->SetIsPendingRename(false);
			NameEditableText->EnterEditingMode();
		}
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			return FReply::Handled()
				.CaptureMouse(SharedThis(this));
		}
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			// Set this script to be the active one.
			ScratchPadViewModel->SetActiveScript(ScriptViewModel->GetScript());

			FMenuBuilder MenuBuilder(true, CommandContext->GetCommands());
			CommandContext->AddEditMenuItems(MenuBuilder);

			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
			return FReply::Handled().ReleaseMouseCapture();
		}
		return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

private:
	FText GetNameText() const
	{
		return ScriptViewModel->GetDisplayName();
	}

	bool GetIsSelected() const
	{
		return IsSelected.Get(false);
	}

	void OnNameTextCommitted(const FText& InText, ETextCommit::Type InCommitType)
	{
		ScriptViewModel->SetScriptName(InText);
	}

private:
	UNiagaraScratchPadViewModel* ScratchPadViewModel;
	TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel;
	TSharedPtr<FNiagaraScratchPadCommandContext> CommandContext;
	TAttribute<bool> IsSelected;
	TSharedPtr<SInlineEditableTextBlock> NameEditableText;
};

class SNiagaraScratchPadScriptSelector : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPadScriptSelector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UNiagaraScratchPadViewModel* InViewModel, TSharedPtr<FNiagaraScratchPadCommandContext> InCommandContext)
	{
		ViewModel = InViewModel;
		CommandContext = InCommandContext;
		ViewModel->OnScriptViewModelsChanged().AddSP(this, &SNiagaraScratchPadScriptSelector::ScriptViewModelsChanged);
		ViewModel->OnActiveScriptChanged().AddSP(this, &SNiagaraScratchPadScriptSelector::ActiveScriptChanged);
		bIsUpdatingSelection = false;

		ChildSlot
		[
			SAssignNew(ScriptSelector, SNiagaraScriptViewModelSelector)
			.ClickActivateMode(EItemSelectorClickActivateMode::SingleClick)
			.Items(ViewModel->GetScriptViewModels())
			.DefaultCategories(ViewModel->GetAvailableUsages())
			.OnGetCategoriesForItem(this, &SNiagaraScratchPadScriptSelector::OnGetCategoriesForItem)
			.OnCompareCategoriesForEquality(this, &SNiagaraScratchPadScriptSelector::OnCompareCategoriesForEquality)
			.OnCompareCategoriesForSorting(this, &SNiagaraScratchPadScriptSelector::OnCompareCategoriesForSorting)
			.OnCompareItemsForEquality(this, &SNiagaraScratchPadScriptSelector::OnCompareItemsForEquality)
			.OnCompareItemsForSorting(this, &SNiagaraScratchPadScriptSelector::OnCompareItemsForSorting)
			.OnDoesItemMatchFilterText(this, &SNiagaraScratchPadScriptSelector::OnDoesItemMatchFilterText)
			.OnGenerateWidgetForCategory(this, &SNiagaraScratchPadScriptSelector::OnGenerateWidgetForCategory)
			.OnGenerateWidgetForItem(this, &SNiagaraScratchPadScriptSelector::OnGenerateWidgetForItem)
			.OnItemActivated(this, &SNiagaraScratchPadScriptSelector::OnScriptActivated)
			.OnSelectionChanged(this, &SNiagaraScratchPadScriptSelector::OnSelectionChanged)
		];
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (CommandContext->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

private:
	void ScriptViewModelsChanged()
	{
		if (ScriptSelector.IsValid())
		{
			ScriptSelector->RefreshItemsAndDefaultCategories(ViewModel->GetScriptViewModels(), ViewModel->GetAvailableUsages());
		}
	}

	void ActiveScriptChanged()
	{
		if (bIsUpdatingSelection == false)
		{
			TGuardValue<bool> UpdateGuard(bIsUpdatingSelection, true);
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ActiveScriptViewModel = ViewModel->GetViewModelForScript(ViewModel->GetActiveScript());
			if (ActiveScriptViewModel.IsValid())
			{
				TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> SelectedItems;
				SelectedItems.Add(ActiveScriptViewModel.ToSharedRef());
				ScriptSelector->SetSelectedItems(SelectedItems);
			}
			else
			{
				ScriptSelector->ClearSelectedItems();
			}
		}
	}

	TArray<ENiagaraScriptUsage> OnGetCategoriesForItem(const TSharedRef<FNiagaraScratchPadScriptViewModel>& Item)
	{
		TArray<ENiagaraScriptUsage> Categories;
		Categories.Add(Item->GetScripts()[0]->GetUsage());
		return Categories;
	}

	bool OnCompareCategoriesForEquality(const ENiagaraScriptUsage& CategoryA, const ENiagaraScriptUsage& CategoryB) const
	{
		return CategoryA == CategoryB;
	}

	bool OnCompareCategoriesForSorting(const ENiagaraScriptUsage& CategoryA, const ENiagaraScriptUsage& CategoryB) const
	{
		return ((int32)CategoryA) < ((int32)CategoryB);
	}

	bool OnCompareItemsForEquality(const TSharedRef<FNiagaraScratchPadScriptViewModel>& ItemA, const TSharedRef<FNiagaraScratchPadScriptViewModel>& ItemB) const
	{
		return ItemA == ItemB;
	}

	bool OnCompareItemsForSorting(const TSharedRef<FNiagaraScratchPadScriptViewModel>& ItemA, const TSharedRef<FNiagaraScratchPadScriptViewModel>& ItemB) const
	{
		return ItemA->GetDisplayName().CompareTo(ItemB->GetDisplayName()) < 0;
	}

	bool OnDoesItemMatchFilterText(const FText& FilterText, const TSharedRef<FNiagaraScratchPadScriptViewModel>& Item)
	{
		return Item->GetDisplayName().ToString().Find(FilterText.ToString(), ESearchCase::IgnoreCase) != INDEX_NONE;
	}

	TSharedRef<SWidget> OnGenerateWidgetForCategory(const ENiagaraScriptUsage& Category)
	{
		return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.ScratchPad.HeaderColor"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(5, 0, 0, 0)
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.SmallHeaderText")
				.Text(ViewModel->GetDisplayNameForUsage(Category))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &SNiagaraScratchPadScriptSelector::ScriptSelectorAddButtonClicked, Category)
				.Content()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AddButtonText", "Add"))
				]
			]
		];
	}

	TSharedRef<SWidget> OnGenerateWidgetForItem(const TSharedRef<FNiagaraScratchPadScriptViewModel>& Item)
	{
		return SNew(SNiagaraScratchPadScriptRow, ViewModel, Item, CommandContext)
		.IsSelected(this, &SNiagaraScratchPadScriptSelector::GetItemIsSelected, TWeakPtr<FNiagaraScratchPadScriptViewModel>(Item));
	}

	void OnScriptActivated(const TSharedRef<FNiagaraScratchPadScriptViewModel>& ActivatedScript)
	{
		if (bIsUpdatingSelection == false)
		{
			TGuardValue<bool> UpdateGuard(bIsUpdatingSelection, true);
			ViewModel->SetActiveScript(ActivatedScript->GetScript());
		}
	}

	void OnSelectionChanged()
	{
		if (bIsUpdatingSelection == false)
		{
			TGuardValue<bool> UpdateGuard(bIsUpdatingSelection, true);
			TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> SelectedScripts = ScriptSelector->GetSelectedItems();
			if (SelectedScripts.Num() == 0)
			{
				ViewModel->SetActiveScript(nullptr);
			}
			else if (SelectedScripts.Num())
			{
				ViewModel->SetActiveScript(SelectedScripts[0]->GetScript());
			}
		}
	}

	FReply ScriptSelectorAddButtonClicked(ENiagaraScriptUsage Usage)
	{
		TSharedPtr<FNiagaraScratchPadScriptViewModel> NewScriptViewModel = ViewModel->CreateNewScript(Usage, ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraTypeDefinition());
		if (NewScriptViewModel.IsValid())
		{
			NewScriptViewModel->SetIsPendingRename(true);
		}
		return FReply::Handled();
	}

	bool GetItemIsSelected(TWeakPtr<FNiagaraScratchPadScriptViewModel> ItemWeak) const
	{
		TSharedPtr<FNiagaraScratchPadScriptViewModel> Item = ItemWeak.Pin();
		return Item.IsValid() && ViewModel->GetActiveScript() == Item->GetScripts()[0];
	}

private:
	TSharedPtr<SNiagaraScriptViewModelSelector> ScriptSelector;
	UNiagaraScratchPadViewModel* ViewModel;
	TSharedPtr<FNiagaraScratchPadCommandContext> CommandContext;
	bool bIsUpdatingSelection;
};

class SNiagaraScratchPadScriptEditor : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPadScriptEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraScratchPadScriptViewModel> InScriptViewModel)
	{
		ScriptViewModel = InScriptViewModel;
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.LargeHeaderText")
					.Text(this, &SNiagaraScratchPadScriptEditor::GetNameText)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SNiagaraScratchPadScriptEditor::OnRefreshButtonClicked)
					.ToolTipText(LOCTEXT("RefreshButtonToolTip", "Refresh the inputs for this script in the selection stack."))
					.Content()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RefreshButtonText", "Refresh"))
					]
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SNiagaraScriptGraph, ScriptViewModel->GetGraphViewModel())
				.ZoomToFitOnLoad(true)
			]
		];
	}

private:
	FText GetNameText() const
	{
		return ScriptViewModel->GetDisplayName();
	}

	FReply OnRefreshButtonClicked()
	{
		TArray<UNiagaraNodeFunctionCall*> FunctionCallNodesToRefresh;
		for (TObjectIterator<UNiagaraNodeFunctionCall> It; It; ++It)
		{
			UNiagaraNodeFunctionCall* FunctionCallNode = *It;
			if (FunctionCallNode->FunctionScript == ScriptViewModel->GetScript())
			{
				FunctionCallNodesToRefresh.Add(FunctionCallNode);
			}
		}

		if (FunctionCallNodesToRefresh.Num())
		{
			for (TObjectIterator<UNiagaraStackFunctionInputCollection> It; It; ++It)
			{
				UNiagaraStackFunctionInputCollection* StackFunctionInputCollection = *It;
				if (StackFunctionInputCollection->IsFinalized() == false && FunctionCallNodesToRefresh.Contains(StackFunctionInputCollection->GetInputFunctionCallNode()))
				{
					StackFunctionInputCollection->RefreshChildren();
				}
			}
		}

		return FReply::Handled();
	}

private:
	TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel;
};

class SNiagaraScratchPadScriptEditorList : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPadScriptEditor) {}

	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraScratchPadViewModel* InViewModel)
	{
		ViewModel = InViewModel;
		ViewModel->OnScriptViewModelsChanged().AddSP(this, &SNiagaraScratchPadScriptEditorList::ScriptViewModelsChanged);
		ViewModel->OnActiveScriptChanged().AddSP(this, &SNiagaraScratchPadScriptEditorList::ActiveScriptChanged);
		bIsUpdatingSelection = false;

		ChildSlot
		[
			SAssignNew(ScriptEditors, SListView<TSharedRef<FNiagaraScratchPadScriptViewModel>>)
			.ListItemsSource(&ViewModel->GetScriptViewModels())
			.OnGenerateRow(this, &SNiagaraScratchPadScriptEditorList::OnGenerateScriptEditorRow)
			.OnSelectionChanged(this, &SNiagaraScratchPadScriptEditorList::OnSelectionChanged)
		];
	}

private:
	void ScriptViewModelsChanged()
	{
		if (ScriptEditors.IsValid())
		{
			ScriptEditors->RequestListRefresh();
		}
	}

	void ActiveScriptChanged()
	{
		if (bIsUpdatingSelection == false)
		{
			TGuardValue<bool> UpdateGuard(bIsUpdatingSelection, true);
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ActiveScriptViewModel = ViewModel->GetViewModelForScript(ViewModel->GetActiveScript());
			if (ActiveScriptViewModel.IsValid())
			{
				ScriptEditors->SetSelection(ActiveScriptViewModel.ToSharedRef());
			}
			else
			{
				ScriptEditors->ClearSelection();
			}
		}
	}

	TSharedRef<ITableRow> OnGenerateScriptEditorRow(TSharedRef<FNiagaraScratchPadScriptViewModel> Item, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return SNew(STableRow<TSharedRef<FNiagaraScratchPadScriptViewModel>>, OwnerTable)
		[
			SNew(SVerticalResizeBox)
			.ContentHeight(300)
			[
				SNew(SNiagaraScratchPadScriptEditor, Item)
			]
		];
	}

	void OnSelectionChanged(TSharedPtr<FNiagaraScratchPadScriptViewModel> InNewSelection, ESelectInfo::Type SelectInfo)
	{
		if (bIsUpdatingSelection == false)
		{
			TGuardValue<bool> UpdateGuard(bIsUpdatingSelection, true);
			TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> SelectedScripts;
			ScriptEditors->GetSelectedItems(SelectedScripts);
			if (SelectedScripts.Num() == 0)
			{
				ViewModel->SetActiveScript(nullptr);
			}
			else if (SelectedScripts.Num())
			{
				ViewModel->SetActiveScript(SelectedScripts[0]->GetScript());
			}
		}
	}

private:
	UNiagaraScratchPadViewModel* ViewModel;
	TSharedPtr<SListView<TSharedRef<FNiagaraScratchPadScriptViewModel>>> ScriptEditors;
	bool bIsUpdatingSelection;
};

void SNiagaraScratchPad::Construct(const FArguments& InArgs, UNiagaraScratchPadViewModel* InViewModel)
{
	ViewModel = InViewModel;
	CommandContext = MakeShared<FNiagaraScratchPadCommandContext>(InViewModel);

	ChildSlot
	[
		SNew(SDynamicLayoutBox)
		.GenerateNamedWidget_Lambda([this](FName InWidgetName)
		{
			if (InWidgetName == ScriptSelectorName)
			{
				return ConstructScriptSelector();
			}
			else if (InWidgetName == ScriptEditorName)
			{
				return ConstructScriptEditor();
			}
			else if (InWidgetName == SelectionEditorName)
			{
				return ConstructSelectionEditor();
			}
			else
			{
				return SNullWidget::NullWidget;
			}
		})
		.GenerateNamedLayout_Lambda([this](FName InLayoutName, const SDynamicLayoutBox::FNamedWidgetProvider& InNamedWidgetProvider)
		{
			TSharedPtr<SWidget> Layout;
			if (InLayoutName == WideLayoutName)
			{
				Layout = SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(0.15f)
				[
					InNamedWidgetProvider.GetNamedWidget(ScriptSelectorName)
				]
				+ SSplitter::Slot()
				.Value(0.6f)
				[
					InNamedWidgetProvider.GetNamedWidget(ScriptEditorName)
				]
				+ SSplitter::Slot()
				.Value(0.25f)
				[
					InNamedWidgetProvider.GetNamedWidget(SelectionEditorName)
				];
			}
			else if (InLayoutName == NarrowLayoutName)
			{
				Layout = SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(0.3f)
				[
					SNew(SSplitter)
					.Orientation(Orient_Vertical)
					+ SSplitter::Slot()
					.Value(0.5f)
					[
						InNamedWidgetProvider.GetNamedWidget(ScriptSelectorName)
					]
					+ SSplitter::Slot()
					.Value(0.5f)
					[
						InNamedWidgetProvider.GetNamedWidget(SelectionEditorName)
					]
				]
				+ SSplitter::Slot()
				.Value(0.7f)
				[
					InNamedWidgetProvider.GetNamedWidget(ScriptEditorName)
				];
			}
			else
			{
				Layout = SNullWidget::NullWidget;
			}
			return Layout.ToSharedRef();
		})
		.ChooseLayout_Lambda([this]() 
		{ 
			if (GetCachedGeometry().GetLocalSize().X < 1500)
			{
				return NarrowLayoutName;
			}
			else
			{
				return WideLayoutName;
			}
		})
	];
}

TSharedRef<SWidget> SNiagaraScratchPad::ConstructScriptSelector()
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.LargeHeaderText")
		.Text(LOCTEXT("ScriptSelector", "Scratch Script Selector"))
	]
	+ SVerticalBox::Slot()
	[
		SNew(SNiagaraScratchPadScriptSelector, ViewModel.Get(), CommandContext)
	];
}

TSharedRef<SWidget> SNiagaraScratchPad::ConstructScriptEditor()
{
	return SNew(SNiagaraScratchPadScriptEditorList, ViewModel.Get());
}

TSharedRef<SWidget> SNiagaraScratchPad::ConstructSelectionEditor()
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.LargeHeaderText")
		.Text(LOCTEXT("ScratchPadSelection", "Scratch Pad Selection"))
	]
	+ SVerticalBox::Slot()
	[
		SNew(SNiagaraSelectedObjectsDetails, ViewModel->GetObjectSelection())
	];
}

#undef LOCTEXT_NAMESPACE