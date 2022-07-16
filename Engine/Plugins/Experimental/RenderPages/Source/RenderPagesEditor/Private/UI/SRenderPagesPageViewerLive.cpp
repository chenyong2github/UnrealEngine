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
#include "Utils/RenderPageLevelSequencePlayer.h"
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
	ViewportClient->Invalidate();// causes it to rerender

	if (ULevelSequencePlayer* SequencePlayer = GetSequencePlayer(); IsValid(SequencePlayer))
	{
		SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(LevelSequenceTime, EUpdatePositionMethod::Play));// execute this every tick, in case any sequencer values get overwritten (by remote control props for example)
		if (UCameraComponent* Camera = SequencePlayer->GetActiveCameraComponent(); IsValid(Camera))
		{
			ViewportClient->SetViewLocation(Camera->GetComponentLocation());
			ViewportClient->SetViewRotation(Camera->GetComponentRotation());
			ViewportClient->ViewFOV = Camera->FieldOfView;
			return;
		}
	}
	ViewportClient->ViewFOV = 90;

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
	ViewportClient = MakeShareable(new FRenderPagesEditorViewportClient(nullptr, SharedThis(this)));
	LevelSequencePlayerWorld = nullptr;
	LevelSequencePlayerActor = nullptr;
	LevelSequencePlayer = nullptr;
	LevelSequence = nullptr;
	LevelSequenceTime = 0.0f;

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

UE::RenderPages::Private::SRenderPagesEditorViewport::~SRenderPagesEditorViewport()
{
	DestroySequencePlayer();
	ViewportClient.Reset();
}

bool UE::RenderPages::Private::SRenderPagesEditorViewport::ShowSequenceFrame(ULevelSequence* InSequence, const float InTime)
{
	LevelSequenceTime = InTime;
	if (!IsValid(InSequence))
	{
		LevelSequence = nullptr;
		DestroySequencePlayer();
		return false;
	}
	if (!IsValid(LevelSequence) || (LevelSequence != InSequence))
	{
		LevelSequence = InSequence;
		DestroySequencePlayer();
	}
	if (ULevelSequencePlayer* SequencePlayer = GetSequencePlayer(); IsValid(SequencePlayer))
	{
		SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(LevelSequenceTime, EUpdatePositionMethod::Play));
	}
	return true;
}

ULevelSequencePlayer* UE::RenderPages::Private::SRenderPagesEditorViewport::GetSequencePlayer()
{
	if (!IsValid(LevelSequence))
	{
		return nullptr;
	}
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (IsValid(LevelSequencePlayer) && LevelSequencePlayerWorld.IsValid() && (World == LevelSequencePlayerWorld))
		{
			return LevelSequencePlayer;
		}

		LevelSequencePlayerWorld = nullptr;
		LevelSequencePlayerActor = nullptr;
		LevelSequencePlayer = nullptr;

		FMovieSceneSequencePlaybackSettings PlaybackSettings;
		PlaybackSettings.LoopCount.Value = 0;
		PlaybackSettings.bAutoPlay = false;
		PlaybackSettings.bPauseAtEnd = true;
		PlaybackSettings.bRestoreState = true;
		FLevelSequenceCameraSettings CameraSettings;

		ALevelSequenceActor* PlayerActor = nullptr;
		if (ULevelSequencePlayer* Player = URenderPageLevelSequencePlayer::CreateLevelSequencePlayer(World, LevelSequence, PlaybackSettings, PlayerActor); IsValid(Player))
		{
			if (IsValid(PlayerActor))
			{
				Player->Initialize(LevelSequence, World->PersistentLevel, PlaybackSettings, CameraSettings);
				Player->State.AssignSequence(MovieSceneSequenceID::Root, *LevelSequence, *Player);
				Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(LevelSequence->GetMovieScene()->GetPlaybackRange().GetLowerBoundValue().Value, EUpdatePositionMethod::Play));

				LevelSequencePlayerWorld = World;
				LevelSequencePlayerActor = PlayerActor;
				LevelSequencePlayer = Player;
				return Player;
			}
		}
	}
	return nullptr;
}

void UE::RenderPages::Private::SRenderPagesEditorViewport::DestroySequencePlayer()
{
	TObjectPtr<ALevelSequenceActor> PlayerActor = LevelSequencePlayerActor;
	TObjectPtr<ULevelSequencePlayer> Player = LevelSequencePlayer;

	LevelSequencePlayerWorld = nullptr;
	LevelSequencePlayerActor = nullptr;
	LevelSequencePlayer = nullptr;

	if (IsValid(Player))
	{
		Player->Stop();
	}
	if (IsValid(PlayerActor))
	{
		PlayerActor->Destroy();
	}
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
