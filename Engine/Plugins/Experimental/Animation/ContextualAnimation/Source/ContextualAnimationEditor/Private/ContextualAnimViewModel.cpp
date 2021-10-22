// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimViewModel.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "ISequencerModule.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Sections/MovieSceneEventRepeaterSection.h"
#include "ContextualAnimMovieSceneSequence.h"
#include "ContextualAnimMovieSceneNotifyTrackEditor.h"
#include "ContextualAnimMovieSceneNotifyTrack.h"
#include "ContextualAnimMovieSceneNotifySection.h"
#include "ContextualAnimPreviewScene.h"
#include "ContextualAnimPreviewManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "EngineUtils.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimSceneAsset.h"
#include "AnimNotifyState_IKWindow.h"

FContextualAnimViewModel::FContextualAnimViewModel()
	: SceneAsset(nullptr)
	, MovieSceneSequence(nullptr)
	, MovieScene(nullptr)
{
}

FContextualAnimViewModel::~FContextualAnimViewModel()
{
	if (Sequencer.IsValid())
	{
		Sequencer->OnMovieSceneDataChanged().RemoveAll(this);
		Sequencer->OnGlobalTimeChanged().RemoveAll(this);
		Sequencer.Reset();
	}
}

void FContextualAnimViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SceneAsset);
	Collector.AddReferencedObject(PreviewManager);
	Collector.AddReferencedObject(MovieSceneSequence);
	Collector.AddReferencedObject(MovieScene);
}

TSharedPtr<ISequencer> FContextualAnimViewModel::GetSequencer()
{
	return Sequencer;
}

void FContextualAnimViewModel::Initialize(UContextualAnimSceneAsset* InSceneAsset, const TSharedRef<FContextualAnimPreviewScene>& InPreviewScene)
{
	SceneAsset = InSceneAsset;
	PreviewScenePtr = InPreviewScene;

	PreviewManager = NewObject<UContextualAnimPreviewManager>(GetTransientPackage());
	PreviewManager->Initialize(*PreviewScenePtr.Pin()->GetWorld(), *SceneAsset);

	CreateSequencer();
	
	RefreshSequencerTracks();
}

UAnimMontage* FContextualAnimViewModel::FindAnimationByGuid(const FGuid& Guid) const
{
	return PreviewManager->FindAnimationByGuid(Guid);
}

void FContextualAnimViewModel::CreateSequencer()
{
	MovieSceneSequence = NewObject<UContextualAnimMovieSceneSequence>(GetTransientPackage());
	MovieSceneSequence->Initialize(AsShared());

	MovieScene = NewObject<UMovieScene>(MovieSceneSequence, FName("ContextualAnimMovieScene"), RF_Transactional);
	MovieScene->SetDisplayRate(FFrameRate(30, 1));

	FSequencerViewParams ViewParams(TEXT("ContextualAnimSequenceSettings"));
	{
		ViewParams.UniqueName = "ContextualAnimSequenceEditor";
		//ViewParams.OnGetAddMenuContent = OnGetSequencerAddMenuContent;
		//ViewParams.OnGetPlaybackSpeeds = ISequencer::FOnGetPlaybackSpeeds::CreateRaw(this, &FContextualAnimViewModel::GetPlaybackSpeeds);
	}

	FSequencerInitParams SequencerInitParams;
	{
		SequencerInitParams.ViewParams = ViewParams;
		SequencerInitParams.RootSequence = MovieSceneSequence;
		SequencerInitParams.bEditWithinLevelEditor = false;
		SequencerInitParams.ToolkitHost = nullptr;
		SequencerInitParams.PlaybackContext.Bind(this, &FContextualAnimViewModel::GetPlaybackContext);
	}

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked< ISequencerModule >("Sequencer");
	Sequencer = SequencerModule.CreateSequencer(SequencerInitParams);
	Sequencer->OnMovieSceneDataChanged().AddRaw(this, &FContextualAnimViewModel::SequencerDataChanged);
	Sequencer->OnGlobalTimeChanged().AddRaw(this, &FContextualAnimViewModel::SequencerTimeChanged);
	//Sequencer->GetSelectionChangedTracks().AddRaw(this, &FContextualAnimAssetEditorToolkit::SequencerTrackSelectionChanged);
	//Sequencer->GetSelectionChangedSections().AddRaw(this, &FContextualAnimAssetEditorToolkit::SequencerSectionSelectionChanged);
	Sequencer->SetPlaybackStatus(EMovieScenePlayerStatus::Stopped);
}

void FContextualAnimViewModel::RefreshSequencerTracks()
{
	// Remove movie scene tracks (if any)
	for (const FContextualAnimPreviewActorData& Data : PreviewManager->PreviewActorsData)
	{
		Data.GetAnimation()->UnregisterOnNotifyChanged(this);

		MovieSceneSequence->GetMovieScene()->RemovePossessable(Data.Guid);
	}

	// Destroy preview actors (if any)
	PreviewManager->Reset();

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	for (const auto& Entry : SceneAsset->DataContainer)
	{
		if (Entry.Value.AnimDataContainer.Num() > 0)
		{
			const FName& Role = Entry.Key;
			const FContextualAnimData& Data = Entry.Value.AnimDataContainer[0]; //@TODO: Use the first entry for now

			UAnimMontage* AnimMontage = Data.Animation;
			if(AnimMontage == nullptr)
			{
				continue;
			}

			// Spawn preview actor
			AActor* PreviewActor = PreviewManager->SpawnPreviewActor(Role, Data);
			check(PreviewActor);

			// Set actor label so the track shows the name of the role it represents
			PreviewActor->SetActorLabel(Role.ToString(), false);

			// Add preview actors to sequencer
			const bool bSelectActors = false;
			TArray<TWeakObjectPtr<AActor>> Actors = { PreviewActor };
			TArray<FGuid> Guids = Sequencer->AddActors(Actors, bSelectActors);
			check(Guids.Num() > 0);

			const FGuid& Guid = Guids[0];

			// Add Animation Track
			{
				// @TODO: Temporally using an EventTrack to represent the animation since this is just a visual representation of the data. This assumes there is a single section in the montage
				UMovieSceneEventTrack* AnimTrack = MovieSceneSequence->GetMovieScene()->AddTrack<UMovieSceneEventTrack>(Guid);
				check(AnimTrack);

				AnimTrack->SetDisplayName(FText::FromString(GetNameSafe(AnimMontage)));

				UMovieSceneSection* NewSection = NewObject<UMovieSceneSection>(AnimTrack, UMovieSceneEventRepeaterSection::StaticClass(), NAME_None, RF_Transactional);
				check(NewSection);

				FFrameRate TickResolution = MovieSceneSequence->GetMovieScene()->GetTickResolution();
				FFrameNumber StartFrame(0);
				FFrameNumber EndFrame = (AnimMontage->GetPlayLength() * TickResolution).RoundToFrame();
				NewSection->SetRange(TRange<FFrameNumber>::Exclusive(StartFrame, EndFrame));

				AnimTrack->AddSection(*NewSection);
			}

			// Add Notify Tracks
			{
				for (const FAnimNotifyTrack& NotifyTrack : AnimMontage->AnimNotifyTracks)
				{
					UContextualAnimMovieSceneNotifyTrack* Track = MovieSceneSequence->GetMovieScene()->AddTrack<UContextualAnimMovieSceneNotifyTrack>(Guid);
					check(Track);

					Track->Initialize(*AnimMontage, NotifyTrack);
				}

				// Listen for when the notifies in the animation changes, so we can refresh the notify sections here
				AnimMontage->RegisterOnNotifyChanged(UAnimSequenceBase::FOnNotifyChanged::CreateSP(this, &FContextualAnimViewModel::OnAnimNotifyChanged, AnimMontage));
			}

			// Save preview actor data
			PreviewManager->AddPreviewActor(*PreviewActor, Role, Guid, *AnimMontage);
		}
	}

	//@TODO: DisableCollisionBetweenActors should also be called if bDisableCollisionBetweenActors changes while the editor is open
	if(SceneAsset->bDisableCollisionBetweenActors)
	{
		PreviewManager->DisableCollisionBetweenActors();
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void FContextualAnimViewModel::AddActorTrack(const FNewRoleWidgetParams& Params)
{	
	UE_LOG(LogContextualAnim, Log, TEXT("FContextualAnimViewModel::AddActorTrack Role: %s PreviewClass: %s Animation: %s"), 
		*Params.RoleName.ToString(), *GetNameSafe(Params.PreviewClass), *GetNameSafe(Params.Animation));

	FContextualAnimCompositeTrack CompositeTrack;
	CompositeTrack.Settings.PreviewActorClass = Params.PreviewClass;
	CompositeTrack.Settings.MeshToComponent = Params.MeshToComponent;

	FContextualAnimData AnimData;
	AnimData.Animation = Params.Animation;
	AnimData.bRequireFlyingMode = Params.bRequiresFlyingMode;
	CompositeTrack.AnimDataContainer.Add(AnimData);

	SceneAsset->DataContainer.Add(Params.RoleName, CompositeTrack);

	SceneAsset->PrecomputeData();

	RefreshSequencerTracks();
}

UObject* FContextualAnimViewModel::GetPlaybackContext() const
{
	check(PreviewScenePtr.IsValid());
	return PreviewScenePtr.Pin()->GetWorld();
}

void FContextualAnimViewModel::SequencerTimeChanged()
{
	EMovieScenePlayerStatus::Type CurrentStatus = Sequencer->GetPlaybackStatus();
	const float CurrentSequencerTime = Sequencer->GetGlobalTime().AsSeconds();
	const float PlaybackSpeed = Sequencer->GetPlaybackSpeed();

	PreviewManager->PreviewTimeChanged(PreviousSequencerStatus, PreviousSequencerTime, CurrentStatus, CurrentSequencerTime, PlaybackSpeed);

	PreviousSequencerStatus = CurrentStatus;
	PreviousSequencerTime = CurrentSequencerTime;
}

void FContextualAnimViewModel::SequencerDataChanged(EMovieSceneDataChangeType DataChangeType)
{
	UE_LOG(LogContextualAnim, Log, TEXT("FContextualAnimViewModel::OnMovieSceneDataChanged DataChangeType: %d"), (int32)DataChangeType);

	if(DataChangeType == EMovieSceneDataChangeType::TrackValueChanged)
	{
		// Update IK AnimNotify's bEnable flag based on the Active state of the section
		// @TODO: Temp brute-force approach until having a way to override FMovieSceneSection::SetIsActive or something similar

		for (const FContextualAnimPreviewActorData& Data : PreviewManager->PreviewActorsData)
		{
			TArray<UMovieSceneTrack*> Tracks = MovieSceneSequence->GetMovieScene()->FindTracks(UContextualAnimMovieSceneNotifyTrack::StaticClass(), Data.Guid);
			for(UMovieSceneTrack* Track : Tracks)
			{
				const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
				for(UMovieSceneSection* Section : Sections)
				{
					if(UContextualAnimMovieSceneNotifySection* NotifySection = Cast<UContextualAnimMovieSceneNotifySection>(Section))
					{
						if(UAnimNotifyState_IKWindow* IKNotify = Cast<UAnimNotifyState_IKWindow>(NotifySection->GetAnimNotifyState()))
						{
							if(IKNotify->bEnable != NotifySection->IsActive())
							{
								IKNotify->bEnable = NotifySection->IsActive();
								IKNotify->MarkPackageDirty();
							}
						}
					}
				}
			}
		}
	}
	else if(DataChangeType == EMovieSceneDataChangeType::MovieSceneStructureItemRemoved)
	{
		// Remove preview actor and role from the scene asset if an actor track was removed
		int32 TotalRemoved = 0;
		for (int32 Idx = PreviewManager->PreviewActorsData.Num() - 1; Idx >= 0; Idx--)
		{
			const FContextualAnimPreviewActorData& Data = PreviewManager->PreviewActorsData[Idx];

			// Missing binding means the actor track was removed...
			FMovieSceneBinding* Binding = MovieSceneSequence->GetMovieScene()->FindBinding(Data.Guid);
			if (Binding == nullptr)
			{
				// Remove role from the scene asset
				SceneAsset->DataContainer.Remove(Data.Role);

				// Remove preview actor from the scene
				Data.Actor->Destroy();

				// Remove cached data
				PreviewManager->PreviewActorsData.RemoveAt(Idx, 1, false);

				TotalRemoved++;
			}
		}

		PreviewManager->PreviewActorsData.Shrink();

		if(TotalRemoved > 0)
		{
			SceneAsset->PrecomputeData();
			SceneAsset->MarkPackageDirty();
		}
	}
}

void FContextualAnimViewModel::OnAnimNotifyChanged(UAnimMontage* Animation)
{
	// Do not refresh sequencer tracks if the change to the notifies came from us
	if (bUpdatingAnimationFromSequencer)
	{
		return;
	}

	UE_LOG(LogContextualAnim, Log, TEXT("FContextualAnimViewModel::OnAnimNotifyChanged Anim: %s. Refreshing Sequencer Tracks"), *GetNameSafe(Animation));

	RefreshSequencerTracks();
}

void FContextualAnimViewModel::AnimationModified(UAnimMontage& Animation)
{
	TGuardValue<bool> UpdateGuard(bUpdatingAnimationFromSequencer, true);

	Animation.RefreshCacheData();
	Animation.PostEditChange();
	Animation.MarkPackageDirty();
}