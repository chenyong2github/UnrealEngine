// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewStackNode.h"
#include "NiagaraOverviewNode.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "SNiagaraOverviewStack.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "Stack/SNiagaraStackIssueIcon.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "AssetThumbnail.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SBoxPanel.h"
#include "NiagaraRendererProperties.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewStackNode"

const float ThumbnailSize = 24.0f;

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
			if (StackViewModel)
			{
				StackViewModel->OnDataObjectChanged().AddRaw(this, &SNiagaraOverviewStackNode::FillThumbnailBar, true);
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
		// Isolate toggle
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.HAlign(HAlign_Center)
			.ContentPadding(1)
			.ToolTipText(this, &SNiagaraOverviewStackNode::GetToggleIsolateToolTip)
			.OnClicked(this, &SNiagaraOverviewStackNode::OnToggleIsolateButtonClicked)
			.Visibility(this, &SNiagaraOverviewStackNode::GetToggleIsolateVisibility)
			.Content()
			[
				SNew(SImage)
				.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Isolate"))
				.ColorAndOpacity(this, &SNiagaraOverviewStackNode::GetToggleIsolateImageColor)
			]
		]
		// Name
		+ SHorizontalBox::Slot()
		.Padding(3, 0, 0, 0)
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

TSharedRef<SWidget> SNiagaraOverviewStackNode::CreateThumbnailWidget(float InThumbnailSize, FRendererPreviewData* InData)
{
	TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(InData->RenderingObject, InThumbnailSize, InThumbnailSize, ThumbnailPool));
	TSharedRef<SWidget> ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget();
	TSharedPtr<FAssetThumbnail> TooltipThumbnail = MakeShareable(new FAssetThumbnail(InData->RenderingObject, 64.0f, 64.0f, ThumbnailPool));
	TSharedRef<SToolTip> ThumbnailTooltipWidget = SNew(SToolTip)
		.Content()
		[
			SNew(SBox)
			.MinDesiredHeight(64.0f)
			.MaxDesiredHeight(64.0f)
			.MinDesiredWidth(64.0f)
			.MaxDesiredWidth(64.0f)
			[
				TooltipThumbnail->MakeThumbnailWidget()
			]
		];
	ThumbnailWidget->SetOnMouseButtonDown(FPointerEventHandler::CreateSP(this, &SNiagaraOverviewStackNode::OnClickedRenderingPreview, InData->RenderingEntry));
	ThumbnailWidget->SetToolTip(ThumbnailTooltipWidget);
	return ThumbnailWidget;
}

FReply SNiagaraOverviewStackNode::OnClickedRenderingPreview(const FGeometry& InGeometry, const FPointerEvent& InEvent, UNiagaraStackEntry* InEntry)
{
	if (InEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		TArray<UNiagaraStackEntry*> SelectedEntries;
		SelectedEntries.Add(InEntry);
		TArray<UNiagaraStackEntry*> DeselectedEntries;
		OverviewSelectionViewModel->UpdateSelectedEntries(SelectedEntries, DeselectedEntries, true);
		return FReply::Handled();
	}
	return FReply::Unhandled();
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

	ThumbnailBar = SNew(SHorizontalBox);
	FillThumbnailBar(nullptr, false);

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
				ThumbnailBar.ToSharedRef()
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

void SNiagaraOverviewStackNode::FillThumbnailBar(UObject* ChangedObject, const bool bIsTriggeredByObjectUpdate)
{
	UNiagaraRendererProperties* RendererProperties = Cast< UNiagaraRendererProperties>(ChangedObject);
	if (!bIsTriggeredByObjectUpdate || RendererProperties != nullptr)
	{
		ThumbnailBar->ClearChildren();
		ThumbnailPool = MakeShared<FAssetThumbnailPool>(100, /*InAreRealTileThumbnailsAllowed=*/true);
		if (EmitterHandleViewModelWeak.IsValid())
		{
			EmitterHandleViewModelWeak.Pin()->GetRendererPreviewData(PreviewData);

			for (FRendererPreviewData* Preview : PreviewData)
			{
				if (Preview->RenderingObject)
				{
					ThumbnailBar->AddSlot()
						.AutoWidth()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.Padding(2.0f, 2.0f, 4.0f, 4.0f)
						.MaxWidth(ThumbnailSize)
						[
							SNew(SBox)
							.MinDesiredHeight(ThumbnailSize)
							.MaxDesiredHeight(ThumbnailSize)
							.MinDesiredWidth(ThumbnailSize)
							.Visibility(this, &SNiagaraOverviewStackNode::GetEnabledCheckBoxVisibility)
							[
								CreateThumbnailWidget(ThumbnailSize, Preview)
							]
						];
				}
			}
		}

		ThumbnailBar->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			];
	}
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

#undef LOCTEXT_NAMESPACE