// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneAudioTemplate.h"


#include "Engine/EngineTypes.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundCue.h"
#include "GameFramework/Actor.h"
#include "AudioThread.h"
#include "AudioDevice.h"
#include "ActiveSound.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "MovieScene.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "IMovieScenePlayer.h"


DECLARE_CYCLE_STAT(TEXT("Audio Track Evaluate"), MovieSceneEval_AudioTrack_Evaluate, STATGROUP_MovieSceneEval);
DECLARE_CYCLE_STAT(TEXT("Audio Track Tear Down"), MovieSceneEval_AudioTrack_TearDown, STATGROUP_MovieSceneEval);
DECLARE_CYCLE_STAT(TEXT("Audio Track Token Execute"), MovieSceneEval_AudioTrack_TokenExecute, STATGROUP_MovieSceneEval);


/** Stop audio from playing */
struct FStopAudioPreAnimatedToken : IMovieScenePreAnimatedToken
{
	static FMovieSceneAnimTypeID GetAnimTypeID()
	{
		return TMovieSceneAnimTypeID<FStopAudioPreAnimatedToken>();
	}

	virtual void RestoreState(UObject& InObject, IMovieScenePlayer& Player) override
	{
		UAudioComponent* AudioComponent = CastChecked<UAudioComponent>(&InObject);
		if (AudioComponent)
		{
			AudioComponent->Stop();
			AudioComponent->DestroyComponent();
		}
	}

	struct FProducer : IMovieScenePreAnimatedTokenProducer
	{
		virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
		{
			return FStopAudioPreAnimatedToken();
		}
	};
};

/** Destroy a transient audio component */
struct FDestroyAudioPreAnimatedToken : IMovieScenePreAnimatedToken
{
	static FMovieSceneAnimTypeID GetAnimTypeID()
	{
		return TMovieSceneAnimTypeID<FDestroyAudioPreAnimatedToken>();
	}

	virtual void RestoreState(UObject& InObject, IMovieScenePlayer& Player) override
	{
		UAudioComponent* AudioComponent = CastChecked<UAudioComponent>(&InObject);
		if (AudioComponent)
		{
			AudioComponent->DestroyComponent();
		}
	}

	struct FProducer : IMovieScenePreAnimatedTokenProducer
	{
		virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
		{
			return FDestroyAudioPreAnimatedToken();
		}
	};
};

struct FCachedAudioTrackData : IPersistentEvaluationData
{
	TMap<FObjectKey, TMap<FObjectKey, TWeakObjectPtr<UAudioComponent>>> AudioComponentsByActorKey;
	
	FCachedAudioTrackData()
	{
		// Create the container for master tracks, which do not have an actor to attach to
		AudioComponentsByActorKey.Add(FObjectKey(), TMap<FObjectKey, TWeakObjectPtr<UAudioComponent>>());
	}

	UAudioComponent* GetAudioComponent(FObjectKey ActorKey, FObjectKey SectionKey)
	{
		if (TMap<FObjectKey, TWeakObjectPtr<UAudioComponent>>* Map = AudioComponentsByActorKey.Find(ActorKey))
		{
			// First, check for an exact match for this section
			TWeakObjectPtr<UAudioComponent> ExistingComponentPtr = Map->FindRef(SectionKey);
			if (ExistingComponentPtr.IsValid())
			{
				return ExistingComponentPtr.Get();
			}

			// If no exact match, check for any AudioComponent that isn't busy
			for (TPair<FObjectKey, TWeakObjectPtr<UAudioComponent >> Pair : *Map)
			{
				UAudioComponent* ExistingComponent = Map->FindRef(Pair.Key).Get();
				if (ExistingComponent && !ExistingComponent->IsPlaying())
				{
					// Replace this entry with the new SectionKey to claim it
					Map->Remove(Pair.Key);
					Map->Add(SectionKey, ExistingComponent);
					return ExistingComponent;
				}
			}
		}

		return nullptr;
	}

	/** Only to be called on the game thread */
	UAudioComponent* AddAudioComponentForRow(int32 RowIndex, FObjectKey SectionKey, UObject& PrincipalObject, IMovieScenePlayer& Player)
	{
		FObjectKey ObjectKey(&PrincipalObject);
		
		if (!AudioComponentsByActorKey.Contains(ObjectKey))
		{
			AudioComponentsByActorKey.Add(ObjectKey, TMap<FObjectKey, TWeakObjectPtr<UAudioComponent>>());
		}

		UAudioComponent* ExistingComponent = GetAudioComponent(ObjectKey, SectionKey);
		if (!ExistingComponent)
		{
			USoundCue* TempPlaybackAudioCue = NewObject<USoundCue>();

			AActor* Actor = nullptr;
			USceneComponent* SceneComponent = nullptr;
			FString ObjectName;

			if (PrincipalObject.IsA<AActor>())
			{
				Actor = Cast<AActor>(&PrincipalObject);
				SceneComponent = Actor->GetRootComponent();
				ObjectName =
#if WITH_EDITOR
					Actor->GetActorLabel();
#else
					Actor->GetName();
#endif
			}
			else if (PrincipalObject.IsA<UActorComponent>())
			{
				UActorComponent* ActorComponent = Cast<UActorComponent>(&PrincipalObject);
				Actor = ActorComponent->GetOwner();
				SceneComponent = Cast<USceneComponent>(ActorComponent);
				ObjectName = ActorComponent->GetName();
			}

			if (!Actor || !SceneComponent)
			{
				UE_LOG(LogMovieScene, Warning, TEXT("Failed to find scene component for spatialized audio track (row %d)."), RowIndex);
				return nullptr;
			}

			FAudioDevice::FCreateComponentParams Params(Actor->GetWorld(), Actor);
			ExistingComponent = FAudioDevice::CreateComponent(TempPlaybackAudioCue, Params);

			if (!ExistingComponent)
			{
				UE_LOG(LogMovieScene, Warning, TEXT("Failed to create audio component for spatialized audio track (row %d on %s)."), RowIndex, *ObjectName);
				return nullptr;
			}

			Player.SavePreAnimatedState(*ExistingComponent, FMovieSceneAnimTypeID::Unique(), FDestroyAudioPreAnimatedToken::FProducer());

			AudioComponentsByActorKey[ObjectKey].Add(SectionKey, ExistingComponent);

			ExistingComponent->SetFlags(RF_Transient);
			ExistingComponent->AttachToComponent(SceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
		}

		return ExistingComponent;
	}

	/** Only to be called on the game thread */
	UAudioComponent* AddMasterAudioComponentForRow(int32 RowIndex, FObjectKey SectionKey, UWorld* World, IMovieScenePlayer& Player)
	{
		UAudioComponent* ExistingComponent = GetAudioComponent(FObjectKey(), SectionKey);
		if (!ExistingComponent)
		{
			USoundCue* TempPlaybackAudioCue = NewObject<USoundCue>();
			ExistingComponent = FAudioDevice::CreateComponent(TempPlaybackAudioCue, FAudioDevice::FCreateComponentParams(World));

			if (!ExistingComponent)
			{
				UE_LOG(LogMovieScene, Warning, TEXT("Failed to create audio component for master audio track (row %d)."), RowIndex);
				return nullptr;
			}

			Player.SavePreAnimatedState(*ExistingComponent, FMovieSceneAnimTypeID::Unique(), FDestroyAudioPreAnimatedToken::FProducer());
			
			ExistingComponent->SetFlags(RF_Transient);
			AudioComponentsByActorKey[FObjectKey()].Add(SectionKey, ExistingComponent);
		}

		return ExistingComponent;
	}

	void StopAllSounds()
	{
		for (TPair<FObjectKey, TMap<FObjectKey, TWeakObjectPtr<UAudioComponent>>>& Map : AudioComponentsByActorKey)
		{
			for (TPair<FObjectKey, TWeakObjectPtr<UAudioComponent>>& Pair : Map.Value)
			{
				if (UAudioComponent* AudioComponent = Pair.Value.Get())
				{
					AudioComponent->Stop();
				}
			}
		}
	}

	void StopSoundsOnSection(FObjectKey ObjectKey)
	{
		for (TPair<FObjectKey, TMap<FObjectKey, TWeakObjectPtr<UAudioComponent>>>& Pair : AudioComponentsByActorKey)
		{
			if (UAudioComponent* AudioComponent = Pair.Value.FindRef(ObjectKey).Get())
			{
				AudioComponent->Stop();
			}
		}
	}
};


struct FAudioSectionExecutionToken : IMovieSceneExecutionToken
{
	FAudioSectionExecutionToken(const UMovieSceneAudioSection* InAudioSection)
		: AudioSection(InAudioSection), SectionKey(InAudioSection)
	{}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		FCachedAudioTrackData& TrackData = PersistentData.GetOrAddTrackData<FCachedAudioTrackData>();

		if ((Context.GetStatus() != EMovieScenePlayerStatus::Playing && Context.GetStatus() != EMovieScenePlayerStatus::Scrubbing) || Context.HasJumped() || Context.GetDirection() == EPlayDirection::Backwards)
		{
			// stopped, recording, etc
			TrackData.StopAllSounds();
		}

		// Master audio track
		else if (!Operand.ObjectBindingID.IsValid())
		{
			UObject* PlaybackContext = Player.GetPlaybackContext();

			UAudioComponent* AudioComponent = TrackData.GetAudioComponent(FObjectKey(), SectionKey);
			if (!AudioComponent)
			{
				// Initialize the sound
				AudioComponent = TrackData.AddMasterAudioComponentForRow(AudioSection->GetRowIndex(), SectionKey, PlaybackContext ? PlaybackContext->GetWorld() : nullptr, Player);

				if (AudioComponent)
				{
					if (AudioSection->GetOnQueueSubtitles().IsBound())
					{
						AudioComponent->OnQueueSubtitles = AudioSection->GetOnQueueSubtitles();
					}
					if (AudioSection->GetOnAudioFinished().IsBound())
					{
						AudioComponent->OnAudioFinished = AudioSection->GetOnAudioFinished();
					}
					if (AudioSection->GetOnAudioPlaybackPercent().IsBound())
					{
						AudioComponent->OnAudioPlaybackPercent = AudioSection->GetOnAudioPlaybackPercent();
					}
				}
			}

			if (AudioComponent)
			{
				EnsureAudioIsPlaying(*AudioComponent, PersistentData, Context, false, Player);
			}
		}

		// Object binding audio track
		else
		{
			for (TWeakObjectPtr<> Object : Player.FindBoundObjects(Operand))
			{
				UAudioComponent* AudioComponent = TrackData.GetAudioComponent(Object.Get(), SectionKey);
				if (!AudioComponent)
				{
					// Initialize the sound
					AudioComponent = TrackData.AddAudioComponentForRow(AudioSection->GetRowIndex(), SectionKey, *Object.Get(), Player);

					if (AudioComponent)
					{
						if (AudioSection->GetOnQueueSubtitles().IsBound())
						{
							AudioComponent->OnQueueSubtitles = AudioSection->GetOnQueueSubtitles();
						}
						if (AudioSection->GetOnAudioFinished().IsBound())
						{
							AudioComponent->OnAudioFinished = AudioSection->GetOnAudioFinished();
						}
						if (AudioSection->GetOnAudioPlaybackPercent().IsBound())
						{
							AudioComponent->OnAudioPlaybackPercent = AudioSection->GetOnAudioPlaybackPercent();
						}
					}
				}

				if (AudioComponent)
				{
					EnsureAudioIsPlaying(*AudioComponent, PersistentData, Context, true, Player);
				}
			}
		}
	}

	void EnsureAudioIsPlaying(UAudioComponent& AudioComponent, FPersistentEvaluationData& PersistentData, const FMovieSceneContext& Context, bool bAllowSpatialization, IMovieScenePlayer& Player) const
	{
		Player.SavePreAnimatedState(AudioComponent, FStopAudioPreAnimatedToken::GetAnimTypeID(), FStopAudioPreAnimatedToken::FProducer());

		bool bPlaySound = !AudioComponent.IsPlaying() || AudioComponent.Sound != AudioSection->GetSound();

		float AudioVolume = 1.f;
		AudioSection->GetSoundVolumeChannel().Evaluate(Context.GetTime(), AudioVolume);
		AudioVolume = AudioVolume * AudioSection->EvaluateEasing(Context.GetTime());
		if (AudioComponent.VolumeMultiplier != AudioVolume)
		{
			AudioComponent.SetVolumeMultiplier(AudioVolume);
		}

		float PitchMultiplier = 1.f;
		AudioSection->GetPitchMultiplierChannel().Evaluate(Context.GetTime(), PitchMultiplier);
		if (AudioComponent.PitchMultiplier != PitchMultiplier)
		{
			AudioComponent.SetPitchMultiplier(PitchMultiplier);
		}

		if (bPlaySound)
		{
			AudioComponent.bAllowSpatialization = bAllowSpatialization;

			if (AudioSection->GetOverrideAttenuation())
			{
				AudioComponent.AttenuationSettings = AudioSection->GetAttenuationSettings();
			}

			AudioComponent.Stop();
			AudioComponent.SetSound(AudioSection->GetSound());
#if WITH_EDITOR
			UObject* PlaybackContext = Player.GetPlaybackContext();
			UWorld* World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;
			if (GIsEditor && World != nullptr && !World->IsPlayInEditor())
			{
				AudioComponent.bIsUISound = true;
				AudioComponent.bIsPreviewSound = true;
			}
			else
#endif // WITH_EDITOR
			{
				AudioComponent.bIsUISound = false;
			}

			float SectionStartTimeSeconds = (AudioSection->HasStartFrame() ? AudioSection->GetInclusiveStartFrame() : 0) / AudioSection->GetTypedOuter<UMovieScene>()->GetTickResolution();

			const float AudioTime = (Context.GetTime() / Context.GetFrameRate()) - SectionStartTimeSeconds + (float)Context.GetFrameRate().AsSeconds(AudioSection->GetStartOffset());
			if (AudioTime >= 0.f && AudioComponent.Sound && AudioTime < AudioComponent.Sound->GetDuration())
			{
				AudioComponent.Play(AudioTime);
			}

			if (Context.GetStatus() == EMovieScenePlayerStatus::Scrubbing)
			{
				// While scrubbing, play the sound for a short time and then cut it.
				AudioComponent.StopDelayed(AudioTrackConstants::ScrubDuration);
			}
		}

		if (bAllowSpatialization)
		{
			if (FAudioDevice* AudioDevice = AudioComponent.GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.MovieSceneUpdateAudioTransform"), STAT_MovieSceneUpdateAudioTransform, STATGROUP_TaskGraphTasks);

				const FTransform ActorTransform = AudioComponent.GetOwner()->GetTransform();
				const uint64 ActorComponentID = AudioComponent.GetAudioComponentID();
				FAudioThread::RunCommandOnAudioThread([AudioDevice, ActorComponentID, ActorTransform]()
				{
					if (FActiveSound* ActiveSound = AudioDevice->FindActiveSound(ActorComponentID))
					{
						ActiveSound->bLocationDefined = true;
						ActiveSound->Transform = ActorTransform;
					}
				}, GET_STATID(STAT_MovieSceneUpdateAudioTransform));
			}
		}
	}

	const UMovieSceneAudioSection* AudioSection;
	FObjectKey SectionKey;
};

FMovieSceneAudioSectionTemplate::FMovieSceneAudioSectionTemplate()
	: AudioSection()
{
}

FMovieSceneAudioSectionTemplate::FMovieSceneAudioSectionTemplate(const UMovieSceneAudioSection& Section)
	: AudioSection(&Section)
{
}


void FMovieSceneAudioSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_AudioTrack_Evaluate)

	if (GEngine && GEngine->UseSound() && Context.GetStatus() != EMovieScenePlayerStatus::Jumping)
	{
		ExecutionTokens.Add(FAudioSectionExecutionToken(AudioSection));
	}
}

void FMovieSceneAudioSectionTemplate::TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_AudioTrack_TearDown)

	if (GEngine && GEngine->UseSound())
	{
		FCachedAudioTrackData& TrackData = PersistentData.GetOrAddTrackData<FCachedAudioTrackData>();

		TrackData.StopSoundsOnSection(AudioSection);
	}
}
