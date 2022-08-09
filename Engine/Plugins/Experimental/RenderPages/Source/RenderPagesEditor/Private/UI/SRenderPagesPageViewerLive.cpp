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
	Render();
}

void UE::RenderPages::Private::SRenderPagesEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	ViewportClient = MakeShareable(new FRenderPagesEditorViewportClient(nullptr, SharedThis(this)));
	LevelSequencePlayerWorld = nullptr;
	LevelSequencePlayerActor = nullptr;
	LevelSequencePlayer = nullptr;
	LevelSequence = nullptr;
	Page = nullptr;
	LevelSequenceTime = 0.0f;
	bRenderedLastAttempt = false;

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

UE::RenderPages::Private::SRenderPagesEditorViewport::~SRenderPagesEditorViewport()
{
	DestroySequencePlayer();
	ViewportClient.Reset();
}

void UE::RenderPages::Private::SRenderPagesEditorViewport::Render()
{
	bRenderedLastAttempt = false;

	if (!ViewportClient.IsValid())
	{
		return;
	}

	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (BlueprintEditor->IsCurrentlyRenderingOrPlaying())
		{
			return;
		}

		if (URenderPageCollection* PageCollection = BlueprintEditor->GetInstance(); IsValid(PageCollection))
		{
			if (!PageCollection->HasRenderPage(Page))
			{
				return;
			}
			bRenderedLastAttempt = true;

			ULevelSequencePlayer* SequencePlayer = GetSequencePlayer();
			if (IsValid(SequencePlayer))
			{
				SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(LevelSequenceTime, EUpdatePositionMethod::Play));// execute this every tick, in case any sequencer values get overwritten (by remote control props for example)
			}

			if (IsValid(Page) && IsValid(PageCollection))
			{
				PageCollection->PreRender(Page);
			}

			bool bHasSetCameraData = false;
			if (IsValid(SequencePlayer))
			{
				if (UCameraComponent* Camera = SequencePlayer->GetActiveCameraComponent(); IsValid(Camera))
				{
					ViewportClient->SetViewLocation(Camera->GetComponentLocation());
					ViewportClient->SetViewRotation(Camera->GetComponentRotation());
					ViewportClient->ViewFOV = Camera->FieldOfView;
					bHasSetCameraData = true;
				}
			}
			if (!bHasSetCameraData)
			{
				if (UWorld* World = GetWorld(); IsValid(World))
				{
					if (APlayerController* LocalPlayerController = World->GetFirstPlayerController(); IsValid(LocalPlayerController))
					{
						FVector ViewLocation;
						FRotator ViewRotation;
						LocalPlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
						ViewportClient->SetViewLocation(ViewLocation);
						ViewportClient->SetViewRotation(ViewRotation);
						ViewportClient->ViewFOV = 90;
					}
					else
					{
						for (TActorIterator<APlayerStart> It(World); It; ++It)
						{
							if (APlayerStart* PlayerStart = *It; IsValid(PlayerStart))
							{
								ViewportClient->SetViewLocation(PlayerStart->GetActorLocation());
								ViewportClient->SetViewRotation(PlayerStart->GetActorRotation());
								ViewportClient->ViewFOV = 90;
								break;
							}
						}
					}
				}
			}

			ViewportClient->Viewport->Draw();

			if (IsValid(Page) && IsValid(PageCollection))
			{
				PageCollection->PostRender(Page);
			}
		}
	}
}

bool UE::RenderPages::Private::SRenderPagesEditorViewport::ShowSequenceFrame(URenderPage* InPage, ULevelSequence* InSequence, const float InTime)
{
	Page = InPage;
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
		if (ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, LevelSequence, PlaybackSettings, PlayerActor); IsValid(Player))
		{
			if (IsValid(PlayerActor))
			{
				Player->Initialize(LevelSequence, World->PersistentLevel, PlaybackSettings, CameraSettings);
				Player->State.AssignSequence(MovieSceneSequenceID::Root, *LevelSequence, *Player);

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
	bViewportWidgetVisible = false;

	SAssignNew(ViewportWidget, SRenderPagesEditorViewport, InBlueprintEditor);

	SAssignNew(FrameSlider, SRenderPagesPageViewerFrameSlider)
		.Visibility(EVisibility::Hidden)
		.OnValueChanged(this, &SRenderPagesPageViewerLive::FrameSliderValueChanged);

	SelectedPageChanged();
	ViewportWidget->Render();// prevents the waiting text from showing up for 1 frame when switching from any other viewer mode to the live viewer mode

	InBlueprintEditor->OnRenderPagesChanged().AddSP(this, &SRenderPagesPageViewerLive::PagesDataChanged);
	InBlueprintEditor->OnRenderPagesSelectionChanged().AddSP(this, &SRenderPagesPageViewerLive::SelectedPageChanged);
	FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SRenderPagesPageViewerLive::OnObjectModified);

	ChildSlot
	[
		SNew(SVerticalBox)

		// viewport & waiting text
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f)
		[
			SNew(SOverlay)
			.Visibility_Lambda([this]() -> EVisibility { return (bViewportWidgetVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden); })

			// viewport
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
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

			// waiting text
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SOverlay)
				.Visibility_Lambda([this]() -> EVisibility { return (ViewportWidget->HasRenderedLastAttempt() ? EVisibility::Hidden : EVisibility::HitTestInvisible); })

				// image to hide the viewport
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Brushes.White"))
					.ColorAndOpacity(FLinearColor(0.0185, 0.0185, 0.0185, 1))
				]

				// text
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Waiting for renderer...")))
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
	bViewportWidgetVisible = false;

	if (!ViewportWidget.IsValid() || !FrameSlider.IsValid())
	{
		return;
	}

	if (URenderPage* SelectedPage = SelectedPageWeakPtr.Get(); IsValid(SelectedPage))
	{
		if (ULevelSequence* Sequence = SelectedPage->GetSequence(); IsValid(Sequence))
		{
			if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
			{
				TOptional<double> StartTime = SelectedPage->GetStartTime();
				TOptional<double> EndTime = SelectedPage->GetEndTime();
				if (!StartTime.IsSet() || !EndTime.IsSet() || (StartTime.Get(0) > EndTime.Get(0)))
				{
					return;
				}

				if (!ViewportWidget->ShowSequenceFrame(SelectedPage, Sequence, FMath::Lerp(StartTime.Get(0), EndTime.Get(0), FrameSlider->GetValue())))
				{
					return;
				}

				bViewportWidgetVisible = true;
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
