// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPageViewerLive.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "IRenderPageCollectionEditor.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "RenderPage/RenderPageCollection.h"
#include "SlateOptMacros.h"
#include "UI/Components/SRenderPagesPageViewerFrameSlider.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPageViewerLive"


UE::RenderPages::Private::FRenderPagesEditorViewportClient::FRenderPagesEditorViewportClient(FPreviewScene* PreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(nullptr, PreviewScene, InEditorViewportWidget)
{
	FOVAngle = 90;
	ViewFOV = 90;
	Invalidate();

	bDisableInput = true;
	SetGameView(true);
	SetRealtime(false);// we manually render every frame, because automatic rendering stops temporarily when you're dragging another widget (with the mouse)
}


void UE::RenderPages::Private::SRenderPagesEditorViewport::Tick(const FGeometry&, const double, const float)
{
	if (!ViewportClient.IsValid())
	{
		return;
	}
	ViewportClient->Invalidate();// render

	if (LevelSequenceActor.IsValid())
	{
		if (ULevelSequencePlayer* SequencePlayer = LevelSequenceActor->GetSequencePlayer(); IsValid(SequencePlayer))
		{
			SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(LevelSequenceTime, EUpdatePositionMethod::Play));// execute every tick, in case any sequencer values get overwritten (by remote control props for example)
			if (UCameraComponent* Camera = SequencePlayer->GetActiveCameraComponent(); IsValid(Camera))
			{
				ViewportClient->SetViewLocation(Camera->GetComponentLocation());
				ViewportClient->SetViewRotation(Camera->GetComponentRotation());
				return;
			}
		}
	}

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (APlayerController* LocalPlayerController = World->GetFirstPlayerController(); IsValid(LocalPlayerController))
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			LocalPlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
			ViewportClient->SetViewLocation(ViewLocation);
			ViewportClient->SetViewRotation(ViewRotation);
			return;
		}

		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			if (APlayerStart* PlayerStart = *It; IsValid(PlayerStart))
			{
				ViewportClient->SetViewLocation(PlayerStart->GetActorLocation());
				ViewportClient->SetViewRotation(PlayerStart->GetActorRotation());
				return;
			}
		}
	}
}

void UE::RenderPages::Private::SRenderPagesEditorViewport::Construct(const FArguments& InArgs)
{
	LevelSequenceTime = 0.0f;
	ViewportClient = MakeShareable(new FRenderPagesEditorViewportClient(nullptr, SharedThis(this)));

	SEditorViewport::Construct(SEditorViewport::FArguments());

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		FActorSpawnParameters LevelSequenceSpawnParams;
		LevelSequenceSpawnParams.ObjectFlags = RF_Transient | RF_DuplicateTransient;
		LevelSequenceActor = TStrongObjectPtr(World->SpawnActor<ALevelSequenceActor>(LevelSequenceSpawnParams));
		LevelSequenceActor->PlaybackSettings.LoopCount.Value = 0;
		LevelSequenceActor->PlaybackSettings.bAutoPlay = false;
		LevelSequenceActor->PlaybackSettings.bPauseAtEnd = true;
		LevelSequenceActor->PlaybackSettings.bRestoreState = true;
	}
}

UE::RenderPages::Private::SRenderPagesEditorViewport::~SRenderPagesEditorViewport()
{
	if (LevelSequenceActor.IsValid())
	{
		if (ULevelSequencePlayer* SequencePlayer = LevelSequenceActor->GetSequencePlayer(); IsValid(SequencePlayer))
		{
			SequencePlayer->Stop();
		}
		LevelSequenceActor->Destroy();
	}
	LevelSequenceActor.Reset();
	ViewportClient.Reset();
}

bool UE::RenderPages::Private::SRenderPagesEditorViewport::ShowSequenceFrame(ULevelSequence* InSequence, const float InTime)
{
	LevelSequenceTime = InTime;
	if (!LevelSequenceActor.IsValid() || !IsValid(InSequence))
	{
		return false;
	}
	if (LevelSequenceActor->GetSequence() != InSequence)
	{
		if (ULevelSequencePlayer* SequencePlayer = LevelSequenceActor->GetSequencePlayer(); IsValid(SequencePlayer))
		{
			SequencePlayer->Stop();
		}
		LevelSequenceActor->SetSequence(InSequence);
	}
	if (ULevelSequencePlayer* SequencePlayer = LevelSequenceActor->GetSequencePlayer(); IsValid(SequencePlayer))
	{
		SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(LevelSequenceTime, EUpdatePositionMethod::Play));
	}
	return true;
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageViewerLive::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	SelectedPageWeakPtr = nullptr;

	SAssignNew(ViewportWidget, SRenderPagesEditorViewport)
		.Visibility(EVisibility::Hidden);

	SAssignNew(FrameSlider, SRenderPagesPageViewerFrameSlider)
		.Visibility(EVisibility::Hidden)
		.OnValueChanged(this, &SRenderPagesPageViewerLive::FrameSliderValueChanged);

	SelectedPageChanged();

	InBlueprintEditor->OnRenderPagesChanged().AddSP(this, &SRenderPagesPageViewerLive::PagesDataChanged);
	InBlueprintEditor->OnRenderPagesSelectionChanged().AddSP(this, &SRenderPagesPageViewerLive::SelectedPageChanged);
	FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SRenderPagesPageViewerLive::OnObjectModified);

	ChildSlot
	[
		SNew(SVerticalBox)

		// viewport
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f)
		[
			SNew(SScaleBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::Both)
			[
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.WidthOverride(1.0f)
				.HeightOverride_Lambda([this]() -> float { return 1.0 / (SelectedPageWeakPtr.IsValid() ? SelectedPageWeakPtr->GetOutputAspectRatio() : 1.0); })
				[
					ViewportWidget.ToSharedRef()
				]
			]
		]

		// slider
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f)
		[
			FrameSlider.ToSharedRef()
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageViewerLive::OnObjectModified(UObject* Object)
{
	if (SelectedPageWeakPtr.IsValid() && (Object == SelectedPageWeakPtr))
	{
		// page changed
		SelectedPageChanged();
	}
	else if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (Object == BlueprintEditor->GetInstance())
		{
			// page collection changed
			PagesDataChanged();
		}
	}
}

void UE::RenderPages::Private::SRenderPagesPageViewerLive::PagesDataChanged()
{
	UpdateViewport();
	UpdateFrameSlider();
}

void UE::RenderPages::Private::SRenderPagesPageViewerLive::SelectedPageChanged()
{
	SelectedPageWeakPtr = nullptr;
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (const TArray<URenderPage*> SelectedPages = BlueprintEditor->GetSelectedRenderPages(); (SelectedPages.Num() == 1))
		{
			SelectedPageWeakPtr = SelectedPages[0];
		}
	}

	UpdateViewport();
	UpdateFrameSlider();
}

void UE::RenderPages::Private::SRenderPagesPageViewerLive::FrameSliderValueChanged(const float NewValue)
{
	UpdateViewport();
	UpdateFrameSlider();
}


void UE::RenderPages::Private::SRenderPagesPageViewerLive::UpdateViewport()
{
	if (!ViewportWidget.IsValid())
	{
		return;
	}
	ViewportWidget->SetVisibility(EVisibility::Hidden);

	if (FrameSlider.IsValid())
	{
		if (URenderPage* SelectedPage = SelectedPageWeakPtr.Get(); IsValid(SelectedPage))
		{
			if (ULevelSequence* Sequence = SelectedPage->GetSequence(); IsValid(Sequence))
			{
				TOptional<double> StartTime = SelectedPage->GetStartTime();
				TOptional<double> EndTime = SelectedPage->GetEndTime();
				if (!StartTime.IsSet() || !EndTime.IsSet() || (StartTime.Get(0) > EndTime.Get(0)))
				{
					return;
				}

				if (!ViewportWidget->ShowSequenceFrame(Sequence, FMath::Lerp(StartTime.Get(0), EndTime.Get(0), FrameSlider->GetValue())))
				{
					return;
				}

				ViewportWidget->SetVisibility(EVisibility::Visible);
			}
		}
	}
}

void UE::RenderPages::Private::SRenderPagesPageViewerLive::UpdateFrameSlider()
{
	if (!FrameSlider.IsValid())
	{
		return;
	}
	FrameSlider->SetVisibility(EVisibility::Hidden);

	if (URenderPage* SelectedPage = SelectedPageWeakPtr.Get(); IsValid(SelectedPage))
	{
		if (!FrameSlider->SetFramesText(SelectedPage))
		{
			return;
		}

		FrameSlider->SetVisibility(EVisibility::Visible);
	}
}


#undef LOCTEXT_NAMESPACE
