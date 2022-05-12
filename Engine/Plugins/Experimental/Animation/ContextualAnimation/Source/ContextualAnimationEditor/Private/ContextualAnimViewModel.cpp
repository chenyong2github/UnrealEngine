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
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "EngineUtils.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimEditorTypes.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimManager.h"
#include "ContextualAnimSceneInstance.h"
#include "AnimNotifyState_IKWindow.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "MotionWarpingComponent.h"

FContextualAnimViewModel::FContextualAnimViewModel()
	: SceneAsset(nullptr)
	, MovieSceneSequence(nullptr)
	, MovieScene(nullptr)
	, ContextualAnimManager(nullptr)
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
	Collector.AddReferencedObject(MovieSceneSequence);
	Collector.AddReferencedObject(MovieScene);
	Collector.AddReferencedObject(ContextualAnimManager);
}

TSharedPtr<ISequencer> FContextualAnimViewModel::GetSequencer()
{
	return Sequencer;
}

void FContextualAnimViewModel::Initialize(UContextualAnimSceneAsset* InSceneAsset, const TSharedRef<FContextualAnimPreviewScene>& InPreviewScene)
{
	SceneAsset = InSceneAsset;
	PreviewScenePtr = InPreviewScene;

	ContextualAnimManager = NewObject<UContextualAnimManager>(GetWorld());

	CreateSequencer();

	RefreshSequencerTracks();
}

UAnimSequenceBase* FContextualAnimViewModel::FindAnimationByGuid(const FGuid& Guid) const
{
	return SceneInstance.IsValid() ? SceneInstance->FindBindingByGuid(Guid)->GetAnimTrack().Animation : nullptr;
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
	Sequencer->SetPlaybackStatus(EMovieScenePlayerStatus::Stopped);
}

void FContextualAnimViewModel::SetActiveSceneVariantIdx(int32 Index)
{
	check(FMath::IsWithinInclusive(Index, 0, (SceneAsset->GetTotalVariants() - 1)));

	ActiveSceneVariantIdx = Index;

	RefreshSequencerTracks();
}

AActor* FContextualAnimViewModel::SpawnPreviewActor(const FContextualAnimTrack& AnimTrack)
{
	const FContextualAnimRoleDefinition* RoleDef = GetSceneAsset()->RolesAsset ? GetSceneAsset()->RolesAsset->FindRoleDefinitionByName(AnimTrack.Role) : nullptr;
	UClass* PreviewClass = RoleDef ? RoleDef->PreviewActorClass : nullptr;
	const FTransform SpawnTransform = (AnimTrack.AlignmentData.ExtractTransformAtTime(0, 0.f));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AActor* PreviewActor = GetWorld()->SpawnActor<AActor>(PreviewClass, SpawnTransform, Params);

	if (ACharacter* PreviewCharacter = Cast<ACharacter>(PreviewActor))
	{
		PreviewCharacter->bUseControllerRotationYaw = false;

		if (UCharacterMovementComponent* CharacterMovementComp = PreviewCharacter->GetCharacterMovement())
		{
			CharacterMovementComp->bOrientRotationToMovement = true;
			CharacterMovementComp->bUseControllerDesiredRotation = false;
			CharacterMovementComp->RotationRate = FRotator(0.f, 540.0, 0.f);
			CharacterMovementComp->bRunPhysicsWithNoController = true;

			CharacterMovementComp->SetMovementMode(AnimTrack.bRequireFlyingMode ? EMovementMode::MOVE_Flying : EMovementMode::MOVE_Walking);
		}

		if (UCameraComponent* CameraComp = PreviewCharacter->FindComponentByClass<UCameraComponent>())
		{
			CameraComp->DestroyComponent();
		}
	}

	UE_LOG(LogContextualAnim, Log, TEXT("Spawned preview Actor: %s at Loc: %s Rot: %s Role: %s"),
		*GetNameSafe(PreviewActor), *SpawnTransform.GetLocation().ToString(), *SpawnTransform.Rotator().ToString(), *AnimTrack.Role.ToString());

	return PreviewActor;
}

void FContextualAnimViewModel::RefreshSequencerTracks()
{
	// Remove movie scene tracks and destroy existing actors (if any)

	for(UAnimSequenceBase* Anim : AnimsBeingEdited)
	{
		Anim->UnregisterOnNotifyChanged(this);
	}
	AnimsBeingEdited.Reset();

	if (SceneInstance.IsValid())
	{
		SceneInstance->Stop();
	}

	const int32 Num = MovieSceneSequence->GetMovieScene()->GetPossessableCount();
	if(Num > 0)
	{
		for (int32 Idx = (Num - 1); Idx >= 0; Idx--)
		{
			FMovieScenePossessable& Possessable = MovieSceneSequence->GetMovieScene()->GetPossessable(Idx);
			MovieSceneSequence->GetMovieScene()->RemovePossessable(Possessable.GetGuid());
		}
	}

	if(StartSceneParams.RoleToActorMap.Num() > 0)
	{
		for (auto& MapEntry : StartSceneParams.RoleToActorMap)
		{
			
			if (AActor* Actor = MapEntry.Value.GetActor())
			{
				Actor->Destroy();
			}
		}
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	StartSceneParams.Reset();
	StartSceneParams.VariantIdx = ActiveSceneVariantIdx;

	SceneAsset->ForEachAnimTrack(ActiveSceneVariantIdx, [this](const FContextualAnimTrack& AnimTrack)
		{
			const FName& Role = AnimTrack.Role;

			// Spawn preview actor
			AActor* PreviewActor = SpawnPreviewActor(AnimTrack);
			
			if(PreviewActor == nullptr)
			{
				return UE::ContextualAnim::EForEachResult::Continue;
			}

			// Set actor label so the track shows the name of the role it represents
			//PreviewActor->SetActorLabel(Role.ToString(), false);

			// Add preview actors to sequencer
			const bool bSelectActors = false;
			TArray<TWeakObjectPtr<AActor>> Actors = { PreviewActor };
			TArray<FGuid> Guids = Sequencer->AddActors(Actors, bSelectActors);
			check(Guids.Num() > 0);

			const FGuid& Guid = Guids[0];

			UAnimSequenceBase* Animation = AnimTrack.Animation;
			if (Animation)
			{
				// Add Animation Track
				{
					// @TODO: Temporally using an EventTrack to represent the animation since this is just a visual representation of the data. This assumes there is a single section in the montage
					UMovieSceneEventTrack* MovieSceneAnimTrack = MovieSceneSequence->GetMovieScene()->AddTrack<UMovieSceneEventTrack>(Guid);
					check(MovieSceneAnimTrack);

					MovieSceneAnimTrack->SetDisplayName(FText::FromString(GetNameSafe(Animation)));

					UMovieSceneSection* NewSection = NewObject<UMovieSceneSection>(MovieSceneAnimTrack, UMovieSceneEventRepeaterSection::StaticClass(), NAME_None, RF_Transactional);
					check(NewSection);

					FFrameRate TickResolution = MovieSceneSequence->GetMovieScene()->GetTickResolution();
					FFrameNumber StartFrame(0);
					FFrameNumber EndFrame = (Animation->GetPlayLength() * TickResolution).RoundToFrame();
					NewSection->SetRange(TRange<FFrameNumber>::Exclusive(StartFrame, EndFrame));

					MovieSceneAnimTrack->AddSection(*NewSection);
				}

				// Add Notify Tracks
				{
					for (const FAnimNotifyTrack& NotifyTrack : Animation->AnimNotifyTracks)
					{
						UContextualAnimMovieSceneNotifyTrack* Track = MovieSceneSequence->GetMovieScene()->AddTrack<UContextualAnimMovieSceneNotifyTrack>(Guid);
						check(Track);

						Track->Initialize(*Animation, NotifyTrack);
					}

					// Listen for when the notifies in the animation changes, so we can refresh the notify sections here
					Animation->RegisterOnNotifyChanged(UAnimSequenceBase::FOnNotifyChanged::CreateSP(this, &FContextualAnimViewModel::OnAnimNotifyChanged, Animation));
				}

				AnimsBeingEdited.Add(Animation);
			}

			StartSceneParams.RoleToActorMap.Add(Role, PreviewActor);

			return UE::ContextualAnim::EForEachResult::Continue;
		});

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	SceneInstance = ContextualAnimManager->ForceStartScene(*GetSceneAsset(), StartSceneParams);

	// Disable auto blend out
	if(ensureAlways(SceneInstance.IsValid()))
	{
		for (auto& Binding : SceneInstance->GetBindings())
		{
			Binding.Guid = Sequencer->FindObjectId(*Binding.GetActor(), MovieSceneSequenceID::Root);
			check(Binding.Guid.IsValid());

			if (FAnimMontageInstance* MontageInstance = Binding.GetAnimMontageInstance())
			{
				MontageInstance->bEnableAutoBlendOut = false;
			}
		}
	}
}

void FContextualAnimViewModel::AddNewVariant(const FContextualAnimNewVariantParams& Params)
{
	FContextualAnimTracksContainer Container;
	for (const FContextualAnimNewVariantData& Data : Params.Data)
	{
		FContextualAnimTrack AnimTrack;
		AnimTrack.Role = Data.RoleName;
		AnimTrack.Animation = Data.Animation;
		AnimTrack.bRequireFlyingMode = Data.bRequiresFlyingMode;
		Container.Tracks.Add(AnimTrack);
	}

	SceneAsset->Variants.Add(Container);

	SceneAsset->PrecomputeData();

	SceneAsset->MarkPackageDirty();

	SetActiveSceneVariantIdx(SceneAsset->GetTotalVariants() - 1);
}

void FContextualAnimViewModel::ToggleSimulateMode() 
{ 
	bIsSimulateModeActive = !bIsSimulateModeActive; 

	if(bIsSimulateModeActive)
	{
		if (SceneInstance.IsValid())
		{
			SceneInstance->Stop();
		}
	}
	else
	{
		if(SceneInstance.IsValid())
		{
			for (auto& Binding : SceneInstance->GetBindings())
			{
				if (UMotionWarpingComponent* MotionWarpComp = Binding.GetActor()->FindComponentByClass<UMotionWarpingComponent>())
				{
					for (const FContextualAnimAlignmentSectionData AligmentData : SceneAsset->AlignmentSections)
					{
						MotionWarpComp->RemoveWarpTarget(AligmentData.WarpTargetName);
					}
				}
			}
		}
		

		RefreshSequencerTracks();
	}
};

void FContextualAnimViewModel::StartSimulation()
{
	//SceneInstance = ContextualAnimManager->ForceStartScene(*GetSceneAsset(), StartSceneParams);
	SceneInstance = ContextualAnimManager->TryStartScene(*GetSceneAsset(), StartSceneParams);

	if(SceneInstance == nullptr)
	{
		//@TODO: This should be a message on the screen
		UE_LOG(LogContextualAnim, Warning, TEXT("Can't start scene"));
		return;
	}
}

UWorld* FContextualAnimViewModel::GetWorld() const
{
	check(PreviewScenePtr.IsValid());
	return PreviewScenePtr.Pin()->GetWorld();
}

UObject* FContextualAnimViewModel::GetPlaybackContext() const
{
	return GetWorld();
}

void FContextualAnimViewModel::SequencerTimeChanged()
{
	auto ResetActorTransform = [](FContextualAnimSceneBinding& Binding, float Time)
	{
		const USkeletalMeshComponent* SkelMeshComp = UContextualAnimUtilities::TryGetSkeletalMeshComponent(Binding.GetActor());

		const FTransform RootTransform = UContextualAnimUtilities::ExtractRootTransformFromAnimation(Binding.GetAnimTrack().Animation, Time);
		const FTransform StartTransform = SkelMeshComp->GetRelativeTransform().Inverse() * RootTransform;

		Binding.GetActor()->SetActorLocationAndRotation(StartTransform.GetLocation(), StartTransform.GetRotation());

		if (UCharacterMovementComponent* MovementComp = Binding.GetActor()->FindComponentByClass<UCharacterMovementComponent>())
		{
			MovementComp->StopMovementImmediately();
		}
	};

	EMovieScenePlayerStatus::Type CurrentStatus = Sequencer->GetPlaybackStatus();
	const float CurrentSequencerTime = Sequencer->GetGlobalTime().AsSeconds();
	const float PlaybackSpeed = Sequencer->GetPlaybackSpeed();

	if(SceneInstance.IsValid())
	{
		for (auto& Binding : SceneInstance->GetBindings())
		{
			if (FAnimMontageInstance* MontageInstance = Binding.GetAnimMontageInstance())
			{
				const float AnimPlayLength = MontageInstance->Montage->GetPlayLength();
				float PreviousTime = FMath::Clamp(PreviousSequencerTime, 0.f, AnimPlayLength);
				float CurrentTime = FMath::Clamp(CurrentSequencerTime, 0.f, AnimPlayLength);

				if (CurrentStatus == EMovieScenePlayerStatus::Stopped || CurrentStatus == EMovieScenePlayerStatus::Scrubbing)
				{
					ResetActorTransform(Binding, CurrentTime);

					if (MontageInstance->IsPlaying())
					{
						MontageInstance->Pause();
					}

					MontageInstance->SetPosition(CurrentTime);
				}
				else if (CurrentStatus == EMovieScenePlayerStatus::Playing)
				{
					if (PlaybackSpeed > 0.f && CurrentTime < PreviousTime)
					{
						ResetActorTransform(Binding, CurrentTime);
						MontageInstance->SetPosition(CurrentTime);
					}

					if (!MontageInstance->IsPlaying())
					{
						MontageInstance->SetPlaying(true);
					}
				}
			}
		}
	}

	PreviousSequencerStatus = CurrentStatus;
	PreviousSequencerTime = CurrentSequencerTime;
}

void FContextualAnimViewModel::SequencerDataChanged(EMovieSceneDataChangeType DataChangeType)
{
	UE_LOG(LogContextualAnim, Log, TEXT("FContextualAnimViewModel::OnMovieSceneDataChanged DataChangeType: %d"), (int32)DataChangeType);

	if(!SceneInstance.IsValid())
	{
		return;
	}

	if(DataChangeType == EMovieSceneDataChangeType::TrackValueChanged)
	{
		// Update IK AnimNotify's bEnable flag based on the Active state of the section
		// @TODO: Temp brute-force approach until having a way to override FMovieSceneSection::SetIsActive or something similar

		for (const auto& Binding : SceneInstance->GetBindings())
		{
			TArray<UMovieSceneTrack*> Tracks = MovieSceneSequence->GetMovieScene()->FindTracks(UContextualAnimMovieSceneNotifyTrack::StaticClass(), Binding.Guid);
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
}

void FContextualAnimViewModel::OnAnimNotifyChanged(UAnimSequenceBase* Animation)
{
	// Do not refresh sequencer tracks if the change to the notifies came from us
	if (bUpdatingAnimationFromSequencer)
	{
		return;
	}

	UE_LOG(LogContextualAnim, Log, TEXT("FContextualAnimViewModel::OnAnimNotifyChanged Anim: %s. Refreshing Sequencer Tracks"), *GetNameSafe(Animation));

	RefreshSequencerTracks();
}

void FContextualAnimViewModel::AnimationModified(UAnimSequenceBase& Animation)
{
	TGuardValue<bool> UpdateGuard(bUpdatingAnimationFromSequencer, true);

	Animation.RefreshCacheData();
	Animation.PostEditChange();
	Animation.MarkPackageDirty();
}

void FContextualAnimViewModel::OnPreviewActorClassChanged()
{
	const UContextualAnimRolesAsset* RolesAsset = GetSceneAsset()->RolesAsset;

	if(RolesAsset && SceneInstance.IsValid())
	{
		for (const auto& Binding : SceneInstance->GetBindings())
		{
			if (const FContextualAnimRoleDefinition* RoleDef = RolesAsset->FindRoleDefinitionByName(Binding.GetRoleDef().Name))
			{
				const UClass* DesiredPreviewClass = RoleDef->PreviewActorClass;
				const UClass* CurrentPreviewClass = Binding.GetActor()->GetClass();

				if (DesiredPreviewClass && DesiredPreviewClass != CurrentPreviewClass)
				{
					RefreshSequencerTracks();
					break;
				}
			}
		}
	}
}