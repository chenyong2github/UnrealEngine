// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimViewModel.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "ISequencerModule.h"
#include "MovieSceneCommonHelpers.h"
#include "ContextualAnimMovieSceneSequence.h"
#include "ContextualAnimMovieSceneTrack.h"
#include "ContextualAnimMovieSceneSection.h"
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

void FContextualAnimViewModel::SetActiveSection(int32 SectionIdx)
{
	check(GetSceneAsset()->Sections.IsValidIndex(SectionIdx));

	ActiveSectionIdx = SectionIdx;

	RefreshSequencerTracks();
}

void FContextualAnimViewModel::SetActiveAnimSetForSection(int32 SectionIdx, int32 AnimSetIdx)
{
	check(GetSceneAsset()->Sections.IsValidIndex(SectionIdx));
	check(GetSceneAsset()->Sections[SectionIdx].AnimSets.IsValidIndex(AnimSetIdx));

	int32& ActiveSetIdx = ActiveAnimSetMap.FindOrAdd(SectionIdx);
	ActiveSetIdx = AnimSetIdx;

	RefreshSequencerTracks();
}

AActor* FContextualAnimViewModel::SpawnPreviewActor(const FContextualAnimTrack& AnimTrack)
{
	const FContextualAnimRoleDefinition* RoleDef = GetSceneAsset()->RolesAsset ? GetSceneAsset()->RolesAsset->FindRoleDefinitionByName(AnimTrack.Role) : nullptr;
	UClass* PreviewClass = RoleDef ? RoleDef->PreviewActorClass : nullptr;
	const FTransform SpawnTransform = AnimTrack.GetRootTransformAtTime(0.f);

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

	for (int32 MasterTrackIdx = MovieScene->GetMasterTracks().Num() - 1; MasterTrackIdx >= 0; MasterTrackIdx--)
	{
		UMovieSceneTrack& MasterTrack = *MovieScene->GetMasterTracks()[MasterTrackIdx];
		for (const UMovieSceneSection* Section : MasterTrack.GetAllSections())
		{
			const UContextualAnimMovieSceneSection* ContextualAnimSection = Cast<const UContextualAnimMovieSceneSection>(Section);
			check(ContextualAnimSection);

			if(ContextualAnimSection->GetAnimTrack().Animation)
			{
				ContextualAnimSection->GetAnimTrack().Animation->UnregisterOnNotifyChanged(this);
			}
		}

		MovieScene->RemoveMasterTrack(MasterTrack);
	}
			
	if (SceneInstance.IsValid())
	{
		SceneInstance->Stop();
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

	if(!SceneAsset->Sections.IsValidIndex(ActiveSectionIdx))
	{
		return;
	}

	StartSceneParams.Reset();
	StartSceneParams.SectionIdx = ActiveSectionIdx;
	StartSceneParams.AnimSetIdx = ActiveAnimSetMap.FindOrAdd(ActiveSectionIdx);

	TArray<FName> Roles = SceneAsset->GetRoles();
	for(FName Role : Roles)
	{
		UContextualAnimMovieSceneTrack* MovieSceneAnimTrack = MovieSceneSequence->GetMovieScene()->AddMasterTrack<UContextualAnimMovieSceneTrack>();
		check(MovieSceneAnimTrack);

		MovieSceneAnimTrack->Initialize(Role);
	}

	FContextualAnimSceneSection& ContextualAnimSection = SceneAsset->Sections[ActiveSectionIdx];
	for (int32 AnimSetIdx = 0; AnimSetIdx < ContextualAnimSection.AnimSets.Num(); AnimSetIdx++)
	{
		FContextualAnimSet& AnimSet = ContextualAnimSection.AnimSets[AnimSetIdx];
		for (int32 AnimTrackIdx = 0; AnimTrackIdx < AnimSet.Tracks.Num(); AnimTrackIdx++)
		{
			const FContextualAnimTrack& AnimTrack = AnimSet.Tracks[AnimTrackIdx];

			UAnimSequenceBase* Animation = AnimTrack.Animation;
			if (Animation)
			{
				UContextualAnimMovieSceneTrack* MovieSceneTrack = FindMasterTrackByRole(AnimTrack.Role);
				if (MovieSceneTrack == nullptr)
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("FContextualAnimViewModel::RefreshSequencerTracks. Can't find MovieSceneTrack for %s. Role: %s SectionIdx: %d AnimIndex: %d"),
						*GetNameSafe(Animation), *AnimTrack.Role.ToString(), ActiveSectionIdx, AnimSetIdx);

					continue;
				}

				UContextualAnimMovieSceneSection* NewSection = NewObject<UContextualAnimMovieSceneSection>(MovieSceneTrack, UContextualAnimMovieSceneSection::StaticClass(), NAME_None, RF_Transactional);
				check(NewSection);

				NewSection->Initialize(ActiveSectionIdx, AnimSetIdx, AnimTrackIdx);

				const float AnimLength = Animation->GetPlayLength();
				const FFrameRate TickResolution = MovieSceneSequence->GetMovieScene()->GetTickResolution();
				NewSection->SetRange(TRange<FFrameNumber>::Inclusive(0, (AnimLength * TickResolution).RoundToFrame()));

				NewSection->SetRowIndex(AnimSetIdx);

				const int32& ActiveSetIdx = ActiveAnimSetMap.FindOrAdd(ActiveSectionIdx);
				NewSection->SetIsActive(AnimSetIdx == ActiveSetIdx);

				MovieSceneTrack->AddSection(*NewSection);
				MovieSceneTrack->SetTrackRowDisplayName(FText::FromString(FString::Printf(TEXT("%d"), AnimSetIdx)), AnimSetIdx);
			}

			if (!StartSceneParams.RoleToActorMap.Contains(AnimTrack.Role))
			{
				if (AActor* PreviewActor = SpawnPreviewActor(AnimTrack))
				{
					StartSceneParams.RoleToActorMap.Add(AnimTrack.Role, PreviewActor);
				}
			}
		}
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	SceneInstance = ContextualAnimManager->ForceStartScene(*GetSceneAsset(), StartSceneParams);

	// Disable auto blend out
	if(SceneInstance.IsValid())
	{
		for (auto& Binding : SceneInstance->GetBindings())
		{
			if (FAnimMontageInstance* MontageInstance = Binding.GetAnimMontageInstance())
			{
				MontageInstance->Pause();
				MontageInstance->bEnableAutoBlendOut = false;
			}
		}
	}
}

void FContextualAnimViewModel::AddNewAnimSet(const FContextualAnimNewAnimSetParams& Params)
{
	FContextualAnimSet AnimSet;
	for (const FContextualAnimNewAnimSetData& Data : Params.Data)
	{
		FContextualAnimTrack AnimTrack;
		AnimTrack.Role = Data.RoleName;
		AnimTrack.Animation = Data.Animation;
		AnimTrack.bRequireFlyingMode = Data.bRequiresFlyingMode;
		AnimSet.Tracks.Add(AnimTrack);
	}

	int32 SectionIdx = INDEX_NONE;
	int32 AnimSetIdx = INDEX_NONE;
	for(int32 Idx = 0; Idx < SceneAsset->Sections.Num(); Idx++)
	{
		FContextualAnimSceneSection& Section = SceneAsset->Sections[Idx];
		if(Section.Name == Params.SectionName)
		{
			AnimSetIdx = Section.AnimSets.Add(AnimSet);
			SectionIdx = Idx;
			break;
		}
	}
	
	if(SectionIdx == INDEX_NONE)
	{
		FContextualAnimSceneSection NewSection;
		NewSection.Name = Params.SectionName;
		AnimSetIdx = NewSection.AnimSets.Add(AnimSet);
		SectionIdx = SceneAsset->Sections.Add(NewSection);
	}

	check(SectionIdx != INDEX_NONE);
	check(AnimSetIdx != INDEX_NONE);

	SceneAsset->PrecomputeData();

	SceneAsset->MarkPackageDirty();

	// Set active AnimSet and refresh sequencer panel
	SetActiveAnimSetForSection(SectionIdx, AnimSetIdx);
}

void FContextualAnimViewModel::AddNewIKTarget(const UContextualAnimNewIKTargetParams& Params)
{
	check(SceneAsset->Sections.IsValidIndex(Params.SectionIdx));

	// Add IK Target definition to the scene asset
	FContextualAnimIKTargetDefinition IKTargetDef;
	IKTargetDef.GoalName = Params.GoalName;
	IKTargetDef.BoneName = Params.SourceBone.BoneName;
	IKTargetDef.Provider = Params.Provider;
	IKTargetDef.TargetRoleName = Params.TargetRole;
	IKTargetDef.TargetBoneName = Params.TargetBone.BoneName;

	if (FContextualAnimIKTargetDefContainer* ContainerPtr = SceneAsset->Sections[Params.SectionIdx].RoleToIKTargetDefsMap.Find(Params.SourceRole))
	{
		ContainerPtr->IKTargetDefs.AddUnique(IKTargetDef);
	}
	else
	{
		FContextualAnimIKTargetDefContainer Container;
		Container.IKTargetDefs.AddUnique(IKTargetDef);
		SceneAsset->Sections[Params.SectionIdx].RoleToIKTargetDefsMap.Add(Params.SourceRole, Container);
	}

	SceneAsset->PrecomputeData();

	SceneAsset->MarkPackageDirty();
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
					for (const FContextualAnimSetPivotDefinition& Def : SceneAsset->GetAnimSetPivotDefinitionsInSection(Binding.GetAnimTrack().SectionIdx))
					{
						MotionWarpComp->RemoveWarpTarget(Def.Name);
					}
				}
			}
		}
		

		RefreshSequencerTracks();
	}
};

void FContextualAnimViewModel::StartSimulation()
{
	FContextualAnimStartSceneParams Params;
	Params.RoleToActorMap = StartSceneParams.RoleToActorMap;
	Params.SectionIdx = 0;
	SceneInstance = ContextualAnimManager->TryStartScene(*GetSceneAsset(), Params);

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

void FContextualAnimViewModel::UpdatePreviewActorTransform(const FContextualAnimSceneBinding& Binding, float Time)
{
	if (AActor* PreviewActor = Binding.GetActor())
	{
		FTransform Transform = Binding.GetAnimTrack().GetRootTransformAtTime(Time);

		// Special case for Character
		if (ACharacter* PreviewCharacter = Cast<ACharacter>(PreviewActor))
		{
			if (UCharacterMovementComponent* MovementComp = Binding.GetActor()->FindComponentByClass<UCharacterMovementComponent>())
			{
				MovementComp->StopMovementImmediately();
			}

			const float MIN_FLOOR_DIST = 1.9f; //from CharacterMovementComp, including in this offset to avoid jittering in walking mode
			const float CapsuleHalfHeight = PreviewCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Transform.SetLocation(Transform.GetLocation() + (PreviewCharacter->GetActorQuat().GetUpVector() * CapsuleHalfHeight + MIN_FLOOR_DIST));

			Transform.SetRotation(PreviewCharacter->GetBaseRotationOffset().Inverse() * Transform.GetRotation());
		}

		PreviewActor->SetActorLocationAndRotation(Transform.GetLocation(), Transform.GetRotation());
	}
}

UContextualAnimMovieSceneTrack* FContextualAnimViewModel::FindMasterTrackByRole(const FName& Role) const
{
	const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetMasterTracks();
	for (UMovieSceneTrack* MasterTrack : MasterTracks)
	{
		UContextualAnimMovieSceneTrack* ContextualAnimTrack = Cast<UContextualAnimMovieSceneTrack>(MasterTrack);
		check(ContextualAnimTrack);

		if (ContextualAnimTrack->GetRole() == Role)
		{
			return ContextualAnimTrack;
		}
	}

	return nullptr;
}

void FContextualAnimViewModel::SequencerTimeChanged()
{
	EMovieScenePlayerStatus::Type CurrentStatus = Sequencer->GetPlaybackStatus();
	const float CurrentSequencerTime = Sequencer->GetGlobalTime().AsSeconds();
	const float PlaybackSpeed = Sequencer->GetPlaybackSpeed();

	if (SceneInstance.IsValid())
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
					UpdatePreviewActorTransform(Binding, CurrentTime);

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
						UpdatePreviewActorTransform(Binding, CurrentTime);
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

		// @TODO: Commented out for now until we add the new behavior where the user needs to double-click on the animation to edit the notifies
		/*for (const auto& Binding : SceneInstance->GetBindings())
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
		}*/
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