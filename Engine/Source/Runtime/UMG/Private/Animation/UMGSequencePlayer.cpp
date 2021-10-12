// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/UMGSequencePlayer.h"
#include "MovieScene.h"
#include "UMGPrivate.h"
#include "Animation/WidgetAnimation.h"
#include "MovieSceneTimeHelpers.h"
#include "Evaluation/MovieScenePlayback.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "Animation/UMGSequenceTickManager.h"

extern TAutoConsoleVariable<bool> CVarUserWidgetUseParallelAnimation;

UUMGSequencePlayer::UUMGSequencePlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PlayerStatus = EMovieScenePlayerStatus::Stopped;
	TimeCursorPosition = FFrameTime(0);
	PlaybackSpeed = 1;
	bRestoreState = false;
	Animation = nullptr;
	bIsEvaluating = false;
	bCompleteOnPostEvaluation = false;
	UserTag = NAME_None;
}

void UUMGSequencePlayer::InitSequencePlayer(UWidgetAnimation& InAnimation, UUserWidget& InUserWidget)
{
	Animation = &InAnimation;
	UserWidget = &InUserWidget;

	UMovieScene* MovieScene = Animation->GetMovieScene();

	// Cache the time range of the sequence to determine when we stop
	Duration = UE::MovieScene::DiscreteSize(MovieScene->GetPlaybackRange());
	AnimationResolution = MovieScene->GetTickResolution();
	AbsolutePlaybackStart = UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange());
}

UMovieSceneEntitySystemLinker* UUMGSequencePlayer::ConstructEntitySystemLinker()
{
	UUserWidget* Widget = UserWidget.Get();
	if (ensure(Widget) && !EnumHasAnyFlags(Animation->GetFlags(), EMovieSceneSequenceFlags::BlockingEvaluation))
	{
		if (!ensure(Widget->AnimationTickManager))
		{
			// @todo: There should be no possible way that the animation tick manager is null here, but there is a very low-rate
			// crash caused by it being null that is very hard to track down, so patching with a band-aid for now.
			Widget->AnimationTickManager = UUMGSequenceTickManager::Get(Widget);
			Widget->AnimationTickManager->AddWidget(Widget);
		}

		return Widget->AnimationTickManager->GetLinker();
	}

	return UMovieSceneEntitySystemLinker::CreateLinker(Widget ? Widget->GetWorld() : nullptr);
}

void UUMGSequencePlayer::Tick(float DeltaTime)
{
	if ( PlayerStatus == EMovieScenePlayerStatus::Playing )
	{
		FFrameTime DeltaFrameTime = (bIsPlayingForward ? DeltaTime * PlaybackSpeed : -DeltaTime * PlaybackSpeed) * AnimationResolution;

		FFrameTime LastTimePosition = TimeCursorPosition;
		TimeCursorPosition += DeltaFrameTime;

		// Check if we crossed over bounds
		const bool bCrossedLowerBound = TimeCursorPosition < 0;
		const bool bCrossedUpperBound = TimeCursorPosition >= FFrameTime(Duration);
		const bool bCrossedEndTime = bIsPlayingForward
			? LastTimePosition < EndTime && EndTime <= TimeCursorPosition
			: LastTimePosition > EndTime && EndTime >= TimeCursorPosition;

		// Increment the num loops if we crossed any bounds.
		if (bCrossedLowerBound || bCrossedUpperBound || (bCrossedEndTime && NumLoopsCompleted >= NumLoopsToPlay - 1))
		{
			NumLoopsCompleted++;
		}

		// Did the animation complete
		const bool bCompleted = NumLoopsToPlay != 0 && NumLoopsCompleted >= NumLoopsToPlay;

		// Handle Times
		if (bCrossedLowerBound)
		{
			if (bCompleted)
			{
				TimeCursorPosition = FFrameTime(0);
			}
			else
			{
				if (PlayMode == EUMGSequencePlayMode::PingPong)
				{
					bIsPlayingForward = !bIsPlayingForward;
					TimeCursorPosition = FMath::Abs(TimeCursorPosition);
				}
				else
				{
					TimeCursorPosition += FFrameTime(Duration);
					LastTimePosition = TimeCursorPosition;
				}
			}
		}
		else if (bCrossedUpperBound)
		{
			FFrameTime LastValidFrame(Duration-1, 0.99999994f);

			if (bCompleted)
			{
				TimeCursorPosition = LastValidFrame;
			}
			else
			{
				if (PlayMode == EUMGSequencePlayMode::PingPong)
				{
					bIsPlayingForward = !bIsPlayingForward;
					TimeCursorPosition = LastValidFrame - (TimeCursorPosition - FFrameTime(Duration));
				}
				else
				{
					TimeCursorPosition = TimeCursorPosition - FFrameTime(Duration);
					LastTimePosition = TimeCursorPosition;
				}
			}
		}
		else if (bCrossedEndTime)
		{
			if (bCompleted)
			{
				TimeCursorPosition = EndTime;
			}
		}

		bCompleteOnPostEvaluation = bCompleted;

		if (RootTemplateInstance.IsValid())
		{
			UMovieScene* MovieScene = Animation->GetMovieScene();

			FMovieSceneContext Context(
					FMovieSceneEvaluationRange(
						AbsolutePlaybackStart + TimeCursorPosition,
						AbsolutePlaybackStart + LastTimePosition,
						AnimationResolution),
					PlayerStatus);
			Context.SetHasJumped(bCrossedLowerBound || bCrossedUpperBound || bCrossedEndTime);

			UMovieSceneSequence* MovieSceneSequence = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root);
			const bool bIsSequenceBlocking = EnumHasAnyFlags(MovieSceneSequence->GetFlags(), EMovieSceneSequenceFlags::BlockingEvaluation);

			const bool bIsRunningTickManager = CVarUserWidgetUseParallelAnimation.GetValueOnGameThread();

			if (bIsRunningTickManager)
			{
				UUserWidget* Widget = UserWidget.Get();
				UUMGSequenceTickManager* TickManager = Widget ? Widget->AnimationTickManager : nullptr;

				if (!TickManager)
				{
					return;
				}

				if (!bIsSequenceBlocking)
				{
					// Queue an evaluation of this player's widget animation, to be evaluated later by our
					// global tick manager as part of a glorious multi-threaded job fest.
					FMovieSceneEntitySystemRunner& Runner = TickManager->GetRunner();
					Runner.QueueUpdate(Context, RootTemplateInstance.GetRootInstanceHandle());
					// WARNING: the evalution hasn't run yet so don't run any stateful code after this point
					// unless you know it's OK to do so. Most likely, you want to run stateful code in the
					// PostEvaluation method, or queue up a latent action.
				}
				else
				{
					// Synchronous evaluation. Sucks for performance.
					RootTemplateInstance.Evaluate(Context, *this);
					// The latent actions will be run by the tick manager.
				}
			}
			else
			{
				// Synchronous evaluation. Sucks for performance.
				RootTemplateInstance.Evaluate(Context, *this);
				ApplyLatentActions();
			}
		}
	}
}

void UUMGSequencePlayer::PlayInternal(double StartAtTime, double EndAtTime, int32 InNumLoopsToPlay, EUMGSequencePlayMode::Type InPlayMode, float InPlaybackSpeed, bool bInRestoreState)
{
	RootTemplateInstance.Initialize(*Animation, *this, nullptr);

	if (bInRestoreState)
	{
		RootTemplateInstance.EnableGlobalPreAnimatedStateCapture();
	}

	bRestoreState = bInRestoreState;
	PlaybackSpeed = FMath::Abs(InPlaybackSpeed);
	PlayMode = InPlayMode;

	FFrameTime LastValidFrame(Duration-1, 0.99999994f);

	if (PlayMode == EUMGSequencePlayMode::Reverse)
	{
		// When playing in reverse count subtract the start time from the end.
		TimeCursorPosition = LastValidFrame - StartAtTime * AnimationResolution;
	}
	else
	{
		TimeCursorPosition = StartAtTime * AnimationResolution;
	}

	// Clamp the start time and end time to be within the bounds
	TimeCursorPosition = FMath::Clamp(TimeCursorPosition, FFrameTime(0), LastValidFrame);
	EndTime = FMath::Clamp(EndAtTime * AnimationResolution, FFrameTime(0), LastValidFrame);

	if ( PlayMode == EUMGSequencePlayMode::PingPong )
	{
		// When animating in ping-pong mode double the number of loops to play so that a loop is a complete forward/reverse cycle.
		NumLoopsToPlay = 2 * InNumLoopsToPlay;
	}
	else
	{
		NumLoopsToPlay = InNumLoopsToPlay;
	}

	NumLoopsCompleted = 0;
	bIsPlayingForward = InPlayMode != EUMGSequencePlayMode::Reverse;

	PlayerStatus = EMovieScenePlayerStatus::Playing;

	UUserWidget* Widget = UserWidget.Get();
	UUMGSequenceTickManager* TickManager = Widget ? Widget->AnimationTickManager : nullptr;

	// Playback assumes the start frame has already been evaulated, so we also want to evaluate any events on the start frame here.
	if (TickManager && RootTemplateInstance.IsValid())
	{
		const FMovieSceneContext Context(FMovieSceneEvaluationRange(AbsolutePlaybackStart + TimeCursorPosition, AbsolutePlaybackStart + TimeCursorPosition, AnimationResolution), PlayerStatus);

		// We queue an update instead of immediately flushing the entire linker so that we don't incur a cascade of flushes on frames when multiple animations are played
		// In rare cases where the linker must be flushed immediately PreTick, the queue should be manually flushed 

		FMovieSceneEntitySystemRunner& Runner = TickManager->GetRunner();
		Runner.QueueUpdate(Context, RootTemplateInstance.GetRootInstanceHandle());
	}
}

void UUMGSequencePlayer::Play(float StartAtTime, int32 InNumLoopsToPlay, EUMGSequencePlayMode::Type InPlayMode, float InPlaybackSpeed, bool bInRestoreState)
{
	if (NeedsQueueLatentAction())
	{
		QueueLatentAction(FMovieSceneSequenceLatentActionDelegate::CreateUObject(
					this, &UUMGSequencePlayer::Play, StartAtTime, InNumLoopsToPlay, InPlayMode, InPlaybackSpeed, bInRestoreState));
		return;
	}

	PlayInternal(StartAtTime, 0.0, InNumLoopsToPlay, InPlayMode, InPlaybackSpeed, bInRestoreState);
}

void UUMGSequencePlayer::PlayTo(float StartAtTime, float EndAtTime, int32 InNumLoopsToPlay, EUMGSequencePlayMode::Type InPlayMode, float InPlaybackSpeed, bool bInRestoreState)
{
	if (NeedsQueueLatentAction())
	{
		QueueLatentAction(FMovieSceneSequenceLatentActionDelegate::CreateUObject(
					this, &UUMGSequencePlayer::PlayTo, StartAtTime, EndAtTime, InNumLoopsToPlay, InPlayMode, InPlaybackSpeed, bInRestoreState));
		return;
	}

	PlayInternal(StartAtTime, EndAtTime, InNumLoopsToPlay, InPlayMode, InPlaybackSpeed, bInRestoreState);
}

void UUMGSequencePlayer::Pause()
{
	if (NeedsQueueLatentAction())
	{
		QueueLatentAction(FMovieSceneSequenceLatentActionDelegate::CreateUObject(this, &UUMGSequencePlayer::Pause));
		return;
	}

	// Purposely don't trigger any OnFinished events
	PlayerStatus = EMovieScenePlayerStatus::Stopped;

	// Evaluate the sequence at its current time, with a status of 'stopped' to ensure that animated state pauses correctly. (ie. audio sounds should stop/pause)
	const FMovieSceneContext Context(FMovieSceneEvaluationRange(AbsolutePlaybackStart + TimeCursorPosition, AbsolutePlaybackStart + TimeCursorPosition, AnimationResolution), PlayerStatus);
	if (RootTemplateInstance.HasEverUpdated())
	{
		UUserWidget* Widget = UserWidget.Get();
		UUMGSequenceTickManager* TickManager = Widget ? Widget->AnimationTickManager : nullptr;

		if (TickManager)
		{
			FMovieSceneEntitySystemRunner& Runner = TickManager->GetRunner();
			Runner.QueueUpdate(Context, RootTemplateInstance.GetRootInstanceHandle());
		}
	}
}

void UUMGSequencePlayer::Reverse()
{
	if (PlayerStatus == EMovieScenePlayerStatus::Playing)
	{
		bIsPlayingForward = !bIsPlayingForward;
	}
}

void UUMGSequencePlayer::Stop()
{
	if (NeedsQueueLatentAction())
	{
		QueueLatentAction(FMovieSceneSequenceLatentActionDelegate::CreateUObject(this, &UUMGSequencePlayer::Stop));
		return;
	}

	PlayerStatus = EMovieScenePlayerStatus::Stopped;

	UUserWidget* Widget = UserWidget.Get();
	UUMGSequenceTickManager* TickManager = Widget ? Widget->AnimationTickManager : nullptr;

	if (TickManager && RootTemplateInstance.IsValid())
	{
		if (RootTemplateInstance.HasEverUpdated())
		{
			const FMovieSceneContext Context(FMovieSceneEvaluationRange(AbsolutePlaybackStart, AnimationResolution), PlayerStatus);
			RootTemplateInstance.Evaluate(Context, *this);
		}
		else
		{
			TickManager->ClearLatentActions(this);
			LatentActions.Empty();
		}
		RootTemplateInstance.Finish(*this);
	}

	if (bRestoreState)
	{
		RestorePreAnimatedState();
	}

	UserWidget->OnAnimationFinishedPlaying(*this);
	OnSequenceFinishedPlayingEvent.Broadcast(*this);

	TimeCursorPosition = FFrameTime(0);
}

void UUMGSequencePlayer::SetNumLoopsToPlay(int32 InNumLoopsToPlay)
{
	if (PlayMode == EUMGSequencePlayMode::PingPong)
	{
		NumLoopsToPlay = (2 * InNumLoopsToPlay);
	}
	else
	{
		NumLoopsToPlay = InNumLoopsToPlay;
	}
}

void UUMGSequencePlayer::SetPlaybackSpeed(float InPlaybackSpeed)
{
	PlaybackSpeed = InPlaybackSpeed;
}

EMovieScenePlayerStatus::Type UUMGSequencePlayer::GetPlaybackStatus() const
{
	return PlayerStatus;
}

UObject* UUMGSequencePlayer::GetPlaybackContext() const
{
	return UserWidget.Get();
}

TArray<UObject*> UUMGSequencePlayer::GetEventContexts() const
{
	TArray<UObject*> EventContexts;
	if (UserWidget.IsValid())
	{
		EventContexts.Add(UserWidget.Get());
	}
	return EventContexts;
}

void UUMGSequencePlayer::SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus)
{
	PlayerStatus = InPlaybackStatus;
}

void UUMGSequencePlayer::PreEvaluation(const FMovieSceneContext& Context)
{
	bIsEvaluating = true;
}

void UUMGSequencePlayer::PostEvaluation(const FMovieSceneContext& Context)
{
	bIsEvaluating = false;

	if (bCompleteOnPostEvaluation)
	{
		bCompleteOnPostEvaluation = false;

		PlayerStatus = EMovieScenePlayerStatus::Stopped;
		
		if (RootTemplateInstance.IsValid())
		{
			RootTemplateInstance.Finish(*this);
		}

		if (bRestoreState)
		{
			RestorePreAnimatedState();
		}

		UserWidget->OnAnimationFinishedPlaying(*this);
		OnSequenceFinishedPlayingEvent.Broadcast(*this);
	}
}

bool UUMGSequencePlayer::NeedsQueueLatentAction() const
{
	return bIsEvaluating;
}

void UUMGSequencePlayer::QueueLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate)
{
	if (CVarUserWidgetUseParallelAnimation.GetValueOnGameThread())
	{
		UUserWidget* Widget = UserWidget.Get();
		UUMGSequenceTickManager* TickManager = Widget ? Widget->AnimationTickManager : nullptr;

		if (ensure(TickManager))
		{
			TickManager->AddLatentAction(Delegate);
		}
	}
	else
	{
		LatentActions.Add(Delegate);
	}
}

void UUMGSequencePlayer::ApplyLatentActions()
{
	if (CVarUserWidgetUseParallelAnimation.GetValueOnGameThread())
	{
		UUserWidget* Widget = UserWidget.Get();
		UUMGSequenceTickManager* TickManager = Widget ? Widget->AnimationTickManager : nullptr;
		if (TickManager)
		{
			TickManager->RunLatentActions();
		}
	}
	else
	{
		while (LatentActions.Num() > 0)
		{
			const FMovieSceneSequenceLatentActionDelegate& Delegate = LatentActions[0];
			Delegate.ExecuteIfBound();
			LatentActions.RemoveAt(0);
		}
	}
}

void UUMGSequencePlayer::TearDown()
{
	RootTemplateInstance.BeginDestroy();
}

void UUMGSequencePlayer::BeginDestroy()
{
	RootTemplateInstance.BeginDestroy();

	Super::BeginDestroy();
}

