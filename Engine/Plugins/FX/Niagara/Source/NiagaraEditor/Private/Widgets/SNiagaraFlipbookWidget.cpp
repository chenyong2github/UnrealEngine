// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraFlipbookWidget.h"
#include "SNiagaraFlipbookTimelineWidget.h"
#include "SNiagaraFlipbookViewport.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ViewModels/NiagaraFlipbookViewModel.h"

#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "ITransportControl.h"
#include "EditorWidgetsModule.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraFlipbookWidget"

//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////

void SNiagaraFlipbookWidget::Construct(const FArguments& InArgs)
{
	WeakViewModel = InArgs._WeakViewModel;

	//-TODO: Add tool bar to set options for the pane

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bHideSelectionTip = true;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::Get().LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");

	FlipbookSettingsDetails = PropertyModule.CreateDetailView(DetailsArgs);

	// Transport control args
	{
		FTransportControlArgs TransportControlArgs;
		TransportControlArgs.OnGetPlaybackMode.BindLambda([&]() -> EPlaybackMode::Type { return bIsPlaying ? EPlaybackMode::PlayingForward : EPlaybackMode::Stopped; } );
		TransportControlArgs.OnBackwardEnd.BindSP(this, &SNiagaraFlipbookWidget::OnTransportBackwardEnd);
		TransportControlArgs.OnBackwardStep.BindSP(this, &SNiagaraFlipbookWidget::OnTransportBackwardStep);
		TransportControlArgs.OnForwardPlay.BindSP(this, &SNiagaraFlipbookWidget::OnTransportForwardPlay);
		TransportControlArgs.OnForwardStep.BindSP(this, &SNiagaraFlipbookWidget::OnTransportForwardStep);
		TransportControlArgs.OnForwardEnd.BindSP(this, &SNiagaraFlipbookWidget::OnTransportForwardEnd);

		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::BackwardEnd));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::BackwardStep));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::ForwardPlay));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::ForwardStep));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::ForwardEnd));

		TransportControlArgs.bAreButtonsFocusable = false;

		TransportControls = EditorWidgetsModule.CreateTransportControl(TransportControlArgs);
	}

	//////////////////////////////////////////////////////////////////////////
	// Widgets
	this->ChildSlot
	[
		SNew(SVerticalBox)
		// Viewport
		+SVerticalBox::Slot()
		.FillHeight(0.60f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(ViewportWidget, SNiagaraFlipbookViewport)
				.WeakViewModel(WeakViewModel)
			]
		]
		// Timeline
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		.VAlign(VAlign_Fill)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(10.f, 0.f, 0.f, 0.f)
			.AutoWidth()
			[
				TransportControls.ToSharedRef()
			]
			+SHorizontalBox::Slot()
			[
				SAssignNew(TimelineWidget, SNiagaraFlipbookTimelineWidget)
				.WeakViewModel(WeakViewModel)
			]
		]
		// Settings Panel
		+SVerticalBox::Slot()
		.FillHeight(0.40f)
		[
			FlipbookSettingsDetails.ToSharedRef()
		]
	];

	if ( auto ViewModel = WeakViewModel.Pin() )
	{
		if ( UNiagaraComponent* PreviewComponent = ViewModel->GetPreviewComponent() )
		{
			if ( UNiagaraSystem* NiagaraSystem = PreviewComponent->GetAsset() )
			{
				FlipbookSettingsDetails->SetObject(NiagaraSystem->GetFlipbookSettings());
			}
		}
	}
}

FText SNiagaraFlipbookWidget::GetSelectedTextureAsText() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		if ( const UNiagaraFlipbookSettings* FlipbookSettings = ViewModel->GetFlipbookSettings() )
		{
			const int32 PrevewTextureIndex = ViewModel->GetPreviewTextureIndex();
			if ( FlipbookSettings->OutputTextures.IsValidIndex(PrevewTextureIndex) )
			{
				FString TextureName;
				if (FlipbookSettings->OutputTextures[PrevewTextureIndex].OutputName.IsNone())
				{
					TextureName = FString::Printf(TEXT("Output Texture %d"), PrevewTextureIndex);
				}
				else
				{
					TextureName = FlipbookSettings->OutputTextures[PrevewTextureIndex].OutputName.ToString();
				}
				return FText::FromString(TextureName);
			}
		}
	}
	return FText::FromString(FString(TEXT("No Texture")));
}

void SNiagaraFlipbookWidget::Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime)
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		float StartSeconds = 0.0f;
		float DurationSeconds = 0.0f;
		if ( ViewModel->IsGeneratedDataValid() )
		{
			StartSeconds = ViewModel->GetGeneratedStartSeconds();
			DurationSeconds = ViewModel->GetGeneratedDurationSeconds();
		}
		else if (UNiagaraFlipbookSettings* FlipbookSettings = GetFlipbookSettings())
		{
			StartSeconds = FlipbookSettings->StartSeconds;
			DurationSeconds = FlipbookSettings->DurationSeconds;
		}

		if ( DurationSeconds > 0.0f )
		{
			if (bIsPlaying)
			{
				PreviewRelativeTime += DeltaTime;
				PreviewRelativeTime = FMath::Fmod(PreviewRelativeTime, DurationSeconds);
			}
			else
			{
				PreviewRelativeTime = FMath::Min(PreviewRelativeTime, DurationSeconds);
			}

			const float PreviewAbsoluteTime = StartSeconds + PreviewRelativeTime;
			ViewportWidget->RefreshView(PreviewAbsoluteTime, DeltaTime);
			TimelineWidget->SetRelativeTime(PreviewRelativeTime);
		}
	}
}

void SNiagaraFlipbookWidget::SetPreviewRelativeTime(float RelativeTime)
{
	bIsPlaying = false;
	PreviewRelativeTime = RelativeTime;
}

FReply SNiagaraFlipbookWidget::OnCapture()
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		ViewModel->RenderFlipbook();
	}
	return FReply::Handled();
}

UNiagaraFlipbookSettings* SNiagaraFlipbookWidget::GetFlipbookSettings() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		return ViewModel->GetFlipbookSettings();
	}
	return nullptr;
}

FReply SNiagaraFlipbookWidget::OnTransportBackwardEnd()
{
	bIsPlaying = false;
	PreviewRelativeTime = 0.0f;

	return FReply::Handled();
}

FReply SNiagaraFlipbookWidget::OnTransportBackwardStep()
{
	bIsPlaying = false;
	if (auto ViewModel = WeakViewModel.Pin())
	{
		const auto DisplayData = ViewModel->GetDisplayDataFromRelativeTime(PreviewRelativeTime);
		const int32 NewFrame = FMath::Clamp(FMath::RoundToInt(DisplayData.FrameTime - 0.6f), 0, DisplayData.NumFrames);
		PreviewRelativeTime = (DisplayData.DurationSeconds / float(DisplayData.NumFrames)) * float(NewFrame);
	}

	return FReply::Handled();
}

FReply SNiagaraFlipbookWidget::OnTransportForwardPlay()
{
	bIsPlaying = !bIsPlaying;
	return FReply::Handled();
}

FReply SNiagaraFlipbookWidget::OnTransportForwardStep()
{
	bIsPlaying = false;
	if (auto ViewModel = WeakViewModel.Pin())
	{
		const auto DisplayData = ViewModel->GetDisplayDataFromRelativeTime(PreviewRelativeTime);
		const int32 NewFrame = FMath::Clamp(FMath::RoundToInt(DisplayData.FrameTime + 0.6f), 0, DisplayData.NumFrames);
		PreviewRelativeTime = (DisplayData.DurationSeconds / float(DisplayData.NumFrames)) * float(NewFrame);
	}

	return FReply::Handled();
}

FReply SNiagaraFlipbookWidget::OnTransportForwardEnd()
{
	bIsPlaying = false;
	if (UNiagaraFlipbookSettings* FlipbookSettings = GetFlipbookSettings())
	{
		PreviewRelativeTime = FlipbookSettings->DurationSeconds;
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
