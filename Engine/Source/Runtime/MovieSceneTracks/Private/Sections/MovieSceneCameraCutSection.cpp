// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneCameraCutSection.h"

#include "MovieScene.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneTransformTrack.h"
#include "MovieScene.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Evaluation/MovieSceneCameraCutTemplate.h"
#include "Compilation/MovieSceneTemplateInterrogation.h"
#include "IMovieScenePlayer.h"
#include "Camera/CameraComponent.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"

/* UMovieSceneCameraCutSection interface
 *****************************************************************************/

UMovieSceneCameraCutSection::UMovieSceneCameraCutSection(const FObjectInitializer& Init)
	: Super(Init)
{
	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
		 EMovieSceneCompletionMode::RestoreState : 
		 EMovieSceneCompletionMode::ProjectDefault);

	SetBlendType(EMovieSceneBlendType::Absolute);
}

FMovieSceneEvalTemplatePtr UMovieSceneCameraCutSection::GenerateTemplate() const
{
	TOptional<FTransform> CutTransform;

	UMovieScene* MovieScene = GetTypedOuter<UMovieScene>();
	check(MovieScene);

	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		if (Binding.GetObjectGuid() == CameraBindingID.GetGuid())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
				if (TransformTrack)
				{
					// Extract the transform
					FMovieSceneEvaluationTrack TransformTrackTemplate = TransformTrack->GenerateTrackTemplate();
					FMovieSceneContext Context = FMovieSceneEvaluationRange(GetInclusiveStartFrame(), MovieScene->GetTickResolution());

					FMovieSceneInterrogationData Container;
					TransformTrackTemplate.Interrogate(Context, Container);

					for (const FTransformData& Transform : Container.Iterate<FTransformData>(UMovieScene3DTransformSection::GetInterrogationKey()))
					{
						CutTransform = FTransform(Transform.Rotation.Quaternion(), Transform.Translation, Transform.Scale);
						break;
					}
				}
			}
		}
	}

	return FMovieSceneCameraCutSectionTemplate(*this, CutTransform);
}

void UMovieSceneCameraCutSection::OnBindingsUpdated(const TMap<FGuid, FGuid>& OldGuidToNewGuidMap)
{
	if (OldGuidToNewGuidMap.Contains(CameraBindingID.GetGuid()))
	{
		Modify();

		CameraBindingID.SetGuid(OldGuidToNewGuidMap[CameraBindingID.GetGuid()]);
	}
}

void UMovieSceneCameraCutSection::GetReferencedBindings(TArray<FGuid>& OutBindings)
{
	OutBindings.Add(CameraBindingID.GetGuid());
}

void UMovieSceneCameraCutSection::PostLoad()
{
	Super::PostLoad();

	if (CameraGuid_DEPRECATED.IsValid())
	{
		if (!CameraBindingID.IsValid())
		{
			CameraBindingID = FMovieSceneObjectBindingID(CameraGuid_DEPRECATED, MovieSceneSequenceID::Root, EMovieSceneObjectBindingSpace::Local);
		}
		CameraGuid_DEPRECATED.Invalidate();
	}
}


UCameraComponent* UMovieSceneCameraCutSection::GetFirstCamera(IMovieScenePlayer& Player, FMovieSceneSequenceID SequenceID) const
{
	if (CameraBindingID.GetSequenceID().IsValid())
	{
		// Ensure that this ID is resolvable from the root, based on the current local sequence ID
		FMovieSceneObjectBindingID RootBindingID = CameraBindingID.ResolveLocalToRoot(SequenceID, Player.GetEvaluationTemplate().GetHierarchy());
		SequenceID = RootBindingID.GetSequenceID();
	}

	for (TWeakObjectPtr<>& WeakObject : Player.FindBoundObjects(CameraBindingID.GetGuid(), SequenceID))
	{
		if (UObject* Object = WeakObject .Get())
		{
			UCameraComponent* Camera = MovieSceneHelpers::CameraComponentFromRuntimeObject(Object);
			if (Camera)
			{
				return Camera;
			}
		}
	}

	return nullptr;
}


#if WITH_EDITOR

void UMovieSceneCameraCutSection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMovieSceneCameraCutSection, SectionRange))
	{
		if (UMovieSceneCameraCutTrack* Track = GetTypedOuter<UMovieSceneCameraCutTrack>())
		{
			Track->OnSectionMoved(*this, EPropertyChangeType::ValueSet);
		}
	}
}

#endif
