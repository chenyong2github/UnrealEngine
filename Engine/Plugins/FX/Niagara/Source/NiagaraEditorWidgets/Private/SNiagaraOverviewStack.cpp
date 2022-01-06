// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewStack.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraStackEditorData.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "SNiagaraStack.h"
#include "Stack/SNiagaraStackItemGroupAddButton.h"
#include "Stack/SNiagaraStackIssueIcon.h"
#include "Stack/SNiagaraStackRowPerfWidget.h"
#include "NiagaraStackCommandContext.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraEditorModule.h"
#include "NiagaraNodeAssignment.h"
#include "Widgets/SNiagaraParameterName.h"
#include "SNiagaraOverviewInlineParameterBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SToolTip.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Colors/SColorBlock.h"
#include "NiagaraEditorStyle.h"
#include "ScopedTransaction.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewStack"

class SNiagaraSystemOverviewEntryListRow : public STableRow<UNiagaraStackEntry*>
{
	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewEntryListRow) {}
		SLATE_ATTRIBUTE(EVisibility, IssueIconVisibility)
		SLATE_EVENT(FOnDragDetected, OnDragDetected)
		SLATE_EVENT(FOnTableRowDragLeave, OnDragLeave)
		SLATE_EVENT(FOnCanAcceptDrop, OnCanAcceptDrop)
		SLATE_EVENT(FOnAcceptDrop, OnAcceptDrop)
		SLATE_DEFAULT_SLOT(FArguments, Content);
	SLATE_END_ARGS();

	virtual ~SNiagaraSystemOverviewEntryListRow() override
	{
		StackEntry->OnStructureChanged().RemoveAll(this);	
	}
	
	void Construct(const FArguments& InArgs, UNiagaraStackViewModel* InStackViewModel, UNiagaraStackEntry* InStackEntry, TSharedRef<FNiagaraStackCommandContext> InStackCommandContext, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		StackViewModel = InStackViewModel;
		StackEntry = InStackEntry;
		StackCommandContext = InStackCommandContext;
		IssueIconVisibility = InArgs._IssueIconVisibility;
		FSlateColor IconColor = FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName()));

		ExpandedImage = FCoreStyle::Get().GetBrush("TreeArrow_Expanded");
		CollapsedImage = FCoreStyle::Get().GetBrush("TreeArrow_Collapsed");
		
		FMargin ContentPadding;
		if (StackEntry->IsA<UNiagaraStackItem>())
		{
			BackgroundColor = FStyleColors::Panel;
			ContentPadding = FMargin(0, 1, 0, 1);
		}
		else
		{
			BackgroundColor = FStyleColors::Header;
			ContentPadding = FMargin(0, 2, 0, 2);
		}

		DisabledBackgroundColor = FStyleColors::Recessed;
		IsolatedBackgroundColor = FStyleColors::Select;

		TSharedRef<FNiagaraSystemViewModel> SystemViewModel = StackEntry->GetSystemViewModel();
		TSharedPtr<FNiagaraEmitterViewModel> EmitterViewModel = StackEntry->GetEmitterViewModel();
		if (EmitterViewModel.IsValid())
		{
			EmitterHandleViewModel = SystemViewModel->GetEmitterHandleViewModelForEmitter(EmitterViewModel->GetEmitter());
		}

		StackEntry->OnStructureChanged().AddSP(this, &SNiagaraSystemOverviewEntryListRow::InvalidateChildrenCount);

		STableRow<UNiagaraStackEntry*>::Construct(STableRow<UNiagaraStackEntry*>::FArguments()
			.Style(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.TableViewRow")
			.OnDragDetected(InArgs._OnDragDetected)
			.OnDragLeave(InArgs._OnDragLeave)
			.OnCanAcceptDrop(InArgs._OnCanAcceptDrop)
			.OnAcceptDrop(InArgs._OnAcceptDrop)
		[
			SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Recessed)
				.ToolTipText_UObject(StackEntry, &UNiagaraStackEntry::GetTooltipText)
				.Padding(FMargin(0, 1, 0, 1))
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(TAttribute<FSlateColor>(this, &SNiagaraSystemOverviewEntryListRow::GetBackgroundColor))
					.Padding(ContentPadding)
					[
						SNew(SHorizontalBox)
						// Expand button
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.Padding(3, 0, 1, 0)
						[					
							SNew(SButton)
							.ButtonStyle(FCoreStyle::Get(), "NoBorder")
							.Visibility_Lambda([=]()
							{
								if (StackEntry->GetCanExpandInOverview())
								{	
									return GetOverviewChildrenCount() > 0
										? EVisibility::Visible
										: EVisibility::Hidden;
								}
								else
								{
									return EVisibility::Collapsed;
								}
							})
							.OnClicked_Lambda([=]()
							{
								const bool bWillBeExpanded = !StackEntry->GetIsExpandedInOverview();
								FText Message = FText::Format(LOCTEXT("ChangedCollapseState", "{0} {1}"), bWillBeExpanded ?
									LOCTEXT("ChangedCollapseState_Expanded", "Expanded") : LOCTEXT("ChangedCollapseState_Collapsed", "Collapsed"),
									StackEntry->GetAlternateDisplayName().IsSet() ? StackEntry->GetAlternateDisplayName().GetValue() : StackEntry->GetDisplayName());
								
								FScopedTransaction Transaction(Message);
								StackEntry->GetStackEditorData().Modify();
								
								StackEntry->SetIsExpandedInOverview(bWillBeExpanded);									
								return FReply::Handled();
							})
							.ForegroundColor(FSlateColor::UseForeground())
							.ContentPadding(2)
							[
								SNew(SImage)
								.Image(this, &SNiagaraSystemOverviewEntryListRow::GetExpandButtonImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
							]							
						]
						+ SHorizontalBox::Slot()
						.Padding(TAttribute<FMargin>(this, &SNiagaraSystemOverviewEntryListRow::GetInnerContentPadding))
						[
							InArgs._Content.Widget
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2, 0, 2, 0)
						[
							SNew(SNiagaraStackIssueIcon, StackViewModel, StackEntry)
							.Visibility(IssueIconVisibility)
						]
					]
				]
		],
		InOwnerTableView);
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			return FReply::Handled()
				.SetUserFocus(OwnerTablePtr.Pin()->AsWidget(), EFocusCause::Mouse)
				.CaptureMouse(SharedThis(this));
		}
		return STableRow<UNiagaraStackEntry*>::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			// Add the item represented by this row to the selection.
			UNiagaraSystemSelectionViewModel* Selection = StackEntry->GetSystemViewModel()->GetSelectionViewModel();
			if (Selection->ContainsEntry(StackEntry) == false)
			{
				TArray<UNiagaraStackEntry*> SelectedEntries;
				TArray<UNiagaraStackEntry*> DeselectedEntries;
				SelectedEntries.Add(StackEntry);
				bool bClearSelection = true;
				Selection->UpdateSelectedEntries(SelectedEntries, DeselectedEntries, bClearSelection);
			}

			FMenuBuilder MenuBuilder(true, StackCommandContext->GetCommands());
			bool bMenuItemsAdded = false;

			if (StackEntry->IsA<UNiagaraStackModuleItem>())
			{
				bMenuItemsAdded |= FNiagaraStackEditorWidgetsUtilities::AddStackModuleItemContextMenuActions(MenuBuilder, *CastChecked<UNiagaraStackModuleItem>(StackEntry), this->AsShared());
			}

			if (StackEntry->IsA<UNiagaraStackItem>())
			{
				bMenuItemsAdded |= FNiagaraStackEditorWidgetsUtilities::AddStackItemContextMenuActions(MenuBuilder, *CastChecked<UNiagaraStackItem>(StackEntry));
			}

			bMenuItemsAdded |= FNiagaraStackEditorWidgetsUtilities::AddStackEntryAssetContextMenuActions(MenuBuilder, *StackEntry);
			bMenuItemsAdded |= StackCommandContext->AddEditMenuItems(MenuBuilder);
		
			if (bMenuItemsAdded)
			{
				FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
				FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
				return FReply::Handled().ReleaseMouseCapture();
			}
			return STableRow<UNiagaraStackEntry*>::OnMouseButtonUp(MyGeometry, MouseEvent).ReleaseMouseCapture();
		}
		return STableRow<UNiagaraStackEntry*>::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
	{
		UNiagaraStackModuleItem* ModuleItem = Cast<UNiagaraStackModuleItem>(StackEntry);
		if (ModuleItem != nullptr)
		{
			const UNiagaraNodeFunctionCall& ModuleFunctionCall = ModuleItem->GetModuleNode();
			if (ModuleFunctionCall.FunctionScript != nullptr)
			{
				if (ModuleFunctionCall.FunctionScript->IsAsset() || GbShowNiagaraDeveloperWindows > 0)
				{
					ModuleFunctionCall.FunctionScript->VersionToOpenInEditor = ModuleFunctionCall.SelectedScriptVersion;
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(const_cast<UNiagaraScript*>(ToRawPtr(ModuleFunctionCall.FunctionScript)));
					return FReply::Handled();
				}
				else if (ModuleItem->IsScratchModule())
				{
					TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchPadScriptViewModel = ModuleItem->GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(ModuleFunctionCall.FunctionScript);
					if (ScratchPadScriptViewModel.IsValid())
					{
						ModuleItem->GetSystemViewModel()->GetScriptScratchPadViewModel()->FocusScratchPadScriptViewModel(ScratchPadScriptViewModel.ToSharedRef());
						return FReply::Handled();
					}
				}
			}
		}
		return FReply::Unhandled();
	}

private:
	FSlateColor GetBackgroundColor() const
	{
		if (IsSelected())
		{
			return FStyleColors::Select;
		}
		// robk - hack bandaid fix
		if (StackEntry->IsFinalized())
		{
			return DisabledBackgroundColor;
		}
		TSharedRef<FNiagaraSystemViewModel> SystemViewModel = StackEntry->GetSystemViewModel();
		if (SystemViewModel->GetSystem().GetIsolateEnabled())
		{
			TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModelPtr = EmitterHandleViewModel.Pin();
			if (EmitterHandleViewModelPtr.IsValid() && !EmitterHandleViewModelPtr->GetIsIsolated())
			{
				return IsolatedBackgroundColor;
			}
		}

		return StackEntry->GetIsEnabledAndOwnerIsEnabled() ? BackgroundColor : DisabledBackgroundColor;
	}

	FMargin GetInnerContentPadding() const
	{
		if (IssueIconVisibility.Get() == EVisibility::Visible)
		{
			return FMargin(5, 0, 1, 0);
		}
		else
		{
			return FMargin(5, 0, 5, 0);
		}
	}

	const FSlateBrush* GetExpandButtonImage() const
	{
		return StackEntry->GetIsExpandedInOverview() ? ExpandedImage : CollapsedImage;
	}

	uint32 GetOverviewChildrenCount() const
	{
		if (ChildrenCount.IsSet() == false)
		{
        	TArray<UNiagaraStackEntry*> Children;
        	StackEntry->GetFilteredChildrenOfTypes(Children, {UNiagaraStackItem::StaticClass()});
        	ChildrenCount = Children.Num();
        }
        	
        return ChildrenCount.GetValue();
	}

	void InvalidateChildrenCount(ENiagaraStructureChangedFlags Flags)
	{
		ChildrenCount.Reset();
	}

private:
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraStackEntry* StackEntry;
	TSharedPtr<FNiagaraStackCommandContext> StackCommandContext;
	FSlateColor BackgroundColor;
	FSlateColor DisabledBackgroundColor;
	FSlateColor IsolatedBackgroundColor;
	TAttribute<EVisibility> IssueIconVisibility;
	TWeakPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel;
	const FSlateBrush* ExpandedImage = nullptr;
	const FSlateBrush* CollapsedImage = nullptr;
	mutable TOptional<uint32> ChildrenCount;
};

class SNiagaraSystemOverviewEnabledCheckBox : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnCheckedChanged, bool /*bIsChecked*/);

	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewEnabledCheckBox) {}
		SLATE_ATTRIBUTE(bool, IsChecked)
		SLATE_EVENT(FOnCheckedChanged, OnCheckedChanged);
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs)
	{
		IsChecked = InArgs._IsChecked;
		OnCheckedChanged = InArgs._OnCheckedChanged;

		ChildSlot
		[
			SNew(SCheckBox)
			.OnCheckStateChanged(this, &SNiagaraSystemOverviewEnabledCheckBox::OnButtonClicked)
			.IsChecked(this, &SNiagaraSystemOverviewEnabledCheckBox::IsEnabled)
			.ToolTipText(LOCTEXT("EnableCheckBoxToolTip", "Enable or disable this item."))
		];
	}

private:
	void OnButtonClicked(ECheckBoxState InNewState)
	{
		OnCheckedChanged.ExecuteIfBound(IsChecked.IsBound() && !IsChecked.Get());
	}

	ECheckBoxState IsEnabled() const
	{
		if (IsChecked.IsBound())
		{
			return IsChecked.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
		return ECheckBoxState::Unchecked;
	}

private:
	TAttribute<bool> IsChecked;
	FOnCheckedChanged OnCheckedChanged;
};

class SNiagaraSystemOverviewItemName : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewItemName) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackItem* InStackItem)
	{
		StackItem = InStackItem;
		StackItem->OnAlternateDisplayNameChanged().AddSP(this, &SNiagaraSystemOverviewItemName::UpdateFromAlternateDisplayName);

		if (StackItem->IsA<UNiagaraStackModuleItem>() &&
			CastChecked<UNiagaraStackModuleItem>(StackItem)->GetModuleNode().IsA<UNiagaraNodeAssignment>())
		{
			UNiagaraNodeAssignment* AssignmentNode = CastChecked<UNiagaraNodeAssignment>(&CastChecked<UNiagaraStackModuleItem>(StackItem)->GetModuleNode());
			AssignmentNode->OnAssignmentTargetsChanged().AddSP(this, &SNiagaraSystemOverviewItemName::UpdateFromAssignmentTargets);
		}

		UpdateContent();
	}

	~SNiagaraSystemOverviewItemName()
	{
		if (StackItem.IsValid() && StackItem->IsFinalized() == false)
		{
			StackItem->OnAlternateDisplayNameChanged().RemoveAll(this);

			if (StackItem->IsA<UNiagaraStackModuleItem>() &&
				CastChecked<UNiagaraStackModuleItem>(StackItem)->GetModuleNode().IsA<UNiagaraNodeAssignment>())
			{
				UNiagaraNodeAssignment* AssignmentNode = CastChecked<UNiagaraNodeAssignment>(&CastChecked<UNiagaraStackModuleItem>(StackItem)->GetModuleNode());
				AssignmentNode->OnAssignmentTargetsChanged().RemoveAll(this);
			}
		}
	}

private:
	void UpdateContent()
	{
		UNiagaraStackModuleItem* ModuleItem = Cast<UNiagaraStackModuleItem>(StackItem);
		UNiagaraNodeAssignment* AssignmentNode = ModuleItem != nullptr 
			? Cast<UNiagaraNodeAssignment>(&ModuleItem->GetModuleNode())
			: nullptr;

		if (StackItem->GetAlternateDisplayName().IsSet() == false &&
			AssignmentNode != nullptr &&
			AssignmentNode->GetAssignmentTargets().Num() > 0)
		{
			NameTextBlock.Reset();

			TSharedPtr<SWidget> ParameterWidget;
			if (AssignmentNode->GetAssignmentTargets().Num() == 1)
			{
				ParameterWidget = SNew(SNiagaraParameterName)
					.ParameterName(AssignmentNode->GetAssignmentTargets()[0].GetName())
					.IsReadOnly(true);
			}
			else
			{
				TSharedRef<SVerticalBox> ParameterBox = SNew(SVerticalBox);
				for (const FNiagaraVariable& AssignmentTarget : AssignmentNode->GetAssignmentTargets())
				{
					ParameterBox->AddSlot()
					.AutoHeight()
					.Padding(0.0f, 1.0f, 0.0f, 1.0f)
					[
						SNew(SScaleBox)
						.UserSpecifiedScale(0.85f)
						.Stretch(EStretch::UserSpecified)
						.HAlign(HAlign_Left)
						[
							SNew(SNiagaraParameterName)
							.ParameterName(AssignmentTarget.GetName())
							.IsReadOnly(true)
						]
					];
				}
				ParameterWidget = ParameterBox;
			}

			ChildSlot
			[
				SNew(SHorizontalBox)
				.IsEnabled_UObject(StackItem.Get(), &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.OverviewStack.ItemText")
					.Text(LOCTEXT("SetVariablesPrefix", "Set:"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				+ SHorizontalBox::Slot()
				[
					ParameterWidget.ToSharedRef()
				]
			];
		}
		else
		{
			ChildSlot
			[
				SAssignNew(NameTextBlock, STextBlock)
				.Text(this, &SNiagaraSystemOverviewItemName::GetItemDisplayName)
				.ToolTipText(this, &SNiagaraSystemOverviewItemName::GetItemToolTip)
				.IsEnabled_UObject(StackItem.Get(), &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.ColorAndOpacity(FSlateColor::UseForeground())
			];
			UpdateTextStyle();
		}
	}

	void UpdateTextStyle()
	{
		if (NameTextBlock.IsValid())
		{
			if (StackItem->GetAlternateDisplayName().IsSet())
			{
				NameTextBlock->SetTextStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("NiagaraEditor.SystemOverview.AlternateItemText"));
			}
			else
			{
				NameTextBlock->SetTextStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("NiagaraEditor.OverviewStack.ItemText"));
			}
		}
	}

	FText GetItemDisplayName() const
	{
		if (StackItem.IsValid() && StackItem->IsFinalized() == false)
		{
			return StackItem->GetAlternateDisplayName().IsSet() ? StackItem->GetAlternateDisplayName().GetValue() : StackItem->GetDisplayName();
		}
		return FText::GetEmpty();
	}

	FText GetItemToolTip() const
	{
		if (StackItem.IsValid() && StackItem->IsFinalized() == false)
		{
			FText CurrentToolTip = StackItem->GetTooltipText();
			if (ToolTipCache.IsSet() == false || LastToolTipCache.IdenticalTo(CurrentToolTip) == false)
			{
				if(StackItem->GetAlternateDisplayName().IsSet())
				{
					FText AlternateNameAndOriginalName = FText::Format(LOCTEXT("AlternateNameAndOriginalNameFormat", "{0} ({1})"), StackItem->GetAlternateDisplayName().GetValue(), StackItem->GetDisplayName());
					if (CurrentToolTip.IsEmptyOrWhitespace())
					{
						ToolTipCache = AlternateNameAndOriginalName;
					}
					else
					{
						ToolTipCache = FText::Format(LOCTEXT("AlternateDisplayNameToolTipFormat", "{0}\n\n{1}"), AlternateNameAndOriginalName, StackItem->GetTooltipText());
					}
				}
				else
				{
					ToolTipCache = CurrentToolTip;
				}
			}
			LastToolTipCache = CurrentToolTip;
			return ToolTipCache.GetValue();
		}
		return FText::GetEmpty();
	}

	void UpdateFromAlternateDisplayName()
	{
		if (StackItem.IsValid() && StackItem->IsFinalized() == false)
		{
			if (StackItem->GetAlternateDisplayName().IsSet())
			{
				if (NameTextBlock.IsValid() == false)
				{
					UpdateContent();
				}
				else
				{
					UpdateTextStyle();
				}
			}
			else
			{
				if (NameTextBlock.IsValid())
				{
					UpdateContent();
				}
			}
		}
		ToolTipCache.Reset();
	}

	void UpdateFromAssignmentTargets()
	{
		if (StackItem.IsValid() && StackItem->IsFinalized() == false)
		{
			UpdateContent();
		}
		ToolTipCache.Reset();
	}

private:
	mutable FText LastToolTipCache;
	mutable TOptional<FText> ToolTipCache;
	TSharedPtr<STextBlock> NameTextBlock;
	TWeakObjectPtr<UNiagaraStackItem> StackItem;
};

void SNiagaraOverviewStack::Construct(const FArguments& InArgs, UNiagaraStackViewModel& InStackViewModel, UNiagaraSystemSelectionViewModel& InOverviewSelectionViewModel)
{
	StackCommandContext = MakeShared<FNiagaraStackCommandContext>();

	bUpdatingOverviewSelectionFromStackSelection = false;
	bUpdatingStackSelectionFromOverviewSelection = false;

	
	StackViewModel = &InStackViewModel;
	OverviewSelectionViewModel = &InOverviewSelectionViewModel;
	OverviewSelectionViewModel->OnEntrySelectionChanged().AddSP(this, &SNiagaraOverviewStack::SystemSelectionChanged);

	ChildSlot
	[
		SAssignNew(EntryListView, SListView<UNiagaraStackEntry*>)
		.ListItemsSource(&FlattenedEntryList)
		.OnGenerateRow(this, &SNiagaraOverviewStack::OnGenerateRowForEntry)
		.OnSelectionChanged(this, &SNiagaraOverviewStack::OnSelectionChanged)
		.SelectionMode(ESelectionMode::Multi)
		.OnItemToString_Debug_Static(&FNiagaraStackEditorWidgetsUtilities::StackEntryToStringForListDebug)
	];

	InStackViewModel.OnExpansionChanged().AddSP(this, &SNiagaraOverviewStack::EntryExpansionChanged);
	InStackViewModel.OnExpansionInOverviewChanged().AddSP(this, &SNiagaraOverviewStack::EntryExpansionInOverviewChanged);
	InStackViewModel.OnStructureChanged().AddSP(this, &SNiagaraOverviewStack::EntryStructureChanged);
		
	bRefreshEntryListPending = true;
	RefreshEntryList();
	SystemSelectionChanged();
}

SNiagaraOverviewStack::~SNiagaraOverviewStack()
{
	if (StackViewModel != nullptr)
	{
		StackViewModel->OnStructureChanged().RemoveAll(this);
		StackViewModel->OnExpansionInOverviewChanged().RemoveAll(this);
		StackViewModel->OnExpansionChanged().RemoveAll(this);
	}

	if (OverviewSelectionViewModel != nullptr)
	{
		OverviewSelectionViewModel->OnEntrySelectionChanged().RemoveAll(this);
	}
}

bool SNiagaraOverviewStack::SupportsKeyboardFocus() const
{
	return true;
}

FReply SNiagaraOverviewStack::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (StackCommandContext->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SNiagaraOverviewStack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	RefreshEntryList();
}

void SNiagaraOverviewStack::AddEntriesRecursive(UNiagaraStackEntry& EntryToAdd, TArray<UNiagaraStackEntry*>& EntryList, const TArray<UClass*>& AcceptableClasses, TArray<UNiagaraStackEntry*> ParentChain)
{
	if (AcceptableClasses.ContainsByPredicate([&] (UClass* Class) { return EntryToAdd.IsA(Class); }))
	{
		EntryList.Add(&EntryToAdd);
		EntryObjectKeyToParentChain.Add(FObjectKey(&EntryToAdd), ParentChain);

		if(!EntryToAdd.GetStackEditorData().GetStackEntryIsExpandedInOverview(EntryToAdd.GetStackEditorDataKey(), true))
		{
			return;
		}
		
		TArray<UNiagaraStackEntry*> Children;
		EntryToAdd.GetFilteredChildren(Children);
		ParentChain.Add(&EntryToAdd);
		for (UNiagaraStackEntry* Child : Children)
		{
			checkf(Child != nullptr, TEXT("Stack entry had null child."));
			AddEntriesRecursive(*Child, EntryList, AcceptableClasses, ParentChain);
		}
	}
}

void SNiagaraOverviewStack::RefreshEntryList()
{
	if (bRefreshEntryListPending)
	{
		FlattenedEntryList.Empty();
		EntryObjectKeyToParentChain.Empty();
		TArray<UClass*> AcceptableClasses;
		AcceptableClasses.Add(UNiagaraStackItemGroup::StaticClass());
		AcceptableClasses.Add(UNiagaraStackItem::StaticClass());

		UNiagaraStackEntry* RootEntry = StackViewModel->GetRootEntry();
		checkf(RootEntry != nullptr, TEXT("Root entry was null."));
		TArray<UNiagaraStackEntry*> RootChildren;
		RootEntry->GetFilteredChildren(RootChildren);
		for (UNiagaraStackEntry* RootChild : RootChildren)
		{
			checkf(RootEntry != nullptr, TEXT("Root entry child was null."));
			TArray<UNiagaraStackEntry*> ParentChain;
			AddEntriesRecursive(*RootChild, FlattenedEntryList, AcceptableClasses, ParentChain);
		}

		bRefreshEntryListPending = false;
		EntryListView->RequestListRefresh();
	}
}

void SNiagaraOverviewStack::EntryExpansionChanged()
{
	bRefreshEntryListPending = true;
}

void SNiagaraOverviewStack::EntryExpansionInOverviewChanged()
{
	bRefreshEntryListPending = true;
}

void SNiagaraOverviewStack::EntryStructureChanged(ENiagaraStructureChangedFlags Flags)
{
	bRefreshEntryListPending = true;
}

TSharedRef<ITableRow> SNiagaraOverviewStack::OnGenerateRowForEntry(UNiagaraStackEntry* Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	FVector2D IconSize = FNiagaraEditorWidgetsStyle::Get().GetVector("NiagaraEditor.Stack.IconSize");
	TSharedPtr<SWidget> Content;
	if (Item->IsA<UNiagaraStackItem>())
	{
		UNiagaraStackItem* StackItem = CastChecked<UNiagaraStackItem>(Item);
		TSharedPtr<SWidget> IndentContent;

		// we give icons priority for the indent content
		if (StackItem->SupportsIcon())
		{
			IndentContent = SNew(SBox)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image_UObject(StackItem, &UNiagaraStackItem::GetIconBrush)
				];
		}
		// if we didn't have an icon but we specified a custom indent, we use that instead
		else if(StackItem->GetCustomOverviewIndent().IsSet())
		{
			float IndentValue = StackItem->GetCustomOverviewIndent().GetValue();
			IndentContent = SNew(SBox)
				.WidthOverride(IndentValue)
				.HeightOverride(1.f);
		}
		// otherwise, use a default
		else
		{
			IndentContent = SNew(SBox)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y);
		}
		
		TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox)
			// Indent content
			+ SHorizontalBox::Slot()
			.Padding(0, 1, 2, 1)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				IndentContent.ToSharedRef()
			]
			// Name
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(3, 2, 0, 2)
			[
				SNew(SNiagaraSystemOverviewItemName, StackItem)
			]
			+ SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(2, 0, 2, 0)
            [
                SNew(SNiagaraStackRowPerfWidget, Item)
            ];

		TSharedRef<SHorizontalBox> OptionsBox = SNew(SHorizontalBox);
		
		UNiagaraStackModuleItem* StackModuleItem = Cast<UNiagaraStackModuleItem>(StackItem);

		if (StackModuleItem)
		{
			// Scratch icon
			if(StackModuleItem->IsScratchModule())
			{
				OptionsBox->AddSlot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(3, 0, 3, 1)
					[					
						SNew(SImage)
						.ToolTipText(LOCTEXT("ScratchPadOverviewTooltip", "This module is a scratch pad script."))
						.Image(FNiagaraEditorStyle::Get().GetBrush("Tab.ScratchPad"))
					];
			}

			// Debug draw 
			if(StackModuleItem->GetModuleNode().ContainsDebugSwitch())
			{
				OptionsBox->AddSlot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(3, 0)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.ButtonColorAndOpacity(FLinearColor::Transparent)
						.ForegroundColor(FLinearColor::Transparent)
						.ToolTipText(LOCTEXT("EnableDebugDrawCheckBoxToolTip", "Enable or disable debug drawing for this item."))
						.OnClicked(this, &SNiagaraOverviewStack::ToggleModuleDebugDraw, StackItem)
						.ContentPadding(FMargin(2.f))
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
						[
							SNew(SImage)
							.Image(this, &SNiagaraOverviewStack::GetDebugIconBrush, StackItem)
						]
					];
			}
		}

		// Enabled checkbox
		OptionsBox->AddSlot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(0)
			[
				SNew(SNiagaraSystemOverviewEnabledCheckBox)
				.Visibility(this, &SNiagaraOverviewStack::GetEnabledCheckBoxVisibility, StackItem)
				.IsEnabled_UObject(StackItem, &UNiagaraStackEntry::GetOwnerIsEnabled)
				.IsChecked_UObject(StackItem, &UNiagaraStackEntry::GetIsEnabled)
				.OnCheckedChanged_UObject(StackItem, &UNiagaraStackItem::SetIsEnabled)
			];

		// in case we have at least one inline input, we add a wrap box to make sure we have enough space for both the module name and parameters
		if(StackModuleItem && StackModuleItem->GetModuleNode().ScriptIsValid() && StackModuleItem->GetInlineParameterInputs().Num() > 0)
		{
			ContentBox = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SWrapBox)
				// the wrap box will put the inline box into a second row if content box + parameter box would exceed the preferred size
				.PreferredSize(250.f)
				+ SWrapBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					// despite that, we also have to cap the content box size, since the wrapbox can't wrap a single widget
					.MaxDesiredWidth(250.f)
					[
						ContentBox
					]
				]
				+ SWrapBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					.MaxDesiredWidth(150.f)
					[
						SNew(SNiagaraOverviewInlineParameterBox, *StackModuleItem)
					]
				]
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				OptionsBox
			];
		}
		// if we don't, we don't use the wrap box for perf reasons
		else
		{
			ContentBox = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.MaxDesiredWidth(250.f)
				[
					ContentBox
				]			
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				OptionsBox
			];
		}
		
		Content = ContentBox;		
	}
	else if (Item->IsA<UNiagaraStackItemGroup>())
	{
		UNiagaraStackItemGroup* StackItemGroup = CastChecked<UNiagaraStackItemGroup>(Item);
		TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox)
			// Execution category icon
			+ SHorizontalBox::Slot()
			.Padding(0, 0, 2, 0)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush(FNiagaraStackEditorWidgetsUtilities::GetIconNameForExecutionSubcategory(Item->GetExecutionSubcategoryName(), true)))
					.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(Item->GetExecutionCategoryName())))
					.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				]
			]
			// Name
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(3, 2, 0, 2)
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.GroupText")
				.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(6, 0, 5, 0)
            [
                SNew(SNiagaraStackRowPerfWidget, Item)
            ];

		// Delete button
		if (StackItemGroup->SupportsDelete())
		{
			ContentBox->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.IsFocusable(false)
					.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.ForegroundColor"))
					.ToolTipText(this, &SNiagaraOverviewStack::GetItemGroupDeleteButtonToolTip, StackItemGroup)
					.OnClicked(this, &SNiagaraOverviewStack::OnItemGroupDeleteClicked, StackItemGroup)
					.IsEnabled(this, &SNiagaraOverviewStack::GetItemGroupDeleteButtonIsEnabled, StackItemGroup)
					.Visibility(this, &SNiagaraOverviewStack::GetItemGroupDeleteButtonVisibility, StackItemGroup)
					.Content()
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
						.Text(FText::FromString(FString(TEXT("\xf1f8"))))
					]
				];
		}

		// Enabled checkbox
		if (StackItemGroup->SupportsChangeEnabled())
		{
			ContentBox->AddSlot()
				.AutoWidth()
				.Padding(3, 3, 3, 3)
				[
					SNew(SCheckBox)
					.ForegroundColor(FSlateColor::UseSubduedForeground())
					.ToolTipText(LOCTEXT("StackItemGroupEnableDisableTooltip", "Enable or disable this item."))
					.IsChecked(this, &SNiagaraOverviewStack::ItemGroupCheckEnabledStatus, StackItemGroup)
					.OnCheckStateChanged(this, &SNiagaraOverviewStack::OnItemGroupEnabledStateChanged, StackItemGroup)
					.IsEnabled(this, &SNiagaraOverviewStack::GetItemGroupEnabledCheckboxEnabled, StackItemGroup)
				];
		}

		if (StackItemGroup->GetAddUtilities() != nullptr)
		{
			ContentBox->AddSlot()
			.AutoWidth()
			.Padding(0, 0, 1, 0)
			[
				SNew(SNiagaraStackItemGroupAddButton, *StackItemGroup)
				.Width(22)
			];
		}

		Content = ContentBox;
	}
	else
	{
		Content = SNullWidget::NullWidget;
	}


	return SNew(SNiagaraSystemOverviewEntryListRow, StackViewModel, Item, StackCommandContext.ToSharedRef(), OwnerTable)
		.OnDragDetected(this, &SNiagaraOverviewStack::OnRowDragDetected, TWeakObjectPtr<UNiagaraStackEntry>(Item))
		.OnDragLeave(this, &SNiagaraOverviewStack::OnRowDragLeave)
		.OnCanAcceptDrop(this, &SNiagaraOverviewStack::OnRowCanAcceptDrop)
		.OnAcceptDrop(this, &SNiagaraOverviewStack::OnRowAcceptDrop)
		.IssueIconVisibility(this, &SNiagaraOverviewStack::GetIssueIconVisibility)
	[
		Content.ToSharedRef()
	];
}

EVisibility SNiagaraOverviewStack::GetEnabledCheckBoxVisibility(UNiagaraStackItem* Item) const
{
	return Item->SupportsChangeEnabled() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SNiagaraOverviewStack::GetShouldDebugDrawStatusVisibility(UNiagaraStackItem* Item) const
{
	return IsModuleDebugDrawEnabled(Item) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SNiagaraOverviewStack::IsModuleDebugDrawEnabled(UNiagaraStackItem* Item) const
{
	UNiagaraStackModuleItem* ModuleItem = Cast<UNiagaraStackModuleItem>(Item);
	return ModuleItem && ModuleItem->GetIsEnabled() && ModuleItem->IsDebugDrawEnabled();
}

const FSlateBrush* SNiagaraOverviewStack::GetDebugIconBrush(UNiagaraStackItem* Item) const
{
	return IsModuleDebugDrawEnabled(Item)? 
		FNiagaraEditorStyle::Get().GetBrush(TEXT("NiagaraEditor.Overview.DebugActive")) :
		FNiagaraEditorStyle::Get().GetBrush(TEXT("NiagaraEditor.Overview.DebugInactive"));
}

FReply SNiagaraOverviewStack::ToggleModuleDebugDraw(UNiagaraStackItem* Item)
{
	UNiagaraStackModuleItem* ModuleItem = Cast<UNiagaraStackModuleItem>(Item);
	if (ModuleItem != nullptr)
	{
		ModuleItem->SetDebugDrawEnabled(!ModuleItem->IsDebugDrawEnabled());
	}

	return FReply::Handled();
}

void SNiagaraOverviewStack::OnSelectionChanged(UNiagaraStackEntry* InNewSelection, ESelectInfo::Type SelectInfo)
{
	if (bUpdatingStackSelectionFromOverviewSelection == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingOverviewSelectionFromStackSelection, true);
		TArray<UNiagaraStackEntry*> SelectedEntries;
		EntryListView->GetSelectedItems(SelectedEntries);

		TArray<UNiagaraStackEntry*> DeselectedEntries;

		for (TWeakObjectPtr<UNiagaraStackEntry> PreviousSelectedEntry : PreviousSelection)
		{
			if (PreviousSelectedEntry.IsValid() && SelectedEntries.Contains(PreviousSelectedEntry.Get()) == false)
			{
				DeselectedEntries.Add(PreviousSelectedEntry.Get());
			}
		}

		bool bClearCurrentSelection = FSlateApplication::Get().GetModifierKeys().IsControlDown() == false;
		OverviewSelectionViewModel->UpdateSelectedEntries(SelectedEntries, DeselectedEntries, bClearCurrentSelection);

		PreviousSelection.Empty();
		for (UNiagaraStackEntry* SelectedEntry : SelectedEntries)
		{
			PreviousSelection.Add(SelectedEntry);
		}

		UpdateCommandContextSelection();
	}
}

void SNiagaraOverviewStack::SystemSelectionChanged()
{
	if (bUpdatingOverviewSelectionFromStackSelection == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingStackSelectionFromOverviewSelection, true);

		TArray<UNiagaraStackEntry*> SelectedListViewStackEntries;
		EntryListView->GetSelectedItems(SelectedListViewStackEntries);
		TArray<UNiagaraStackEntry*> SelectedOverviewEntries;
		OverviewSelectionViewModel->GetSelectedEntries(SelectedOverviewEntries);

		TArray<UNiagaraStackEntry*> EntriesToDeselect;
		for (UNiagaraStackEntry* SelectedListViewStackEntry : SelectedListViewStackEntries)
		{
			if (SelectedOverviewEntries.Contains(SelectedListViewStackEntry) == false)
			{
				EntriesToDeselect.Add(SelectedListViewStackEntry);
			}
		}

		TArray<UNiagaraStackEntry*> EntriesToSelect;
		RefreshEntryList();
		for (UNiagaraStackEntry* SelectedOverviewEntry : SelectedOverviewEntries)
		{
			if (FlattenedEntryList.Contains(SelectedOverviewEntry))
			{
				EntriesToSelect.Add(SelectedOverviewEntry);
			}
		}

		for (UNiagaraStackEntry* EntryToDeselect : EntriesToDeselect)
		{
			EntryListView->SetItemSelection(EntryToDeselect, false);
		}

		for (UNiagaraStackEntry* EntryToSelect : EntriesToSelect)
		{
			EntryListView->SetItemSelection(EntryToSelect, true);
		}

		UpdateCommandContextSelection();
	}
}

void SNiagaraOverviewStack::UpdateCommandContextSelection()
{
	TArray<UNiagaraStackEntry*> SelectedListViewStackEntries;
	EntryListView->GetSelectedItems(SelectedListViewStackEntries);
	StackCommandContext->SetSelectedEntries(SelectedListViewStackEntries);
}

FReply SNiagaraOverviewStack::OnRowDragDetected(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent, TWeakObjectPtr<UNiagaraStackEntry> InStackEntryWeak)
{
	UNiagaraStackEntry* StackEntry = InStackEntryWeak.Get();
	if (StackEntry != nullptr && StackEntry->CanDrag())
	{
		TArray<UNiagaraStackEntry*> EntriesToDrag;
		StackEntry->GetSystemViewModel()->GetSelectionViewModel()->GetSelectedEntries(EntriesToDrag);
		EntriesToDrag.AddUnique(StackEntry);
		return FReply::Handled().BeginDragDrop(FNiagaraStackEditorWidgetsUtilities::ConstructDragDropOperationForStackEntries(EntriesToDrag));
	}
	return FReply::Unhandled();
}

void SNiagaraOverviewStack::OnRowDragLeave(const FDragDropEvent& InDragDropEvent)
{
	FNiagaraStackEditorWidgetsUtilities::HandleDragLeave(InDragDropEvent);
}

TOptional<EItemDropZone> SNiagaraOverviewStack::OnRowCanAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry)
{
	return FNiagaraStackEditorWidgetsUtilities::RequestDropForStackEntry(InDragDropEvent, InDropZone, InTargetEntry, UNiagaraStackEntry::EDropOptions::Overview);
}

FReply SNiagaraOverviewStack::OnRowAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry)
{
	bool bHandled = FNiagaraStackEditorWidgetsUtilities::HandleDropForStackEntry(InDragDropEvent, InDropZone, InTargetEntry, UNiagaraStackEntry::EDropOptions::Overview);
	return bHandled ? FReply::Handled() : FReply::Unhandled();
}

EVisibility SNiagaraOverviewStack::GetIssueIconVisibility() const
{
	return StackViewModel->HasIssues() ? EVisibility::Visible : EVisibility::Collapsed;
}

void SNiagaraOverviewStack::OnItemGroupEnabledStateChanged(ECheckBoxState InCheckState, UNiagaraStackItemGroup* Group)
{
	Group->SetIsEnabled(InCheckState == ECheckBoxState::Checked);
}

ECheckBoxState SNiagaraOverviewStack::ItemGroupCheckEnabledStatus(UNiagaraStackItemGroup* Group) const
{
	return Group->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SNiagaraOverviewStack::GetItemGroupEnabledCheckboxEnabled(UNiagaraStackItemGroup* Group) const
{
	return Group->GetOwnerIsEnabled();
}

FText SNiagaraOverviewStack::GetItemGroupDeleteButtonToolTip(UNiagaraStackItemGroup* Group) const
{
	FText Message;
	Group->TestCanDeleteWithMessage(Message);
	return Message;
}

bool SNiagaraOverviewStack::GetItemGroupDeleteButtonIsEnabled(UNiagaraStackItemGroup* Group) const
{
	FText UnusedMessage;
	return Group->TestCanDeleteWithMessage(UnusedMessage);
}

EVisibility SNiagaraOverviewStack::GetItemGroupDeleteButtonVisibility(UNiagaraStackItemGroup* Group) const
{
	return Group->SupportsDelete() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SNiagaraOverviewStack::OnItemGroupDeleteClicked(UNiagaraStackItemGroup* Group)
{
	Group->Delete();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
