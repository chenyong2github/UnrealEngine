// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewStackNode.h"
#include "NiagaraOverviewNode.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "SNiagaraOverviewStack.h"
#include "NiagaraEditorModule.h"
#include "Widgets/Layout/SBox.h"

#include "Modules/ModuleManager.h"

void SNiagaraOverviewStackNode::Construct(const FArguments& InArgs, UNiagaraOverviewNode* InNode)
{
	GraphNode = InNode;
	OverviewStackNode = InNode;
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
				TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleviewModel = OwningSystemViewModel->GetEmitterHandleViewModelById(OverviewStackNode->GetEmitterHandleGuid());
				if (EmitterHandleviewModel.IsValid())
				{
					StackViewModel = EmitterHandleviewModel->GetEmitterStackViewModel();
				}
			}
			OverviewSelectionViewModel = OwningSystemViewModel->GetSelectionViewModel();
		}
	}
	UpdateGraphNode();
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
		.BorderImage( FEditorStyle::GetBrush("NoBorder") )
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding( FMargin(0,3) )
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
				ContentWidget.ToSharedRef()
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