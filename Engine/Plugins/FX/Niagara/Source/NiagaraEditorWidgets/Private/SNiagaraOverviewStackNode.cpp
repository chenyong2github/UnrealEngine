// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewStackNode.h"
#include "NiagaraOverviewNode.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "SNiagaraOverviewStack.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "Stack/SNiagaraStackIssueIcon.h"

#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewStackNode"

void SNiagaraOverviewStackNode::Construct(const FArguments& InArgs, UNiagaraOverviewNode* InNode)
{
	GraphNode = InNode;
	OverviewStackNode = InNode;
	StackViewModel = nullptr;
	OverviewSelectionViewModel = nullptr;
	EmitterHandleViewModelWeak.Reset();

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
		.Padding(4, 0, 0, 0)
		.FillWidth(1.0f)
		[
			DefaultTitle
		]
		// Stack issues icon
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(5, 0, 0, 0)
		[
			SNew(SNiagaraStackIssueIcon, StackViewModel, StackViewModel->GetRootEntry())
			.Visibility(this, &SNiagaraOverviewStackNode::GetIssueIconVisibility)
		];
}

TSharedRef<SWidget> SNiagaraOverviewStackNode::CreateNodeContentArea()
{
	TSharedPtr<SWidget> ContentWidget;
	if (StackViewModel != nullptr && OverviewSelectionViewModel != nullptr)
	{
		ContentWidget = SNew(SBox)
			.WidthOverride(210)
			[
				SNew(SNiagaraOverviewStack, *StackViewModel, *OverviewSelectionViewModel)
			];
	}
	else
	{
		ContentWidget = SNullWidget::NullWidget;
	}

	// NODE CONTENT AREA
	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(2, 2, 2, 4))
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

#undef LOCTEXT_NAMESPACE