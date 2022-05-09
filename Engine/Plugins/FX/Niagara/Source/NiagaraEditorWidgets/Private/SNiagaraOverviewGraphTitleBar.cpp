// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewGraphTitleBar.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraSettings.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "SNiagaraScalabilityPreviewSettings.h"
#include "SNiagaraSystemEffectTypeBar.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewGraphTitleBar"

void SNiagaraOverviewGraphTitleBar::Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModel = InSystemViewModel;

	bScalabilityModeActive = SystemViewModel.Pin()->GetScalabilityViewModel()->IsActive();
	SystemViewModel.Pin()->GetScalabilityViewModel()->OnScalabilityModeChanged().AddSP(this, &SNiagaraOverviewGraphTitleBar::OnUpdateScalabilityMode);

	RebuildWidget();
}

void SNiagaraOverviewGraphTitleBar::RebuildWidget()
{
	ContainerWidget = SNew(SVerticalBox);

	// we only allow the Niagara system effect type bar on system assets
	if(bScalabilityModeActive && SystemViewModel.IsValid() && SystemViewModel.Pin()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		ContainerWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.TitleBackground")))
			.HAlign(HAlign_Fill)
			[
				SNew(SNiagaraSystemEffectTypeBar, SystemViewModel.Pin()->GetSystem())
			]
		];
	}
	
	ContainerWidget->AddSlot()
	.HAlign(HAlign_Fill)
	.AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Graph.TitleBackground")))
		.HAlign(HAlign_Fill)
		[
			SNew(STextBlock)
			.Text(SystemViewModel.Pin()->GetOverviewGraphViewModel().Get(), &FNiagaraOverviewGraphViewModel::GetDisplayName)
			.TextStyle(FAppStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
			.Justification(ETextJustify::Center)
		]
	];
	
	if(bScalabilityModeActive)
	{
		ContainerWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.Padding(5.f)
		.AutoHeight()
		[			
			SNew(SNiagaraScalabilityPreviewSettings, *SystemViewModel.Pin()->GetScalabilityViewModel()).Visibility(EVisibility::SelfHitTestInvisible)			
		];
	}

	ChildSlot
	[
		ContainerWidget.ToSharedRef()
	];
}

// FReply SNiagaraOverviewGraphTitleBar::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
// {
// 	if(OverviewGraph.IsValid())
// 	{
// 		//OverviewGraph->g
// 	}
//
// 	return FReply::Handled();
// }

void SNiagaraOverviewGraphTitleBar::OnUpdateScalabilityMode(bool bActive)
{
	bScalabilityModeActive = bActive;
	RebuildWidget();
}

#undef LOCTEXT_NAMESPACE
