// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneNiagaraTrackRecorder.h"

#include "ISequencer.h"
#include "LevelSequence.h"
#include "NiagaraComponent.h"
#include "TakeRecorderSettings.h"
#include "Misc/App.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/CoreMiscDefines.h"
#include "MovieScene/MovieSceneNiagaraSystemTrack.h"
#include "Niagara/NiagaraSimCachingEditorPlugin.h"
#include "Niagara/Sequencer/MovieSceneNiagaraCacheSection.h"
#include "Niagara/Sequencer/MovieSceneNiagaraCacheTrack.h"
#include "Recorder/TakeRecorderParameters.h"

#define LOCTEXT_NAMESPACE "MovieSceneNiagaraTrackRecorder"

bool FMovieSceneNiagaraTrackRecorderFactory::CanRecordObject(UObject* InObjectToRecord) const
{
	return InObjectToRecord->IsA<UNiagaraComponent>();
}

UMovieSceneTrackRecorder* FMovieSceneNiagaraTrackRecorderFactory::CreateTrackRecorderForObject() const
{
	return NewObject<UMovieSceneNiagaraTrackRecorder>();
}

UMovieSceneTrackRecorder* FMovieSceneNiagaraTrackRecorderFactory::CreateTrackRecorderForCacheTrack(IMovieSceneCachedTrack* CachedTrack, TObjectPtr<ULevelSequence> Sequence, TSharedPtr<ISequencer> Sequencer) const
{
	if (UMovieSceneNiagaraCacheTrack* NiagaraCacheTrack = Cast<UMovieSceneNiagaraCacheTrack>(CachedTrack))
	{
		UMovieSceneNiagaraTrackRecorder* TrackRecorder = NewObject<UMovieSceneNiagaraTrackRecorder>();
		
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		const TArray<FMovieSceneBinding>& SceneBindings = MovieScene->GetBindings();

		for (const FMovieSceneBinding& Binding : SceneBindings)
		{
			TArray<UMovieSceneTrack*> ComponentTracks = Binding.GetTracks();
			// find the Niagara component the track is bound to
			if (ComponentTracks.Contains(NiagaraCacheTrack))
			{
				FGuid ObjectGuid = Binding.GetObjectGuid();
				TArrayView<TWeakObjectPtr<>> BoundObjects = Sequencer->FindBoundObjects(ObjectGuid, Sequencer->GetFocusedTemplateID());
				for (auto& Bound : BoundObjects)
				{
					if (UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(Bound))
					{
						TrackRecorder->SystemToRecord = NiagaraComponent;
						break;
					}
				}
				TrackRecorder->NiagaraCacheTrack = NiagaraCacheTrack;
				TrackRecorder->ObjectGuid = ObjectGuid;
				TrackRecorder->OwningTakeRecorderSource = nullptr;
				TrackRecorder->ObjectToRecord = TrackRecorder->SystemToRecord;
				TrackRecorder->MovieScene = MovieScene;
				TrackRecorder->Settings = nullptr;

				for (UMovieSceneTrack* Track : ComponentTracks)
				{
					if (UMovieSceneNiagaraSystemTrack* SystemTrack = Cast<UMovieSceneNiagaraSystemTrack>(Track))
					{
						if (SystemTrack->IsEvalDisabled() == false)
						{
							for (UMovieSceneSection* Section : SystemTrack->GetAllSections())
							{
								if (!TrackRecorder->RecordRange.IsSet())
								{
									TrackRecorder->RecordRange = Section->GetRange();
								}
								TrackRecorder->RecordRange = FFrameNumberRange::Hull(*TrackRecorder->RecordRange, Section->GetRange());
							}
						}
					}
				}

				TArray<UMovieSceneSection*> SceneSections = NiagaraCacheTrack->GetAllSections();
				if (SceneSections.Num() > 0)
				{
					TrackRecorder->NiagaraCacheSection = Cast<UMovieSceneNiagaraCacheSection>(SceneSections[0]);
				}
				else
				{
					TrackRecorder->NiagaraCacheSection = CastChecked<UMovieSceneNiagaraCacheSection>(NiagaraCacheTrack->CreateNewSection());
					TrackRecorder->NiagaraCacheSection->SetIsActive(false);
					NiagaraCacheTrack->AddSection(*TrackRecorder->NiagaraCacheSection);
				}
				NiagaraCacheTrack->bIsRecording = true;

				return TrackRecorder;
			}
		}
	}
	return nullptr;
}

UMovieSceneTrackRecorder* FMovieSceneNiagaraTrackRecorderFactory::CreateTrackRecorderForProperty(UObject* InObjectToRecord, const FName& InPropertyToRecord) const
{
	return nullptr;
}

void UMovieSceneNiagaraTrackRecorder::CreateTrackImpl()
{
	SystemToRecord = CastChecked<UNiagaraComponent>(ObjectToRecord.Get());

	NiagaraCacheTrack = MovieScene->FindTrack<UMovieSceneNiagaraCacheTrack>(ObjectGuid);
	if (!NiagaraCacheTrack.IsValid())
	{
		NiagaraCacheTrack = MovieScene->AddTrack<UMovieSceneNiagaraCacheTrack>(ObjectGuid);
	}
	else
	{
		NiagaraCacheTrack->RemoveAllAnimationData();
	}

	if (NiagaraCacheTrack.IsValid())
	{
		NiagaraCacheTrack->bIsRecording = true;
		NiagaraCacheSection = CastChecked<UMovieSceneNiagaraCacheSection>(NiagaraCacheTrack->CreateNewSection());
		NiagaraCacheSection->SetIsActive(false);
		NiagaraCacheTrack->AddSection(*NiagaraCacheSection);

		// Resize the section to either it's remaining	keyframes range or 0
		NiagaraCacheSection->SetRange(NiagaraCacheSection->GetAutoSizeRange().Get(TRange<FFrameNumber>(0, 0)));

		// Make sure it starts at frame 0, in case Auto Size removed a piece of the start
		NiagaraCacheSection->ExpandToFrame(0);
	}
}

bool UMovieSceneNiagaraTrackRecorder::ShouldContinueRecording(const FQualifiedFrameTime& FrameTime) const
{
	if (RecordRange.IsSet())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameNumber CurrentFrame = FrameTime.ConvertTo(TickResolution).FloorToFrame();
		return CurrentFrame <= RecordRange.GetValue().GetUpperBoundValue();
	}
	return true;
}

void UMovieSceneNiagaraTrackRecorder::SetSectionStartTimecodeImpl(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame)
{
	if (NiagaraCacheSection.IsValid() && NiagaraCacheTrack.IsValid())
	{
		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		FTakeRecorderParameters Parameters;
		Parameters.User = GetDefault<UTakeRecorderUserSettings>()->Settings;
		Parameters.Project = GetDefault<UTakeRecorderProjectSettings>()->Settings;
		RecordStartTime = FApp::GetCurrentTime();
		RecordStartFrame = Parameters.Project.bStartAtCurrentTimecode ?
			FFrameRate::TransformTime(FFrameTime(InSectionStartTimecode.ToFrameNumber(DisplayRate)),
				DisplayRate, TickResolution).FloorToFrame() : MovieScene->GetPlaybackRange().GetLowerBoundValue();

		if (RecordRange.IsSet())
		{
			NiagaraCacheSection->TimecodeSource = FMovieSceneTimecodeSource(FTimecode::FromFrameNumber(RecordRange->GetLowerBoundValue(), TickResolution));
			NiagaraCacheSection->SetRange(RecordRange.GetValue());
			NiagaraCacheSection->SetStartFrame(TRangeBound<FFrameNumber>::Inclusive(RecordRange->GetLowerBoundValue()));
		} 
		else
		{
			NiagaraCacheSection->TimecodeSource = FMovieSceneTimecodeSource(InSectionStartTimecode);
		}

		if (SystemToRecord.IsValid())
		{
			// start simulation and writing to the sim cache 
			SystemToRecord->SetSimCache(nullptr);
			//SystemToRecord->SetAgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime);
			//SystemToRecord->ResetSystem();
			if (NiagaraCacheSection->Params.SimCache == nullptr)
			{
				NiagaraCacheSection->Params.SimCache = NewObject<UNiagaraSimCache>(NiagaraCacheSection.Get(), NAME_None, RF_Transactional);
			}
			NiagaraCacheSection->Params.SimCache->BeginWrite(NiagaraCacheSection->Params.CacheParameters, SystemToRecord.Get());
			NiagaraCacheSection->bCacheOutOfDate = false;
		}
	}
}

UMovieSceneSection* UMovieSceneNiagaraTrackRecorder::GetMovieSceneSection() const
{
	return Cast<UMovieSceneSection>(NiagaraCacheSection.Get());
}

void UMovieSceneNiagaraTrackRecorder::FinalizeTrackImpl()
{
	if (NiagaraCacheTrack.IsValid())
	{
		NiagaraCacheTrack->bIsRecording = false;
	}
	if (NiagaraCacheSection.IsValid())
	{
		// finalize the sim cache
		NiagaraCacheSection->Params.SimCache->EndWrite();
		
		// Set the final range 
		TOptional<TRange<FFrameNumber>> DefaultSectionLength = NiagaraCacheSection->GetAutoSizeRange();
		if (DefaultSectionLength.IsSet())
		{
			if (RecordRange.IsSet())
			{
				NiagaraCacheSection->SetRange(TRange<FFrameNumber>(RecordRange->GetLowerBoundValue(), (DefaultSectionLength->GetUpperBoundValue() - DefaultSectionLength->GetLowerBoundValue()) + RecordRange->GetLowerBoundValue()));
			}
			else
			{
				NiagaraCacheSection->SetRange(DefaultSectionLength.GetValue());
			}
		}

		// Activate the section
		NiagaraCacheSection->SetIsActive(true);
	}
}

void UMovieSceneNiagaraTrackRecorder::RecordSampleImpl(const FQualifiedFrameTime& CurrentFrameTime)
{
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameNumber CurrentFrame = CurrentFrameTime.ConvertTo(TickResolution).FloorToFrame();

	if (RecordRange.IsSet() && (RecordRange->GetLowerBoundValue() > CurrentFrame || RecordRange->GetUpperBoundValue() < CurrentFrame))
	{
		return;
	}

	if (NiagaraCacheSection.IsValid() && SystemToRecord.IsValid())
	{
		FNiagaraSimCacheWriteResult Result = NiagaraCacheSection->Params.SimCache->WriteFrame(SystemToRecord.Get());
		if (!Result.bSuccess)
		{
			UE_LOG(LogNiagaraSimCachingEditor, Warning, TEXT("Error trying to write sim cache: %s"), *Result.ErrorMsg);
		}
		else
		{
			// Expand the section to the new length
			NiagaraCacheSection->SetEndFrame(CurrentFrame);
		}
	}
}

#undef LOCTEXT_NAMESPACE
