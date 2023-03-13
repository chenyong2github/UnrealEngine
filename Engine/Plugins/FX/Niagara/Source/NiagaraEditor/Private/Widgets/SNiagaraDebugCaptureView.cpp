// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraDebugCaptureView.h"

#include "PropertyEditorModule.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"

#define LOCTEXT_NAMESPACE "NiagaraDebugCaptureView"

void SNiagaraDebugCaptureView::OnNumFramesChanged(int32 InNumFrames)
{
	NumFrames = FMath::Max(1, InNumFrames);
}

void SNiagaraDebugCaptureView::Construct(const FArguments& InArgs, const TSharedRef<FNiagaraSystemViewModel> InSystemViewModel, const TSharedRef<FNiagaraSimCacheViewModel> InSimCacheViewModel)
{
	TargetComponent = InSystemViewModel->GetPreviewComponent();
	SimCacheViewModel = InSimCacheViewModel;
	SystemViewModel = InSystemViewModel;

	CapturedCache = NewObject<UNiagaraSimCache>(GetTransientPackage());
	SimCacheViewModel.Get()->Initialize(CapturedCache);
	CapturedCache->SetFlags(RF_Standalone);

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bShowOptions = false;
	
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Vertical)
		+SSplitter::Slot()
		.SizeRule(SSplitter::SizeToContent)
		[
			// Capture controls
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+SSplitter::Slot()
			.Resizable(false)
			.SizeRule(SSplitter::SizeToContent)
			[
				// Header
				SNew(SBorder)
				.BorderImage(FAppStyle::GetNoBrush())
				.Padding(5.0f)
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Center)
					.Text(LOCTEXT("CaptureControlsLabel", "Capture"))
				]
			]
			+SSplitter::Slot()
			.SizeRule(SSplitter::SizeToContent)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(2.0f)
				// Capture Single Frame
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.OnClicked(this, &SNiagaraDebugCaptureView::OnSingleFrameSelected)
					.ToolTipText(LOCTEXT("SingleFrameButtonTooltip", "Capture a single frame."))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SingleFrameButton", "Single"))
					]
				]
				// Capture Multi Frame
				+SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.OnClicked(this, &SNiagaraDebugCaptureView::OnMultiFrameSelected)
					.ToolTipText(LOCTEXT("MultiFrameButtonTooltip", "Capture multiple frames."))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MultiFrameButton", "Multi"))
					]
				]
				// Capture Until Complete
				+SUniformGridPanel::Slot(2, 0)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NumberOfFrames", "Frames"))
					]
					+SHorizontalBox::Slot()
					[
						SNew(SSpinBox<int32>)
						.Value(1)
						.MinValue(1)
						.OnValueChanged(this, &SNiagaraDebugCaptureView::OnNumFramesChanged)
					]
				]
			]
		]
		+SSplitter::Slot()
		.Value(0.99f)
		[
			SNew(SNiagaraSimCacheOverview)
			.SimCacheViewModel(SimCacheViewModel)
		]
	];
}

SNiagaraDebugCaptureView::~SNiagaraDebugCaptureView()
{
	if(CapturedCache)
	{
		CapturedCache->ClearFlags(RF_Standalone);
		CapturedCache->MarkAsGarbage();
	}
}

FReply SNiagaraDebugCaptureView::OnSingleFrameSelected()
{
	if(!bIsCaptureActive && TargetComponent && CapturedCache)
	{
		SystemViewModel.Get()->GetSequencer().Get()->OnPlay(false);

		const FNiagaraSimCacheCreateParameters CreateParameters;

		SimCacheCapture.CaptureCurrentFrameImmediate(CapturedCache, CreateParameters, TargetComponent, CapturedCache, true);

		if(CapturedCache)
		{
			OnCaptureComplete(CapturedCache);
		}
		
	}
	return FReply::Handled();
}

FReply SNiagaraDebugCaptureView::OnMultiFrameSelected()
{
	if(!bIsCaptureActive && TargetComponent && CapturedCache)
	{
		const FNiagaraSimCacheCreateParameters CreateParameters;
		
		FNiagaraSimCacheCaptureParameters CaptureParameters;
		CaptureParameters.NumFrames = NumFrames;

		SystemViewModel.Get()->GetSequencer().Get()->OnPlay(false);
		
		bIsCaptureActive = true;

		SimCacheCapture.CaptureNiagaraSimCache(CapturedCache, CreateParameters, TargetComponent, CaptureParameters);
		
		SimCacheCapture.OnCaptureComplete().AddSP(this, &SNiagaraDebugCaptureView::OnCaptureComplete);
	}

	return FReply::Handled();
}

void SNiagaraDebugCaptureView::OnCaptureComplete(UNiagaraSimCache* CapturedSimCache)
{
	bIsCaptureActive = false;
	SystemViewModel.Get()->GetSequencer().Get()->Pause();
				
	TArray<FGuid> SelectedEmitterHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();

	if(SelectedEmitterHandleIds.Num())
	{
		TSharedPtr<FNiagaraEmitterHandleViewModel> SelectedEmitterHandleViewModel = SystemViewModel->GetEmitterHandleViewModelById(SelectedEmitterHandleIds[0]);

		if(SelectedEmitterHandleViewModel.IsValid())
		{
			for(int32 i = 0; i < SimCacheViewModel->GetNumEmitterLayouts(); ++i)
			{
				if(SimCacheViewModel->GetEmitterLayoutName(i) == SelectedEmitterHandleViewModel->GetName())
				{
					if(SimCacheViewModel->GetEmitterIndex() != i)
					{
						SimCacheViewModel->SetEmitterIndex(i);
					}
					break;
				}
			}
		}
	}

	RequestSpreadsheetTab.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
