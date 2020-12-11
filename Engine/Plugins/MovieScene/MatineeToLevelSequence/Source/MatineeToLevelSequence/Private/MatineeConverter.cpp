// Copyright Epic Games, Inc. All Rights Reserved.

#include "MatineeConverter.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/Light.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Character.h"
#include "MovieSceneFolder.h"
#include "MovieSceneTimeHelpers.h"
#include "Particles/ParticleSystemComponent.h"

#include "Matinee/InterpData.h"
#include "Matinee/InterpGroup.h"
#include "Matinee/InterpGroupDirector.h"
#include "Matinee/InterpGroupInst.h"
#include "Matinee/InterpTrackAnimControl.h"
#include "Matinee/InterpTrackAudioMaster.h"
#include "Matinee/InterpTrackBoolProp.h"
#include "Matinee/InterpTrackColorProp.h"
#include "Matinee/InterpTrackColorScale.h"
#include "Matinee/InterpTrackDirector.h"
#include "Matinee/InterpTrackEvent.h"
#include "Matinee/InterpTrackFade.h"
#include "Matinee/InterpTrackFloatMaterialParam.h"
#include "Matinee/InterpTrackFloatProp.h"
#include "Matinee/InterpTrackLinearColorProp.h"
#include "Matinee/InterpTrackMove.h"
#include "Matinee/InterpTrackMoveAxis.h"
#include "Matinee/InterpTrackSlomo.h"
#include "Matinee/InterpTrackSound.h"
#include "Matinee/InterpTrackVectorMaterialParam.h"
#include "Matinee/InterpTrackVectorProp.h"
#include "Matinee/InterpTrackVisibility.h"
#include "Matinee/MatineeActor.h"
#include "Matinee/MatineeActorCameraAnim.h"

#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneParticleTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneSlomoTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"

#define LOCTEXT_NAMESPACE "MatineeConverter"

FDelegateHandle FMatineeConverter::RegisterTrackConverterForMatineeClass(TSubclassOf<UInterpTrack> InterpTrackClass, IMatineeToLevelSequenceModule::FOnConvertMatineeTrack OnConvertMatineeTrack)
{
	if (ExtendedInterpConverters.Contains(InterpTrackClass))
	{
		UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Track converter already registered for: %s"), InterpTrackClass->GetClass());
		return FDelegateHandle();
	}

	return ExtendedInterpConverters.Add(InterpTrackClass, OnConvertMatineeTrack).GetHandle();
}

void FMatineeConverter::UnregisterTrackConverterForMatineeClass(FDelegateHandle RemoveDelegate)
{
	for (auto InterpConverter : ExtendedInterpConverters)
	{
		if (InterpConverter.Value.GetHandle() == RemoveDelegate)
		{
			ExtendedInterpConverters.Remove(*InterpConverter.Key);
			return;
		}
	}

	UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Attempted to remove track convert that could not be found"));
}

void FMatineeConverter::FindOrAddFolder(UMovieScene* MovieScene, TWeakObjectPtr<AActor> Actor, FGuid Guid)
{
	FName FolderName(NAME_None);
	if(Actor.Get()->IsA<ACharacter>() || Actor.Get()->IsA<ASkeletalMeshActor>())
	{
		FolderName = TEXT("Characters");
	}
	else if(Actor.Get()->GetClass()->IsChildOf(ACameraActor::StaticClass()))
	{
		FolderName = TEXT("Cameras");
	}
	else if(Actor.Get()->GetClass()->IsChildOf(ALight::StaticClass()))
	{
		FolderName = TEXT("Lights");
	}
	else if (Actor.Get()->FindComponentByClass<UParticleSystemComponent>())
	{
		FolderName = TEXT("Particles");
	}
	else
	{
		FolderName = TEXT("Misc");
	}

	UMovieSceneFolder* FolderToUse = FindOrAddFolder(MovieScene, FolderName);
	FolderToUse->AddChildObjectBinding(Guid);
}

UMovieSceneFolder* FMatineeConverter::FindOrAddFolder(UMovieScene* MovieScene, FName FolderName)
{
	// look for a folder to put us in
	UMovieSceneFolder* FolderToUse = nullptr;
	for (UMovieSceneFolder* Folder : MovieScene->GetRootFolders())
	{
		if (Folder->GetFolderName() == FolderName)
		{
			FolderToUse = Folder;
			break;
		}
	}

	if (FolderToUse == nullptr)
	{
		FolderToUse = NewObject<UMovieSceneFolder>(MovieScene, NAME_None, RF_Transactional);
		FolderToUse->SetFolderName(FolderName);
		MovieScene->GetRootFolders().Add(FolderToUse);
	}

	return FolderToUse;
}

void FMatineeConverter::AddMasterTrackToFolder(UMovieScene* MovieScene, UMovieSceneTrack* MovieSceneTrack, FName FolderName)
{
	UMovieSceneFolder* FolderToUse = FindOrAddFolder(MovieScene, FolderName);
	FolderToUse->AddChildMasterTrack(MovieSceneTrack);
}

FGuid FMatineeConverter::FindComponentGuid(AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, FGuid PossessableGuid) const
{
	FGuid ComponentGUID;
	// Skeletal and static mesh actors can both have material component tracks, and need to have their mesh component added to sequencer.
	if (GroupActor->GetClass() == ASkeletalMeshActor::StaticClass())
	{
		ASkeletalMeshActor* SkelMeshActor = CastChecked<ASkeletalMeshActor>(GroupActor);
		USkeletalMeshComponent* SkelMeshComponent = SkelMeshActor->GetSkeletalMeshComponent();
		// In matinee a component may be referenced in multiple material tracks, so check to see if this one is already bound.
		FGuid FoundGUID = NewSequence->FindPossessableObjectId(*SkelMeshComponent, SkelMeshActor);
		if (FoundGUID != FGuid())
		{
			ComponentGUID = FoundGUID;
		}
		else
		{
			ComponentGUID = NewMovieScene->AddPossessable(SkelMeshComponent->GetName(), SkelMeshComponent->GetClass());
			FMovieScenePossessable* ChildPossesable = NewMovieScene->FindPossessable(ComponentGUID);
			ChildPossesable->SetParent(PossessableGuid);
			NewSequence->BindPossessableObject(ComponentGUID, *SkelMeshComponent, GroupActor->GetWorld());
		}
	}
	else if (GroupActor->GetClass() == AStaticMeshActor::StaticClass())
	{
		AStaticMeshActor* StaticMeshActor = CastChecked<AStaticMeshActor>(GroupActor);
		UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent();
		FGuid FoundGUID = NewSequence->FindPossessableObjectId(*StaticMeshComponent, StaticMeshActor);
		if (FoundGUID != FGuid())
		{
			ComponentGUID = FoundGUID;
		}
		else
		{
			ComponentGUID = NewMovieScene->AddPossessable(StaticMeshComponent->GetName(), StaticMeshComponent->GetClass());
			FMovieScenePossessable* ChildPossesable = NewMovieScene->FindPossessable(ComponentGUID);
			ChildPossesable->SetParent(PossessableGuid);
			NewSequence->BindPossessableObject(ComponentGUID, *StaticMeshComponent, GroupActor->GetWorld());
		}
	}
	else
	{
		return FGuid();
	}
	return ComponentGUID;
}

void FMatineeConverter::ConvertInterpGroup(UInterpGroup* Group, AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings) const
{
	FGuid PossessableGuid;

	// Bind the group actor as a possessable						
	if (GroupActor)
	{
		UObject* BindingContext = GroupActor->GetWorld();
		PossessableGuid = NewMovieScene->AddPossessable(GroupActor->GetActorLabel(), GroupActor->GetClass());
		NewSequence->BindPossessableObject(PossessableGuid, *GroupActor, BindingContext);

		FindOrAddFolder(NewMovieScene, GroupActor, PossessableGuid);
	}

	ConvertInterpGroup(Group, PossessableGuid, GroupActor, NewSequence, NewMovieScene, NumWarnings);
}

void FMatineeConverter::ConvertInterpGroup(UInterpGroup* Group, FGuid ObjectBindingGuid, AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings) const
{
	TMap<UObject*, FGuid> BoundObjectsToGuids;

	for (int32 j=0; j<Group->InterpTracks.Num(); ++j)
	{
		UInterpTrack* Track = Group->InterpTracks[j];
		if (Track->IsDisabled())
		{
			continue;
		}

		// Handle each track class
		if (ExtendedInterpConverters.Find(Track->GetClass()))
		{
			ExtendedInterpConverters.Find(Track->GetClass())->Execute(Track, ObjectBindingGuid, NewMovieScene);
		}
		else if (Track->IsA(UInterpTrackMove::StaticClass()))
		{
			UInterpTrackMove* MatineeMoveTrack = StaticCast<UInterpTrackMove*>(Track);

			bool bHasKeyframes = MatineeMoveTrack->GetNumKeyframes() != 0;

			for (auto SubTrack : MatineeMoveTrack->SubTracks)
			{
				if (SubTrack->IsA(UInterpTrackMoveAxis::StaticClass()))
				{
					UInterpTrackMoveAxis* MoveSubTrack = Cast<UInterpTrackMoveAxis>(SubTrack);
					if (MoveSubTrack)
					{
						if (MoveSubTrack->FloatTrack.Points.Num() > 0)
						{
							bHasKeyframes = true;
							break;
						}
					}
				}
			}

			if ( bHasKeyframes && ObjectBindingGuid.IsValid())
			{
				FVector DefaultScale = GroupActor != nullptr ? GroupActor->GetActorScale() : FVector(1.f);
				UMovieScene3DTransformTrack* TransformTrack = NewMovieScene->AddTrack<UMovieScene3DTransformTrack>(ObjectBindingGuid);								
				FMatineeImportTools::CopyInterpMoveTrack(MatineeMoveTrack, TransformTrack, DefaultScale);
			}
		}
		else if (Track->IsA(UInterpTrackAnimControl::StaticClass()))
		{
			UInterpTrackAnimControl* MatineeAnimControlTrack = StaticCast<UInterpTrackAnimControl*>(Track);
			if (MatineeAnimControlTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneSkeletalAnimationTrack* SkeletalAnimationTrack = NewMovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(ObjectBindingGuid);	
				FFrameNumber EndPlaybackRange = UE::MovieScene::DiscreteExclusiveUpper(NewMovieScene->GetPlaybackRange());
				FMatineeImportTools::CopyInterpAnimControlTrack(MatineeAnimControlTrack, SkeletalAnimationTrack, EndPlaybackRange);
			}
		}
		else if (Track->IsA(UInterpTrackToggle::StaticClass()))
		{
			UInterpTrackToggle* MatineeParticleTrack = StaticCast<UInterpTrackToggle*>(Track);
			if (MatineeParticleTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneParticleTrack* ParticleTrack = NewMovieScene->AddTrack<UMovieSceneParticleTrack>(ObjectBindingGuid);	
				FMatineeImportTools::CopyInterpParticleTrack(MatineeParticleTrack, ParticleTrack);
			}
		}
		else if (Track->IsA(UInterpTrackEvent::StaticClass()))
		{
			UInterpTrackEvent* MatineeEventTrack = StaticCast<UInterpTrackEvent*>(Track);
			if (MatineeEventTrack->GetNumKeyframes() != 0)
			{
				UMovieSceneEventTrack* EventTrack = NewMovieScene->AddMasterTrack<UMovieSceneEventTrack>();
				FString EventTrackName = Group->GroupName.ToString() + TEXT("Events");
				EventTrack->SetDisplayName(FText::FromString(EventTrackName));
				FMatineeImportTools::CopyInterpEventTrack(MatineeEventTrack, EventTrack);

				static FName EventsFolder("Events");
				AddMasterTrackToFolder(NewMovieScene, EventTrack, EventsFolder);
			}
		}
		else if (Track->IsA(UInterpTrackSound::StaticClass()))
		{
			UInterpTrackSound* MatineeSoundTrack = StaticCast<UInterpTrackSound*>(Track);
			if (MatineeSoundTrack->GetNumKeyframes() != 0)
			{
				UMovieSceneAudioTrack* AudioTrack = NewMovieScene->AddMasterTrack<UMovieSceneAudioTrack>();
				FString AudioTrackName = Group->GroupName.ToString() + TEXT("Audio");
				AudioTrack->SetDisplayName(FText::FromString(AudioTrackName));					
				FMatineeImportTools::CopyInterpSoundTrack(MatineeSoundTrack, AudioTrack);

				static FName AudioFolder("Audio");
				AddMasterTrackToFolder(NewMovieScene, AudioTrack, AudioFolder);
			}
		}
		else if (Track->IsA(UInterpTrackBoolProp::StaticClass()))
		{
			UInterpTrackBoolProp* MatineeBoolTrack = StaticCast<UInterpTrackBoolProp*>(Track);
			if (MatineeBoolTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneBoolTrack* BoolTrack = AddPropertyTrack<UMovieSceneBoolTrack>(MatineeBoolTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
				if (BoolTrack)
				{
					FMatineeImportTools::CopyInterpBoolTrack(MatineeBoolTrack, BoolTrack);
				}
			}
		}
		else if (Track->IsA(UInterpTrackFloatProp::StaticClass()))
		{
			UInterpTrackFloatProp* MatineeFloatTrack = StaticCast<UInterpTrackFloatProp*>(Track);
			if (MatineeFloatTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneFloatTrack* FloatTrack = AddPropertyTrack<UMovieSceneFloatTrack>(MatineeFloatTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
				if (FloatTrack)
				{
					FMatineeImportTools::CopyInterpFloatTrack(MatineeFloatTrack, FloatTrack);
				}
			}
		}
		else if (Track->IsA(UInterpTrackFloatMaterialParam::StaticClass()))
		{
			UInterpTrackFloatMaterialParam* MatineeMaterialParamTrack = StaticCast<UInterpTrackFloatMaterialParam*>(Track);
			if (MatineeMaterialParamTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				FGuid ComponentGuid = FindComponentGuid(GroupActor, NewSequence, NewMovieScene, ObjectBindingGuid);

				if (ComponentGuid == FGuid())
				{
					continue;
				}

				CopyMaterialsToComponents(MatineeMaterialParamTrack->TargetMaterials.Num(), ComponentGuid, NewMovieScene, MatineeMaterialParamTrack);
				
			}
		}
		else if (Track->IsA(UInterpTrackVectorMaterialParam::StaticClass()))
		{
			UInterpTrackVectorMaterialParam* MatineeMaterialParamTrack = StaticCast<UInterpTrackVectorMaterialParam*>(Track);
			if (MatineeMaterialParamTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				FGuid ComponentGuid = FindComponentGuid(GroupActor, NewSequence, NewMovieScene, ObjectBindingGuid);

				if (ComponentGuid == FGuid())
				{
					continue;
				}

				CopyMaterialsToComponents(MatineeMaterialParamTrack->TargetMaterials.Num(), ComponentGuid, NewMovieScene, MatineeMaterialParamTrack);

			}
		}
		else if (Track->IsA(UInterpTrackVectorProp::StaticClass()))
		{
			UInterpTrackVectorProp* MatineeVectorTrack = StaticCast<UInterpTrackVectorProp*>(Track);
			if (MatineeVectorTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneVectorTrack* VectorTrack = AddPropertyTrack<UMovieSceneVectorTrack>(MatineeVectorTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
				if (VectorTrack)
				{
					VectorTrack->SetNumChannelsUsed(3);
					FMatineeImportTools::CopyInterpVectorTrack(MatineeVectorTrack, VectorTrack);
				}
			}
		}
		else if (Track->IsA(UInterpTrackColorProp::StaticClass()))
		{
			UInterpTrackColorProp* MatineeColorTrack = StaticCast<UInterpTrackColorProp*>(Track);
			if (MatineeColorTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneColorTrack* ColorTrack = AddPropertyTrack<UMovieSceneColorTrack>(MatineeColorTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
				if (ColorTrack)
				{
					FMatineeImportTools::CopyInterpColorTrack(MatineeColorTrack, ColorTrack);
				}
			}
		}
		else if (Track->IsA(UInterpTrackLinearColorProp::StaticClass()))
		{
			UInterpTrackLinearColorProp* MatineeLinearColorTrack = StaticCast<UInterpTrackLinearColorProp*>(Track);
			if (MatineeLinearColorTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneColorTrack* ColorTrack = AddPropertyTrack<UMovieSceneColorTrack>(MatineeLinearColorTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
				if (ColorTrack)
				{
					FMatineeImportTools::CopyInterpLinearColorTrack(MatineeLinearColorTrack, ColorTrack);
				}
			}
		}
		else if (Track->IsA(UInterpTrackVisibility::StaticClass()))
		{
			UInterpTrackVisibility* MatineeVisibilityTrack = StaticCast<UInterpTrackVisibility*>(Track);
			if (MatineeVisibilityTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
			{
				UMovieSceneVisibilityTrack* VisibilityTrack = NewMovieScene->AddTrack<UMovieSceneVisibilityTrack>(ObjectBindingGuid);	
				if (VisibilityTrack)
				{
					VisibilityTrack->SetPropertyNameAndPath(TEXT("bHidden"), GroupActor->GetPathName() + TEXT(".bHidden"));

					FMatineeImportTools::CopyInterpVisibilityTrack(MatineeVisibilityTrack, VisibilityTrack);
				}
			}
		}
		else if (Track->IsA(UInterpTrackDirector::StaticClass()))
		{
			// Intentionally left blank - The director track is converted in a separate pass below.
		}
		else
		{
			if (GroupActor)
			{
				UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s' for '%s'."), *Track->TrackTitle, *GroupActor->GetActorLabel());
			}
			else
			{
				UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s'."), *Track->TrackTitle);
			}

			++NumWarnings;
		}
	}
}

#undef LOCTEXT_NAMESPACE
