// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraBakerWidget.h"
#include "SNiagaraBakerTimelineWidget.h"
#include "SNiagaraBakerViewport.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ViewModels/NiagaraBakerViewModel.h"

#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "ITransportControl.h"
#include "EditorWidgetsModule.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraBakerWidget"

//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////

void SNiagaraBakerWidget::Construct(const FArguments& InArgs)
{
	WeakViewModel = InArgs._WeakViewModel;

	//-TODO: Add tool bar to set options for the pane

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bHideSelectionTip = true;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::Get().LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");

	BakerSettingsDetails = PropertyModule.CreateDetailView(DetailsArgs);

	// Transport control args
	{
		FTransportControlArgs TransportControlArgs;
		TransportControlArgs.OnGetPlaybackMode.BindLambda([&]() -> EPlaybackMode::Type { return bIsPlaying ? EPlaybackMode::PlayingForward : EPlaybackMode::Stopped; } );
		TransportControlArgs.OnBackwardEnd.BindSP(this, &SNiagaraBakerWidget::OnTransportBackwardEnd);
		TransportControlArgs.OnBackwardStep.BindSP(this, &SNiagaraBakerWidget::OnTransportBackwardStep);
		TransportControlArgs.OnForwardPlay.BindSP(this, &SNiagaraBakerWidget::OnTransportForwardPlay);
		TransportControlArgs.OnForwardStep.BindSP(this, &SNiagaraBakerWidget::OnTransportForwardStep);
		TransportControlArgs.OnForwardEnd.BindSP(this, &SNiagaraBakerWidget::OnTransportForwardEnd);

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
				SAssignNew(ViewportWidget, SNiagaraBakerViewport)
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
				SAssignNew(TimelineWidget, SNiagaraBakerTimelineWidget)
				.WeakViewModel(WeakViewModel)
			]
		]
		// Settings Panel
		+SVerticalBox::Slot()
		.FillHeight(0.40f)
		[
			BakerSettingsDetails.ToSharedRef()
		]
	];

	if ( auto ViewModel = WeakViewModel.Pin() )
	{
		if ( UNiagaraComponent* PreviewComponent = ViewModel->GetPreviewComponent() )
		{
			if ( UNiagaraSystem* NiagaraSystem = PreviewComponent->GetAsset() )
			{
				BakerSettingsDetails->SetObject(NiagaraSystem->GetBakerSettings());
			}
		}
	}
}

FText SNiagaraBakerWidget::GetSelectedTextureAsText() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		if ( const UNiagaraBakerSettings* BakerSettings = ViewModel->GetBakerSettings() )
		{
			const int32 PrevewTextureIndex = ViewModel->GetPreviewTextureIndex();
			if ( BakerSettings->OutputTextures.IsValidIndex(PrevewTextureIndex) )
			{
				FString TextureName;
				if (BakerSettings->OutputTextures[PrevewTextureIndex].OutputName.IsNone())
				{
					TextureName = FString::Printf(TEXT("Output Texture %d"), PrevewTextureIndex);
				}
				else
				{
					TextureName = BakerSettings->OutputTextures[PrevewTextureIndex].OutputName.ToString();
				}
				return FText::FromString(TextureName);
			}
		}
	}
	return FText::FromString(FString(TEXT("No Texture")));
}

void SNiagaraBakerWidget::Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime)
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		float StartSeconds = 0.0f;
		float DurationSeconds = 0.0f;
		if (const UNiagaraBakerSettings* GeneratedSettings = GetBakerGeneratedSettings())
		{
			StartSeconds = GeneratedSettings->StartSeconds;
			DurationSeconds = GeneratedSettings->DurationSeconds;
		}
		else if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
		{
			StartSeconds = BakerSettings->StartSeconds;
			DurationSeconds = BakerSettings->DurationSeconds;
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

void SNiagaraBakerWidget::SetPreviewRelativeTime(float RelativeTime)
{
	bIsPlaying = false;
	PreviewRelativeTime = RelativeTime;
}

FReply SNiagaraBakerWidget::OnCapture()
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		ViewModel->RenderBaker();
	}
	return FReply::Handled();
}

UNiagaraBakerSettings* SNiagaraBakerWidget::GetBakerSettings() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		return ViewModel->GetBakerSettings();
	}
	return nullptr;
}

const UNiagaraBakerSettings* SNiagaraBakerWidget::GetBakerGeneratedSettings() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		return ViewModel->GetBakerGeneratedSettings();
	}
	return nullptr;
}

FReply SNiagaraBakerWidget::OnTransportBackwardEnd()
{
	bIsPlaying = false;
	PreviewRelativeTime = 0.0f;

	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportBackwardStep()
{
	bIsPlaying = false;
	if (auto ViewModel = WeakViewModel.Pin())
	{
		if(UNiagaraBakerSettings* Settings = ViewModel->GetBakerSettings())
		{
			const auto DisplayData = Settings->GetDisplayInfo(PreviewRelativeTime, Settings->bPreviewLooping);
			const int NumFrames = Settings->GetNumFrames();
			const int NewFrame = DisplayData.Interp > 0.25f ? DisplayData.FrameIndexA : FMath::Max(DisplayData.FrameIndexA - 1, 0);
			PreviewRelativeTime = (Settings->DurationSeconds / float(NumFrames)) * float(NewFrame);
		}
	}

	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportForwardPlay()
{
	bIsPlaying = !bIsPlaying;
	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportForwardStep()
{
	bIsPlaying = false;
	if (auto ViewModel = WeakViewModel.Pin())
	{
		if (UNiagaraBakerSettings* Settings = ViewModel->GetBakerSettings())
		{
			const auto DisplayData = Settings->GetDisplayInfo(PreviewRelativeTime, Settings->bPreviewLooping);
			const int NumFrames = Settings->GetNumFrames();
			const int NewFrame = DisplayData.Interp < 0.75f ? DisplayData.FrameIndexB : FMath::Min(DisplayData.FrameIndexB + 1, NumFrames - 1);
			PreviewRelativeTime = (Settings->DurationSeconds / float(NumFrames)) * float(NewFrame);
		}
	}

	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportForwardEnd()
{
	bIsPlaying = false;
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		PreviewRelativeTime = BakerSettings->DurationSeconds;
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
