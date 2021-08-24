// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewStackNode.h"
#include "NiagaraOverviewNode.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "SNiagaraOverviewStack.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEmitter.h"
#include "Stack/SNiagaraStackIssueIcon.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "AssetThumbnail.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SBoxPanel.h"
#include "NiagaraRendererProperties.h"
#include "Widgets/SWidget.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ViewModels/Stack/NiagaraStackRendererItem.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewStackNode"

const float ThumbnailSize = 24.0f;

void SNiagaraOverviewStackNode::Construct(const FArguments& InArgs, UNiagaraOverviewNode* InNode)
{
	GraphNode = InNode;
	OverviewStackNode = InNode;
	StackViewModel = nullptr;
	OverviewSelectionViewModel = nullptr;
	bIsHoveringThumbnail = false;
	bTopContentBarRefreshPending = true;
	CurrentIssueIndex = -1;

	EmitterHandleViewModelWeak.Reset();
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(10, TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SNiagaraOverviewStackNode::IsHoveringThumbnail)));
	TopContentBar = SNew(SHorizontalBox);
	
	if (OverviewStackNode->GetOwningSystem() != nullptr)
	{
		FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::Get().LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
		TSharedPtr<FNiagaraSystemViewModel> OwningSystemViewModel = NiagaraEditorModule.GetExistingViewModelForSystem(OverviewStackNode->GetOwningSystem());
		if (OwningSystemViewModel.IsValid())
		{
			if (OverviewStackNode->GetEmitterHandleGuid().IsValid() == false)
			{
				StackViewModel = OwningSystemViewModel->GetSystemStackViewModel();
			}
			else
			{
				EmitterHandleViewModelWeak = OwningSystemViewModel->GetEmitterHandleViewModelById(OverviewStackNode->GetEmitterHandleGuid());
				if (EmitterHandleViewModelWeak.IsValid())
				{
					StackViewModel = EmitterHandleViewModelWeak.Pin()->GetEmitterStackViewModel();
				}
			}
			if (StackViewModel)
			{
				StackViewModel->OnStructureChanged().AddSP(this, &SNiagaraOverviewStackNode::StackViewModelStructureChanged);
				StackViewModel->OnDataObjectChanged().AddSP(this, &SNiagaraOverviewStackNode::StackViewModelDataObjectChanged);
			}
			UMaterial::OnMaterialCompilationFinished().AddSP(this, &SNiagaraOverviewStackNode::OnMaterialCompiled);
			OverviewSelectionViewModel = OwningSystemViewModel->GetSelectionViewModel();
		}
	}

	UpdateGraphNode();
}

TSharedRef<SWidget> SNiagaraOverviewStackNode::CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle)
{
	TSharedRef<SWidget> DefaultTitle = SGraphNode::CreateTitleWidget(NodeTitle);

	if (StackViewModel == nullptr)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0, 0, 5, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidNode", "INVALID"))
			]
			+ SHorizontalBox::Slot()
			[
				DefaultTitle
			];
	}

	return SNew(SHorizontalBox)
		// Enabled checkbox
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0)
		[
			SNew(SCheckBox)
			.Visibility(this, &SNiagaraOverviewStackNode::GetEnabledCheckBoxVisibility)
			.IsChecked(this, &SNiagaraOverviewStackNode::GetEnabledCheckState)
			.OnCheckStateChanged(this, &SNiagaraOverviewStackNode::OnEnabledCheckStateChanged)
		]
		// Name
		+ SHorizontalBox::Slot()
		.Padding(3, 0, 0, 0)
		.FillWidth(1.0f)
		[
			DefaultTitle
		];
}

TSharedRef<SWidget> SNiagaraOverviewStackNode::CreateTitleRightWidget()
{
	if (StackViewModel == nullptr)
	{
		return SNullWidget::NullWidget;
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0, 0, 1, 0)
		[
			SNew(SButton)
			.IsFocusable(false)
			.ToolTipText(LOCTEXT("OpenAndFocusParentEmitterToolTip", "Open and Focus Parent Emitter"))
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.ForegroundColor"))
			.ContentPadding(2)
			.OnClicked(this, &SNiagaraOverviewStackNode::OpenParentEmitter)
			.Visibility(this, &SNiagaraOverviewStackNode::GetOpenParentEmitterVisibility)
			.DesiredSizeScale(FVector2D(14.0f / 30.0f, 14.0f / 30.0f)) // GoToSourceIcon is 30x30, scale down
			.Content()
			[
				SNew(SImage)
				.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.GoToSourceIcon"))
				.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0, 0, 1, 0)
		[
			SNew(SNiagaraStackIssueIcon, StackViewModel, StackViewModel->GetRootEntry())
			.Visibility(this, &SNiagaraOverviewStackNode::GetIssueIconVisibility)
			.OnClicked(this, &SNiagaraOverviewStackNode::OnCycleThroughIssues)
		];
}

FReply SNiagaraOverviewStackNode::OnCycleThroughIssues()
{
	const TArray<UNiagaraStackEntry*>& ChildrenWithIssues = StackViewModel->GetRootEntry()->GetAllChildrenWithIssues();
	if (ChildrenWithIssues.Num() > 0)
	{
		++CurrentIssueIndex;

		if (CurrentIssueIndex >= ChildrenWithIssues.Num())
		{
			CurrentIssueIndex = 0;
		}

		if (ChildrenWithIssues.IsValidIndex(CurrentIssueIndex))
		{
			UNiagaraStackEntry* ChildIssue = ChildrenWithIssues[CurrentIssueIndex];
			UNiagaraStackEntry* ChildToSelect = Cast<UNiagaraStackModuleItem>(ChildIssue);
			if (ChildToSelect == nullptr)
			{
				ChildToSelect = ChildIssue->GetTypedOuter<UNiagaraStackModuleItem>();
			}

			if (ChildToSelect == nullptr)
			{
				ChildToSelect = Cast<UNiagaraStackItemGroup>(ChildIssue);
			}
			
			if (ChildToSelect != nullptr)
			{
				OverviewSelectionViewModel->UpdateSelectedEntries(TArray<UNiagaraStackEntry*> { ChildToSelect }, TArray<UNiagaraStackEntry*>(), true /* bClearCurrentSelection */ );
			}
		}
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SNiagaraOverviewStackNode::CreateThumbnailWidget(UNiagaraStackEntry* InData, TSharedPtr<SWidget> InWidget, TSharedPtr<SWidget> InTooltipWidget)
{
	TSharedPtr<SToolTip> ThumbnailTooltipWidget;
	// If this is just text, don't constrain the size
	if (InTooltipWidget->GetType() == TEXT("STextBlock"))
	{
		ThumbnailTooltipWidget = SNew(SToolTip)
			.Content()
			[
				InTooltipWidget.ToSharedRef()
			];
	}
	else
	{

		ThumbnailTooltipWidget = SNew(SToolTip)
			.Content()
			[
				SNew(SBox)
				.MaxDesiredHeight(64.0f)
				.MinDesiredHeight(64.0f)
				.MaxDesiredWidth(64.0f)
				.MinDesiredWidth(64.0f)
				[
					InTooltipWidget.ToSharedRef()
				]
			];
	}
	InWidget->SetOnMouseButtonDown(FPointerEventHandler::CreateSP(this, &SNiagaraOverviewStackNode::OnClickedRenderingPreview, InData));
	InWidget->SetOnMouseEnter(FNoReplyPointerEventHandler::CreateSP(this, &SNiagaraOverviewStackNode::SetIsHoveringThumbnail, true));
	InWidget->SetOnMouseLeave(FSimpleNoReplyPointerEventHandler::CreateSP(this, &SNiagaraOverviewStackNode::SetIsHoveringThumbnail, false));
	InWidget->SetToolTip(ThumbnailTooltipWidget);

	return InWidget.ToSharedRef();
}

FReply SNiagaraOverviewStackNode::OnClickedRenderingPreview(const FGeometry& InGeometry, const FPointerEvent& InEvent, UNiagaraStackEntry* InEntry)
{
	if (InEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		TArray<UNiagaraStackEntry*> SelectedEntries;
		SelectedEntries.Add(InEntry);
		TArray<UNiagaraStackEntry*> DeselectedEntries;
		OverviewSelectionViewModel->UpdateSelectedEntries(SelectedEntries, DeselectedEntries, true);

		CurrentIssueIndex = -1;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SNiagaraOverviewStackNode::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (OverviewStackNode != nullptr)
	{
		if (OverviewStackNode->IsRenamePending() && !SGraphNode::IsRenamePending())
		{
			SGraphNode::RequestRename();
			OverviewStackNode->RenameStarted();
		}

		if (bTopContentBarRefreshPending)
		{
			FillTopContentBar();
			bTopContentBarRefreshPending = false;
		}
	}
}

void SNiagaraOverviewStackNode::OnMaterialCompiled(class UMaterialInterface* MaterialInterface)
{
	bool bUsingThisMaterial = false;
	if (EmitterHandleViewModelWeak.IsValid())
	{
		EmitterHandleViewModelWeak.Pin()->GetRendererEntries(PreviewStackEntries);
		FNiagaraEmitterInstance* InInstance = EmitterHandleViewModelWeak.Pin()->GetEmitterViewModel()->GetSimulation().IsValid() ? EmitterHandleViewModelWeak.Pin()->GetEmitterViewModel()->GetSimulation().Pin().Get() : nullptr;
		for (UNiagaraStackEntry* Entry : PreviewStackEntries)
		{
			if (UNiagaraStackRendererItem* RendererItem = Cast<UNiagaraStackRendererItem>(Entry))
			{
				TArray<UMaterialInterface*> Materials;
				RendererItem->GetRendererProperties()->GetUsedMaterials(InInstance, Materials);
				if (Materials.Contains(MaterialInterface))
				{
					bUsingThisMaterial = true;
					break;
				}
			}
		}

		if (bUsingThisMaterial)
		{
			bTopContentBarRefreshPending = true;
		}
	}
}

TSharedRef<SWidget> SNiagaraOverviewStackNode::CreateNodeContentArea()
{
	TSharedPtr<SWidget> ContentWidget;
	if (StackViewModel != nullptr && OverviewSelectionViewModel != nullptr)
	{
		ContentWidget = SNew(SBox)
			.MaxDesiredWidth(300)
			[
				SNew(SNiagaraOverviewStack, *StackViewModel, *OverviewSelectionViewModel)
			];
	}
	else
	{
		ContentWidget = SNullWidget::NullWidget;
	}

	FillTopContentBar();

	// NODE CONTENT AREA
	TSharedRef<SWidget> NodeWidget = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(2, 2, 2, 4))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f)
			[
				TopContentBar.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(0.0f)
			[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				// LEFT
				SAssignNew(LeftNodeBox, SVerticalBox)
			]
			+SHorizontalBox::Slot()
			[
				SNew(SBorder)
				.BorderImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.SystemOverview.NodeBackgroundBorder"))
				.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.SystemOverview.NodeBackgroundColor"))
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(FMargin(0, 0, 0, 4))
				[
					ContentWidget.ToSharedRef()
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				// RIGHT
				SAssignNew(RightNodeBox, SVerticalBox)
			]
			]
		];


	return NodeWidget;

}

void SNiagaraOverviewStackNode::StackViewModelStructureChanged(ENiagaraStructureChangedFlags Flags)
{
	bTopContentBarRefreshPending = true;
}

void SNiagaraOverviewStackNode::StackViewModelDataObjectChanged(TArray<UObject*> ChangedObjects, ENiagaraDataObjectChange ChangeType)
{
	for (UObject* ChangedObject : ChangedObjects)
	{
		if (ChangedObject->IsA<UNiagaraRendererProperties>())
		{
			bTopContentBarRefreshPending = true;
			break;
		}
	}
}

void SNiagaraOverviewStackNode::FillTopContentBar()
{
	if (TopContentBar.IsValid() && TopContentBar->GetChildren())
	{
		TopContentBar->ClearChildren();
	}
	if (EmitterHandleViewModelWeak.IsValid())
	{
	
		// Isolate toggle button
		TopContentBar->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.HAlign(HAlign_Center)
				.ContentPadding(1)
				.ToolTipText(this, &SNiagaraOverviewStackNode::GetToggleIsolateToolTip)
				.OnClicked(this, &SNiagaraOverviewStackNode::OnToggleIsolateButtonClicked)
				.Visibility(this, &SNiagaraOverviewStackNode::GetToggleIsolateVisibility)
				.IsFocusable(false)
				.Content()
				[
					SNew(SImage)
					.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Isolate"))
					.ColorAndOpacity(this, &SNiagaraOverviewStackNode::GetToggleIsolateImageColor)
				]
			];

		EmitterHandleViewModelWeak.Pin()->GetRendererEntries(PreviewStackEntries);
		FNiagaraEmitterInstance* InInstance = EmitterHandleViewModelWeak.Pin()->GetEmitterViewModel()->GetSimulation().IsValid() ? EmitterHandleViewModelWeak.Pin()->GetEmitterViewModel()->GetSimulation().Pin().Get() : nullptr;

		FToolBarBuilder ToolBarBuilder(nullptr, FMultiBoxCustomization::None, nullptr, true);
		ToolBarBuilder.SetStyle(&FNiagaraEditorStyle::Get(), FName("OverviewStackNodeThumbnailToolBar"));
		ToolBarBuilder.SetLabelVisibility(EVisibility::Collapsed);
		
		for (int32 StackEntryIndex = 0; StackEntryIndex < PreviewStackEntries.Num(); StackEntryIndex++)
		{
			UNiagaraStackEntry* Entry = PreviewStackEntries[StackEntryIndex];
			if (UNiagaraStackRendererItem* RendererItem = Cast<UNiagaraStackRendererItem>(Entry))
			{
				TArray<TSharedPtr<SWidget>> Widgets;
				RendererItem->GetRendererProperties()->GetRendererWidgets(InInstance, Widgets, ThumbnailPool);
				TArray<TSharedPtr<SWidget>> TooltipWidgets;
				RendererItem->GetRendererProperties()->GetRendererTooltipWidgets(InInstance, TooltipWidgets, ThumbnailPool);

				for (int32 WidgetIndex = 0; WidgetIndex < Widgets.Num(); WidgetIndex++)
				{
					ToolBarBuilder.AddWidget(
						SNew(SBox)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.MinDesiredHeight(ThumbnailSize)
						.MinDesiredWidth(ThumbnailSize)
						.MaxDesiredHeight(ThumbnailSize)
						.MaxDesiredWidth(ThumbnailSize)
						.Visibility(this, &SNiagaraOverviewStackNode::GetEnabledCheckBoxVisibility)
						[
							CreateThumbnailWidget(Entry, Widgets[WidgetIndex], TooltipWidgets[WidgetIndex])
						]
					);
				}

				// if we had a widget for this entry, add a separator for the next entry's widgets, except for the last entry
				if(Widgets.Num() > 0 && StackEntryIndex < PreviewStackEntries.Num() - 1)
				{
					ToolBarBuilder.AddSeparator();
				}
			}
		}

		TopContentBar->AddSlot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.MaxWidth(300.f)
		[
			ToolBarBuilder.MakeWidget()
		];
	}

	TopContentBar->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		];
}

EVisibility SNiagaraOverviewStackNode::GetIssueIconVisibility() const
{
	return StackViewModel->HasIssues() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SNiagaraOverviewStackNode::GetEnabledCheckBoxVisibility() const
{
	return EmitterHandleViewModelWeak.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

ECheckBoxState SNiagaraOverviewStackNode::GetEnabledCheckState() const
{
	return EmitterHandleViewModelWeak.IsValid() && EmitterHandleViewModelWeak.Pin()->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNiagaraOverviewStackNode::OnEnabledCheckStateChanged(ECheckBoxState InCheckState)
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();
	if (EmitterHandleViewModel.IsValid())
	{
		EmitterHandleViewModel->SetIsEnabled(InCheckState == ECheckBoxState::Checked);
	}
}

FReply SNiagaraOverviewStackNode::OnToggleIsolateButtonClicked()
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();
	if (EmitterHandleViewModel.IsValid())
	{
		bool bShouldBeIsolated = !EmitterHandleViewModel->GetIsIsolated();
		EmitterHandleViewModel->SetIsIsolated(bShouldBeIsolated);
	}
	return FReply::Handled();
}

FText SNiagaraOverviewStackNode::GetToggleIsolateToolTip() const
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();
	return EmitterHandleViewModel.IsValid() && EmitterHandleViewModel->GetIsIsolated()
		? LOCTEXT("TurnOffEmitterIsolation", "Disable emitter isolation.")
		: LOCTEXT("IsolateThisEmitter", "Enable isolation for this emitter.");
}

EVisibility SNiagaraOverviewStackNode::GetToggleIsolateVisibility() const
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();
	return EmitterHandleViewModel.IsValid() &&
		EmitterHandleViewModel->GetOwningSystemEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset 
		? EVisibility::Visible 
		: EVisibility::Collapsed;
}

FSlateColor SNiagaraOverviewStackNode::GetToggleIsolateImageColor() const
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();
	return EmitterHandleViewModel.IsValid() && 
		EmitterHandleViewModel->GetIsIsolated()
		? FEditorStyle::GetSlateColor("SelectionColor")
		: FLinearColor::Gray;
}

FReply SNiagaraOverviewStackNode::OpenParentEmitter()
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();

	if (EmitterHandleViewModel.IsValid())
	{
		UNiagaraEmitter* ParentEmitter = const_cast<UNiagaraEmitter*>(EmitterHandleViewModel->GetEmitterViewModel()->GetParentEmitter());
		if (ParentEmitter != nullptr)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ParentEmitter);
		}
	}
	return FReply::Handled();
}

EVisibility SNiagaraOverviewStackNode::GetOpenParentEmitterVisibility() const
{
	TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelWeak.Pin();
	return EmitterHandleViewModel.IsValid() && 
		EmitterHandleViewModel->GetEmitterViewModel()->GetParentEmitter() != nullptr
		? EVisibility::Visible 
		: EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE