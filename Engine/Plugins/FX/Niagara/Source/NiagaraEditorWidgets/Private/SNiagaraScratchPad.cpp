// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraScratchPad.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "Widgets/SDynamicLayoutBox.h"
#include "Widgets/SItemSelector.h"
#include "Widgets/SNiagaraSelectedObjectsDetails.h"
#include "Widgets/SVerticalResizeBox.h"
#include "Widgets/SNiagaraScriptGraph.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraScratchPadCommandContext.h"
#include "Widgets/SNiagaraParameterPanel.h"
#include "Widgets/SNiagaraParameterMapView.h"

#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "EditorFontGlyphs.h"
#include "NiagaraConstants.h"

#define LOCTEXT_NAMESPACE "NiagaraScratchPad"

FName ScriptSelectorName = "ScriptSelector";
FName ScriptParameterPanelName = "ScriptParameterPanel";
FName ScriptEditorName = "ScriptEditor";
FName SelectionEditorName = "SelectionEditor";
FName WideLayoutName = "Wide";
FName NarrowLayoutName = "Narrow";

class SNiagaraPinButton : public SButton
{
public:
	DECLARE_DELEGATE_OneParam(FOnPinnedChanged, bool /* bIsPinned */)

public:
	SLATE_BEGIN_ARGS(SNiagaraPinButton)
		: _IsPinned(false) 
		, _ShowWhenUnpinned(true)
		, _PinTargetDisplayName(LOCTEXT("DefaultTargetDisplayName", "Target"))
		, _PinItemDisplayName(LOCTEXT("DefaultItemDisplayName", "Item"))
	{}
		SLATE_ATTRIBUTE(bool, IsPinned)
		SLATE_ATTRIBUTE(bool, ShowWhenUnpinned)
		SLATE_ARGUMENT(FText, PinTargetDisplayName)
		SLATE_ARGUMENT(FText, PinItemDisplayName)
		SLATE_EVENT(FOnPinnedChanged, OnPinnedChanged);
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		IsPinned = InArgs._IsPinned;
		ShowWhenUnpinned = InArgs._ShowWhenUnpinned;
		OnPinnedChangedDelegate = InArgs._OnPinnedChanged;
		PinnedToolTip = FText::Format(LOCTEXT("UnpinnedFormat", "Unpin this {0} from the {1}."), InArgs._PinItemDisplayName, InArgs._PinTargetDisplayName);
		UnpinnedToolTip = FText::Format(LOCTEXT("PinnedFormat", "Pin this {0} to the {1}."), InArgs._PinItemDisplayName, InArgs._PinTargetDisplayName);

		// Visibility and ToolTipText are base attributes so can't be set in the construct call below,
		// so them them directly here since the base widget construct has already been run.
		TAttribute<EVisibility> PinVisibility;
		PinVisibility.Bind(this, &SNiagaraPinButton::GetVisibilityFromPinned);
		SetVisibility(PinVisibility);
		TAttribute<FText> PinToolTipText;
		PinToolTipText.Bind(this, &SNiagaraPinButton::GetToolTipTextFromPinned);
		SetToolTipText(PinToolTipText);

		SButton::Construct(
			SButton::FArguments()
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ForegroundColor(FSlateColor::UseForeground())
			.OnClicked(this, &SNiagaraPinButton::OnButtonClicked)
			.ContentPadding(FMargin(3, 2, 2, 2))
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.WidthOverride(16)
				.HeightOverride(16)
				.RenderTransform(this, &SNiagaraPinButton::GetPinGlyphRenderTransform)
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FEditorFontGlyphs::Thumb_Tack)
				]
			]);
	}

private:
	FReply OnButtonClicked()
	{
		OnPinnedChangedDelegate.ExecuteIfBound(!IsPinned.Get());
		return FReply::Handled();
	}

	FText GetToolTipTextFromPinned() const
	{
		return IsPinned.Get(false) 
			? PinnedToolTip 
			: UnpinnedToolTip;
	}

	EVisibility GetVisibilityFromPinned() const
	{
		return IsPinned.Get(false) || ShowWhenUnpinned.Get(true)
			? EVisibility::Visible
			: EVisibility::Hidden;
	}

	TOptional<FSlateRenderTransform> GetPinGlyphRenderTransform() const
	{
		return IsPinned.Get(false) 
			? TOptional<FSlateRenderTransform>()
			: FSlateRenderTransform(FQuat2D(PI / 2));
	}

private:
	TAttribute<bool> IsPinned;
	TAttribute<bool> ShowWhenUnpinned;
	FOnPinnedChanged OnPinnedChangedDelegate;
	FText PinnedToolTip;
	FText UnpinnedToolTip;
};

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
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(3, 0, 0, 0)
			[
				SAssignNew(NameEditableText, SInlineEditableTextBlock)
				.Text(this, &SNiagaraScratchPadScriptRow::GetNameText)
				.ToolTipText(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::GetToolTip)
				.IsSelected(this, &SNiagaraScratchPadScriptRow::GetIsSelected)
				.OnVerifyTextChanged(this, &SNiagaraScratchPadScriptRow::VerifyNameTextChange)
				.OnTextCommitted(this, &SNiagaraScratchPadScriptRow::OnNameTextCommitted)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Visibility(this, &SNiagaraScratchPadScriptRow::GetUnappliedChangesVisibility)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.EditorHeaderText")
				.Text(FText::FromString(TEXT("*")))
				.ToolTipText(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::GetToolTip)
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1)
			[
				SNew(SNiagaraPinButton)
				.IsPinned(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::GetIsPinned)
				.OnPinnedChanged(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::SetIsPinned)
				.ShowWhenUnpinned(this, &SNiagaraScratchPadScriptRow::IsActive)
				.PinItemDisplayName(LOCTEXT("PinItem", "script"))
				.PinTargetDisplayName(LOCTEXT("PinTarget", "edit list"))
			]
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
			ScratchPadViewModel->SetActiveScriptViewModel(ScriptViewModel.ToSharedRef());

			FMenuBuilder MenuBuilder(true, CommandContext->GetCommands());
			CommandContext->AddMenuItems(MenuBuilder);

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

	bool VerifyNameTextChange(const FText& InNewNameText, FText& OutErrorMessage)
	{
		if (InNewNameText.IsEmpty())
		{
			OutErrorMessage = NSLOCTEXT("NiagaraScratchPadScriptName", "EmptyNameErrorMessage", "Script name can not be empty.");
			return false;
		}
		if (InNewNameText.ToString().Len() > FNiagaraConstants::MaxScriptNameLength)
		{
			OutErrorMessage = FText::Format(NSLOCTEXT("NiagaraScratchPadScriptName", "NameTooLongErrorFormat", "The name entered is too long.\nThe maximum script name length is {0}."), FText::AsNumber(FNiagaraConstants::MaxScriptNameLength));
			return false;
		}

		return true;
	}

	void OnNameTextCommitted(const FText& InText, ETextCommit::Type InCommitType)
	{
		ScriptViewModel->SetScriptName(InText);
	}

	bool IsActive() const
	{
		return IsHovered();
	}

	EVisibility GetUnappliedChangesVisibility() const
	{
		return ScriptViewModel->HasUnappliedChanges() ? EVisibility::Visible : EVisibility::Collapsed;
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
			.CategoryRowStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.CategoryRow")
			.ClearSelectionOnClick(false)
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

		if (ViewModel->GetActiveScriptViewModel().IsValid())
		{
			TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> SelectedViewModels;
			SelectedViewModels.Add(ViewModel->GetActiveScriptViewModel().ToSharedRef());
			ScriptSelector->SetSelectedItems(SelectedViewModels);
		}
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (CommandContext->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
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
			FMenuBuilder MenuBuilder(true, CommandContext->GetCommands());
			CommandContext->AddMenuItems(MenuBuilder);

			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
			return FReply::Handled().ReleaseMouseCapture();
		}
		return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
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
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ActiveScriptViewModel = ViewModel->GetActiveScriptViewModel();
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
		Categories.Add(Item->GetScripts()[0].Script->GetUsage());
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
		return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(3, 0, 0, 0)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
			.Text(ViewModel->GetDisplayNameForUsage(Category))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 4.0f, 3.0f, 4.0f)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "RoundButton")
			.OnClicked(this, &SNiagaraScratchPadScriptSelector::ScriptSelectorAddButtonClicked, Category)
			.ContentPadding(FMargin(3.0f, 2.0f, 2.0f, 2.0f))
			.Content()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Plus"))
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
			ViewModel->SetActiveScriptViewModel(ActivatedScript);
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
				ViewModel->ResetActiveScriptViewModel();
			}
			else if (SelectedScripts.Num())
			{
				ViewModel->SetActiveScriptViewModel(SelectedScripts[0]);
			}
		}
	}

	FReply ScriptSelectorAddButtonClicked(ENiagaraScriptUsage Usage)
	{
		TSharedPtr<FNiagaraScratchPadScriptViewModel> NewScriptViewModel = ViewModel->CreateNewScript(Usage, ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraTypeDefinition());
		if (NewScriptViewModel.IsValid())
		{
			ViewModel->SetActiveScriptViewModel(NewScriptViewModel.ToSharedRef());
			NewScriptViewModel->SetIsPendingRename(true);
		}
		return FReply::Handled();
	}

	bool GetItemIsSelected(TWeakPtr<FNiagaraScratchPadScriptViewModel> ItemWeak) const
	{
		TSharedPtr<FNiagaraScratchPadScriptViewModel> Item = ItemWeak.Pin();
		return Item.IsValid() && ViewModel->GetActiveScriptViewModel() == Item;
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
				.AutoWidth()
				.Padding(2.0f, 3.0f, 5.0f, 3.0f)
				[
					SNew(SButton)
					.OnClicked(this, &SNiagaraScratchPadScriptEditor::OnApplyButtonClicked)
					.ToolTipText(LOCTEXT("ApplyButtonToolTip", "Apply the current changes to this script.  This will update the selection stack UI and compile neccessary scripts."))
					.IsEnabled(this, &SNiagaraScratchPadScriptEditor::GetApplyButtonIsEnabled)
					.ContentPadding(FMargin(3.0f, 0.0f))
					.Content()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f, 2.0f, 2.0f, 2.0f)
						[
							SNew(SImage)
							.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Apply.Small"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 2.0f, 2.0f, 3.0f)
						[
							SNew(STextBlock)
							.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.EditorHeaderText")
							.Text(LOCTEXT("ApplyButtonLabel", "Apply"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(5, 0, 2, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.EditorHeaderText")
					.Text(this, &SNiagaraScratchPadScriptEditor::GetNameText)
					.ToolTipText(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::GetToolTip)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(STextBlock)
					.Visibility(this, &SNiagaraScratchPadScriptEditor::GetUnappliedChangesVisibility)
					.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.EditorHeaderText")
					.Text(FText::FromString(TEXT("*")))
					.ToolTipText(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::GetToolTip)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1)
				[
					SNew(SNiagaraPinButton)
					.IsPinned(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::GetIsPinned)
					.OnPinnedChanged(ScriptViewModel.ToSharedRef(), &FNiagaraScratchPadScriptViewModel::SetIsPinned)
					.PinItemDisplayName(LOCTEXT("PinItem", "script"))
					.PinTargetDisplayName(LOCTEXT("PinTarget", "edit list"))
				]
			]
			+ SVerticalBox::Slot()
			[
				SAssignNew(Graph,SNiagaraScriptGraph, ScriptViewModel->GetGraphViewModel())
				.ZoomToFitOnLoad(true)
			]
		];

		if (ScriptViewModel)
		{
			NodeIDHandle = ScriptViewModel->OnNodeIDFocusRequested().AddLambda(
				[this](FNiagaraScriptIDAndGraphFocusInfo* FocusInfo)
				{
					if (Graph.IsValid() && FocusInfo != nullptr)
					{
						Graph->FocusGraphElement(FocusInfo->GetScriptGraphFocusInfo().Get());
					}
				}
			);

			PinIDHandle = ScriptViewModel->OnPinIDFocusRequested().AddLambda(
				[this](FNiagaraScriptIDAndGraphFocusInfo* FocusInfo)
				{
					if (Graph.IsValid() && FocusInfo != nullptr)
					{
						Graph->FocusGraphElement(FocusInfo->GetScriptGraphFocusInfo().Get());
					}
				}
			);
		}
	}

	~SNiagaraScratchPadScriptEditor()
	{
		if (ScriptViewModel)
		{
			ScriptViewModel->OnNodeIDFocusRequested().Remove(NodeIDHandle);
			ScriptViewModel->OnPinIDFocusRequested().Remove(PinIDHandle);
		}
	}

private:
	FText GetNameText() const
	{
		return ScriptViewModel->GetDisplayName();
	}

	EVisibility GetUnappliedChangesVisibility() const
	{
		return ScriptViewModel->HasUnappliedChanges() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FReply OnApplyButtonClicked()
	{
		ScriptViewModel->ApplyChanges();

		return FReply::Handled();
	}

	bool GetApplyButtonIsEnabled() const
	{
		return ScriptViewModel->HasUnappliedChanges();
	}

private:
	TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel;
	TSharedPtr<SNiagaraScriptGraph> Graph;

	FDelegateHandle NodeIDHandle;
	FDelegateHandle PinIDHandle;
};

class SNiagaraScratchPadScriptEditorList : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPadScriptEditor) {}

	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraScratchPadViewModel* InViewModel)
	{
		ViewModel = InViewModel;
		ViewModel->OnScriptViewModelsChanged().AddSP(this, &SNiagaraScratchPadScriptEditorList::ScriptViewModelsChanged);
		ViewModel->OnEditScriptViewModelsChanged().AddSP(this, &SNiagaraScratchPadScriptEditorList::EditScriptViewModelsChanged);
		ViewModel->OnActiveScriptChanged().AddSP(this, &SNiagaraScratchPadScriptEditorList::ActiveScriptChanged);
		bIsUpdatingSelection = false;

		ChildSlot
		[
			SAssignNew(ContentBorder, SBorder)
			.BorderImage(FEditorStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		];
		UpdateContentFromEditScriptViewModels();
	}

private:
	void ScriptViewModelsChanged()
	{
		ScriptViewModelWidgetPairs.RemoveAll([](const FScriptViewModelWidgetPair& ScriptViewModelWidgetPair)
		{
			return ScriptViewModelWidgetPair.ViewModel.IsValid() == false || ScriptViewModelWidgetPair.Widget.IsValid() == false;
		});
	}

	void EditScriptViewModelsChanged()
	{
		UpdateContentFromEditScriptViewModels();
	}

	TSharedRef<SWidget> FindOrAddScriptEditor(TSharedRef<FNiagaraScratchPadScriptViewModel> ScriptViewModel)
	{
		FScriptViewModelWidgetPair* ExistingPair = ScriptViewModelWidgetPairs.FindByPredicate([ScriptViewModel](FScriptViewModelWidgetPair& ScriptViewModelWidgetPair)
		{ 
			return ScriptViewModelWidgetPair.ViewModel == ScriptViewModel && ScriptViewModelWidgetPair.Widget.IsValid();
		});

		if (ExistingPair != nullptr)
		{
			return ExistingPair->Widget.ToSharedRef();
		}
		else
		{
			TSharedRef<SWidget> NewEditor = SNew(SNiagaraScratchPadScriptEditor, ScriptViewModel);
			ScriptViewModelWidgetPairs.Add({ TWeakPtr<FNiagaraScratchPadScriptViewModel>(ScriptViewModel), NewEditor });
			return NewEditor;
		}
	}

	void UpdateContentFromEditScriptViewModels()
	{
		TSharedPtr<SWidget> NewContent;
		if (ViewModel->GetEditScriptViewModels().Num() == 0)
		{
			NewContent = SNew(SBox)
				.HAlign(HAlign_Center)
				.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoScriptToEdit", "No script selected"))
				];
			ScriptEditorList.Reset();
		}
		else if(ViewModel->GetEditScriptViewModels().Num() == 1)
		{
			NewContent = FindOrAddScriptEditor(ViewModel->GetEditScriptViewModels()[0]);
			ScriptEditorList.Reset();
		}
		else 
		{
			if (ScriptEditorList.IsValid())
			{
				ScriptEditorList->RequestListRefresh();
			}
			else 
			{
				ScriptEditorList = SNew(SListView<TSharedRef<FNiagaraScratchPadScriptViewModel>>)
					.ListItemsSource(&ViewModel->GetEditScriptViewModels())
					.OnGenerateRow(this, &SNiagaraScratchPadScriptEditorList::OnGenerateScriptEditorRow)
					.OnSelectionChanged(this, &SNiagaraScratchPadScriptEditorList::OnSelectionChanged);
			}
			NewContent = ScriptEditorList;
		}

		ContentBorder->SetContent(NewContent.ToSharedRef());
	}

	void ActiveScriptChanged()
	{
		if (ScriptEditorList.IsValid() && bIsUpdatingSelection == false)
		{
			TGuardValue<bool> UpdateGuard(bIsUpdatingSelection, true);
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ActiveScriptViewModel = ViewModel->GetActiveScriptViewModel();
			if (ActiveScriptViewModel.IsValid())
			{
				ScriptEditorList->SetSelection(ActiveScriptViewModel.ToSharedRef());
			}
			else
			{
				ScriptEditorList->ClearSelection();
			}
		}
	}

	TSharedRef<ITableRow> OnGenerateScriptEditorRow(TSharedRef<FNiagaraScratchPadScriptViewModel> Item, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return SNew(STableRow<TSharedRef<FNiagaraScratchPadScriptViewModel>>, OwnerTable)
		[
			SNew(SVerticalResizeBox)
			.ContentHeight(Item, &FNiagaraScratchPadScriptViewModel::GetEditorHeight)
			.ContentHeightChanged(Item, &FNiagaraScratchPadScriptViewModel::SetEditorHeight)
			[
				FindOrAddScriptEditor(Item)
			]
		];
	}

	void OnSelectionChanged(TSharedPtr<FNiagaraScratchPadScriptViewModel> InNewSelection, ESelectInfo::Type SelectInfo)
	{
		if (bIsUpdatingSelection == false)
		{
			TGuardValue<bool> UpdateGuard(bIsUpdatingSelection, true);
			TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> SelectedScripts;
			ScriptEditorList->GetSelectedItems(SelectedScripts);
			if (SelectedScripts.Num() == 0)
			{
				ViewModel->ResetActiveScriptViewModel();
			}
			else if (SelectedScripts.Num())
			{
				ViewModel->SetActiveScriptViewModel(SelectedScripts[0]);
			}
		}
	}

private:
	struct FScriptViewModelWidgetPair
	{
		TWeakPtr<FNiagaraScratchPadScriptViewModel> ViewModel;
		TSharedPtr<SWidget> Widget;
	};

	UNiagaraScratchPadViewModel* ViewModel;
	TSharedPtr<SBorder> ContentBorder;
	TSharedPtr<SListView<TSharedRef<FNiagaraScratchPadScriptViewModel>>> ScriptEditorList;
	TArray<FScriptViewModelWidgetPair> ScriptViewModelWidgetPairs;
	bool bIsUpdatingSelection;
};

class SNiagaraScratchPadParameterPanel : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPadScriptEditor) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraScratchPadViewModel* InViewModel)
	{
		ViewModel = InViewModel;
		ViewModel->OnActiveScriptChanged().AddSP(this, &SNiagaraScratchPadParameterPanel::ActiveScriptChanged);
		bool bForce = true;
		UpdateContent(bForce);
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel = ScriptViewModelWeak.Pin();
		if (ScriptViewModel.IsValid() && ScriptViewModel->GetParameterPanelCommands()->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

private:
	void UpdateContent(bool bForce)
	{
		TSharedPtr<FNiagaraScratchPadScriptViewModel> OldScriptViewModel = ScriptViewModelWeak.Pin();
		TSharedPtr<FNiagaraScratchPadScriptViewModel> NewScriptViewModel = ViewModel->GetActiveScriptViewModel();
		if (NewScriptViewModel != OldScriptViewModel || bForce)
		{
			ScriptViewModelWeak = NewScriptViewModel;
			if (NewScriptViewModel.IsValid())
			{
				TSharedPtr<SWidget> ParameterPanelWidget;
				if (NewScriptViewModel->GetParameterPanelViewModel().IsValid())
				{
					ParameterPanelWidget = SNew(SNiagaraParameterPanel, NewScriptViewModel->GetParameterPanelViewModel(), NewScriptViewModel->GetParameterPanelCommands());
				}
				else
				{
					TSharedRef<FNiagaraObjectSelection> ScriptSelection = MakeShared<FNiagaraObjectSelection>();
					const FVersionedNiagaraScript& EditScript = NewScriptViewModel->GetEditScript();
					ScriptSelection->SetSelectedObject(EditScript.Script, &EditScript.Version);
					TArray<TSharedRef<FNiagaraObjectSelection>> ParameterSelections;
					ParameterSelections.Add(ScriptSelection);
					ParameterSelections.Add(NewScriptViewModel->GetVariableSelection());
					ParameterPanelWidget = SNew(SNiagaraParameterMapView, ParameterSelections, SNiagaraParameterMapView::EToolkitType::SCRIPT, NewScriptViewModel->GetParameterPanelCommands());
				}
				ChildSlot
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2.0f, 2.0f, 2.0f, 5.0f)
					[
						SNew(STextBlock)
						.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.SubSectionHeaderText")
						.Text(this, &SNiagaraScratchPadParameterPanel::GetSubHeaderText)
					]
					+ SVerticalBox::Slot()
					[
						ParameterPanelWidget.ToSharedRef()
					]
				];
			}
			else
			{
				ChildSlot
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoScriptForParameters", "No script selected"))
					]
				];
			}
		}
	}

	void ActiveScriptChanged()
	{
		bool bForce = false;
		UpdateContent(bForce);
	}

	FText GetSubHeaderText() const
	{
		TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel = ScriptViewModelWeak.Pin();
		return ScriptViewModel.IsValid() ? ScriptViewModel->GetDisplayName() : FText();
	}

private:
	UNiagaraScratchPadViewModel* ViewModel;
	TWeakPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModelWeak;
};

class SNiagaraScratchPadSectionBox : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPadSectionBox) {}
		SLATE_ATTRIBUTE(FText, HeaderText)
		SLATE_DEFAULT_SLOT(FArguments, Content);
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(1.0f, 5.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "Docking.TabFont")
				.Text(InArgs._HeaderText)
			]
			+ SVerticalBox::Slot()
			.Padding(5.0f, 0.0f, 3.0f, 5.0f)
			[
				InArgs._Content.Widget
			]
		];
	}
};

void SNiagaraScratchPad::Construct(const FArguments& InArgs, UNiagaraScratchPadViewModel* InViewModel)
{
	ViewModel = InViewModel;
	ViewModel->GetObjectSelection()->OnSelectedObjectsChanged().AddSP(this, &SNiagaraScratchPad::ObjectSelectionChanged);

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
			else if (InWidgetName == ScriptParameterPanelName)
			{
				return ConstructParameterPanel();
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
				.PhysicalSplitterHandleSize(4.0f)
				.HitDetectionSplitterHandleSize(6.0f)
				+ SSplitter::Slot()
				.Value(0.15f)
				[
					SNew(SSplitter)
					.Style(FEditorStyle::Get(), "SplitterDark")
					.Orientation(Orient_Vertical)
					.PhysicalSplitterHandleSize(4.0f)
					.HitDetectionSplitterHandleSize(6.0f)
					+ SSplitter::Slot()
					.Value(0.5f)
					[
						InNamedWidgetProvider.GetNamedWidget(ScriptSelectorName)
					]
					+ SSplitter::Slot()
					.Value(0.5f)
					[
						InNamedWidgetProvider.GetNamedWidget(ScriptParameterPanelName)
					]
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
				.PhysicalSplitterHandleSize(4.0f)
				.HitDetectionSplitterHandleSize(6.0f)
				+ SSplitter::Slot()
				.Value(0.3f)
				[
					SNew(SSplitter)
					.Style(FEditorStyle::Get(), "SplitterDark")
					.Orientation(Orient_Vertical)
					.PhysicalSplitterHandleSize(4.0f)
					.HitDetectionSplitterHandleSize(6.0f)
					+ SSplitter::Slot()
					.Value(0.3f)
					[
						InNamedWidgetProvider.GetNamedWidget(ScriptSelectorName)
					]
					+ SSplitter::Slot()
					.Value(0.3f)
					[
						InNamedWidgetProvider.GetNamedWidget(ScriptParameterPanelName)
					]
					+ SSplitter::Slot()
					.Value(0.4f)
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

void SNiagaraScratchPad::ObjectSelectionChanged()
{
	int32 SelectionCount = ViewModel->GetObjectSelection()->GetSelectedObjects().Num();
	if (SelectionCount == 0)
	{
		ObjectSelectionSubHeaderText = FText();
	}
	else if (SelectionCount == 1)
	{
		UObject* SelectedObject = ViewModel->GetObjectSelection()->GetSelectedObjects().Array()[0];
		if (SelectedObject->IsA<UEdGraphNode>())
		{
			UEdGraphNode* SelectedGraphNode = CastChecked<UEdGraphNode>(SelectedObject);
			ObjectSelectionSubHeaderText = SelectedGraphNode->GetNodeTitle(ENodeTitleType::ListView);
		}
		else if (SelectedObject->IsA<UNiagaraScript>())
		{
			UNiagaraScript* SelectedScript = CastChecked<UNiagaraScript>(SelectedObject);
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchPadScriptViewModel = ViewModel->GetViewModelForEditScript(SelectedScript);
			if (ScratchPadScriptViewModel.IsValid())
			{
				ObjectSelectionSubHeaderText = ScratchPadScriptViewModel->GetDisplayName();
			}
			else
			{
				ObjectSelectionSubHeaderText = FText::FromString(SelectedScript->GetName());
			}
		}
		else
		{
			ObjectSelectionSubHeaderText = FText::FromString(SelectedObject->GetName());
		}
	}
	else
	{
		ObjectSelectionSubHeaderText = FText::Format(LOCTEXT("MultipleSelectionFormat", "{0} Objects Selected..."),
			FText::AsNumber(SelectionCount));
	}
}

TSharedRef<SWidget> SNiagaraScratchPad::ConstructScriptSelector()
{
	return SNew(SNiagaraScratchPadSectionBox)
	.HeaderText(LOCTEXT("ScriptSelector", "Scratch Script Selector"))
	[
		SNew(SNiagaraScratchPadScriptSelector, ViewModel.Get(), CommandContext)
	];
}

TSharedRef<SWidget> SNiagaraScratchPad::ConstructParameterPanel()
{
	return SNew(SNiagaraScratchPadSectionBox)
	.HeaderText(LOCTEXT("ScratchScriptParameters", "Scratch Script Parameters"))
	[
		SNew(SNiagaraScratchPadParameterPanel, ViewModel.Get())
	];
}

TSharedRef<SWidget> SNiagaraScratchPad::ConstructScriptEditor()
{
	return SNew(SNiagaraScratchPadScriptEditorList, ViewModel.Get());
}

TSharedRef<SWidget> SNiagaraScratchPad::ConstructSelectionEditor()
{
	return SNew(SNiagaraScratchPadSectionBox)
	.HeaderText(LOCTEXT("ScratchPadSelection", "Scratch Pad Selection"))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f, 2.0f, 2.0f, 5.0f)
		[
			SNew(STextBlock)
			.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.ScratchPad.SubSectionHeaderText")
			.Visibility(this, &SNiagaraScratchPad::GetObjectSelectionSubHeaderTextVisibility)
			.Text(this, &SNiagaraScratchPad::GetObjectSelectionSubHeaderText)
		]
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Center)
		.AutoHeight()
		.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Visibility(this, &SNiagaraScratchPad::GetObjectSelectionNoSelectionTextVisibility)
			.Text(LOCTEXT("NoObjectSelection", "No object selected"))
		]
		+ SVerticalBox::Slot()
		[
			SNew(SNiagaraSelectedObjectsDetails, ViewModel->GetObjectSelection())
		]
	];
}

EVisibility SNiagaraScratchPad::GetObjectSelectionSubHeaderTextVisibility() const
{
	return ViewModel->GetObjectSelection()->GetSelectedObjects().Num() != 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SNiagaraScratchPad::GetObjectSelectionSubHeaderText() const
{
	return ObjectSelectionSubHeaderText;
}

EVisibility SNiagaraScratchPad::GetObjectSelectionNoSelectionTextVisibility() const
{
	return ViewModel->GetObjectSelection()->GetSelectedObjects().Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE