// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPSplineActor.h"
#include "VPSplinePointData.h"
#include "VPSplineLog.h"

#include "CineCameraComponent.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneToolHelpers.h"
#include "VPUtilitiesEditorSettings.h"
#endif

AVPSplineActor::AVPSplineActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SplineComp = CreateDefaultSubobject<UVPSplineComponent>("VPSpline");
	SetRootComponent(SplineComp);

	SplineAttachment = CreateDefaultSubobject<USceneComponent>("Spline Attachment");
	SplineAttachment->SetupAttachment(SplineComp);

#if WITH_EDITORONLY_DATA
	PreviewMeshScale = 1.f;

	if (!IsRunningCommandlet())
	{
		FSoftObjectPath PreviewMeshPath = GetDefault<UVPUtilitiesEditorSettings>()->VPSplinePreviewMeshPath;
		static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshFinder(*PreviewMeshPath.ToString());
		if (MeshFinder.Succeeded())
		{
			PreviewMesh = MeshFinder.Object;
		}
		else
		{
			UE_LOG(LogVPSpline, Warning, TEXT("Failed to find spline preview mesh: %s"), *PreviewMeshPath.ToString());
		}
	}

	PreviewMeshComp = CreateOptionalDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	if (PreviewMeshComp)
	{
		PreviewMeshComp->SetIsVisualizationComponent(true);
		PreviewMeshComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		PreviewMeshComp->bHiddenInGame = true;
		PreviewMeshComp->CastShadow = false;
		PreviewMeshComp->SetupAttachment(SplineAttachment);
	}
#endif

}

void AVPSplineActor::SetPointFromActor(const AActor* Actor)
{
	if (Actor != nullptr)
	{
		FVPSplinePointData Data;
		if (const ACineCameraActor* CineCameraActor = Cast<ACineCameraActor>(Actor))
		{
			if (UCineCameraComponent* CameraComp = CineCameraActor->GetCineCameraComponent())
			{
				Data.Location = CameraComp->GetComponentLocation();
				Data.Rotation = CameraComp->GetComponentRotation();
				Data.FocalLength = CameraComp->CurrentFocalLength;
				Data.Aperture = CameraComp->CurrentAperture;
				Data.FocusDistance = CameraComp->CurrentFocusDistance;
			}
		}
		else
		{
			Data.Location = Actor->GetActorLocation();
			Data.Rotation = Actor->GetActorRotation();
		}

		SetPointByValue(Data);
	}
}

void AVPSplineActor::SetPointByValue(const FVPSplinePointData& Data)
{
	int32 Index;
	if (SplineComp->FindSplineDataAtPosition(CurrentPosition, Index))
	{
		UE_LOG(LogVPSpline, Display, TEXT("Updating keyframe at %d"), Index);
		SplineComp->UpdateSplineDataAtIndex(Index, Data);
	}
	else
	{
		UE_LOG(LogVPSpline, Display, TEXT("Adding keyframe at %f"), CurrentPosition);
		SplineComp->AddSplineDataAtPosition(CurrentPosition, Data);
	}
}

void AVPSplineActor::RemoveCurrentPoint()
{
	int32 Index;
	if (SplineComp->FindSplineDataAtPosition(CurrentPosition, Index))
	{
		UE_LOG(LogVPSpline, Display, TEXT("Removing keyframe at %d"), Index);
		SplineComp->RemoveSplinePoint(Index);
	}
}


void AVPSplineActor::GoToNextPosition()
{
	UVPSplineMetadata* Metadata = SplineComp ? Cast<UVPSplineMetadata>(SplineComp->GetSplinePointsMetadata()) : nullptr;
	if (Metadata != nullptr)
	{
		for (int32 i = 0; i < Metadata->NormalizedPosition.Points.Num(); ++i)
		{
			if (CurrentPosition < Metadata->NormalizedPosition.Points[i].OutVal)
			{
				CurrentPosition = Metadata->NormalizedPosition.Points[i].OutVal;
				break;
			}
		}
	}
}

void AVPSplineActor::GoToPrevPosition()
{
	UVPSplineMetadata* Metadata = SplineComp ? Cast<UVPSplineMetadata>(SplineComp->GetSplinePointsMetadata()) : nullptr;
	if (Metadata != nullptr)
	{
		for (int32 i = Metadata->NormalizedPosition.Points.Num(); i > 0; --i)
		{
			if (CurrentPosition > Metadata->NormalizedPosition.Points[i-1].OutVal)
			{
				CurrentPosition = Metadata->NormalizedPosition.Points[i-1].OutVal;
				break;
			}
		}
	}
}



void AVPSplineActor::BakePointsToSequence()
{
#if WITH_EDITOR
	const FName PropName = FName(TEXT("CurrentPosition"));
	const FString PropPath = PropName.ToString();

	ULevelSequence* LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (LevelSequence != nullptr)
	{
		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (MovieScene != nullptr)
		{
			FGuid ActorBinding;
			ActorBinding = LevelSequence->FindBindingFromObject(this, GetWorld());
			if (!ActorBinding.IsValid())
			{
				ActorBinding = MovieScene->AddPossessable(GetActorLabel(), GetClass());
				LevelSequence->BindPossessableObject(ActorBinding, *this, GetWorld());
				MovieScene->Modify();
			}

			UMovieSceneFloatTrack* ParamTrack = MovieScene->FindTrack<UMovieSceneFloatTrack>(ActorBinding, PropName);
			if (ParamTrack == nullptr)
			{
				ParamTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(ActorBinding);
				if (!ParamTrack)
				{
					UE_LOG(LogVPSpline, Error, TEXT("%s: Failed to add float track"), *GetActorNameOrLabel());
					return;
				}
				ParamTrack->RemoveAllAnimationData();
				ParamTrack->SetPropertyNameAndPath(PropName, PropPath);
				ParamTrack->Modify();
				UE_LOG(LogVPSpline, Log, TEXT("%s: %s track added"), *GetActorNameOrLabel(), *PropPath);
			}

			bool bSectionAdded = false;
			UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(ParamTrack->FindOrAddSection(0, bSectionAdded));
			Section->SetRange(TRange<FFrameNumber>::All());
			FMovieSceneFloatChannel* Channel = Section->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
			ensure(Channel);
			Channel->Reset();
			Channel->SetDefault(0.0f);
			TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channel->GetData();
			UVPSplineMetadata* Metadata = SplineComp ? Cast<UVPSplineMetadata>(SplineComp->GetSplinePointsMetadata()) : nullptr;
			
			if (Metadata != nullptr)
			{
				TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
				int32 StartFrame = PlaybackRange.GetLowerBoundValue().Value;
				int32 EndFrame = PlaybackRange.GetUpperBoundValue().Value;
				int32 NumPoints = Metadata->NormalizedPosition.Points.Num();
				for (int32 i = 0; i < NumPoints; ++i)
				{
					float Value = Metadata->NormalizedPosition.Points[i].OutVal;
					int32 Frame = FMath::FloorToInt((float)(EndFrame - StartFrame) * Value) + StartFrame;
					AddKeyToChannel(Channel, FFrameNumber(Frame), Value, EMovieSceneKeyInterpolation::Linear);
				}
			}
		}
		ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	}
#endif
}

void AVPSplineActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateSplineAttachment();
}

bool AVPSplineActor::ShouldTickIfViewportsOnly() const
{
	return true;
}

USceneComponent* AVPSplineActor::GetDefaultAttachComponent() const
{
	return SplineAttachment;
}

void AVPSplineActor::UpdateSplineAttachment()
{
	if(!GetWorld())
	{
		return;
	}

	if (SplineComp && SplineAttachment)
	{
		float InputKey = SplineComp->GetInputKeyAtPosition(CurrentPosition);
		FVector const Position = SplineComp->GetLocationAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
		FQuat const Rotation = SplineComp->GetQuaternionAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
		SplineAttachment->SetWorldTransform(FTransform(Rotation, Position));
	}

#if WITH_EDITOR
	if (GIsEditor)
	{
		const UWorld* const MyWorld = GetWorld();
		if (MyWorld && !MyWorld->IsGameWorld())
		{
			UpdatePreviewMesh();
		}
	}
#endif
}

#if WITH_EDITOR
void AVPSplineActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateSplineAttachment();
}
void AVPSplineActor::PostEditUndo()
{
	Super::PostEditUndo();
	UpdateSplineAttachment();
}

void AVPSplineActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	UpdateSplineAttachment();
}
#endif

#if WITH_EDITORONLY_DATA
void AVPSplineActor::UpdatePreviewMesh()
{
	if (PreviewMeshComp && PreviewMeshComp->GetStaticMesh() != PreviewMesh)
	{
		PreviewMeshComp->SetStaticMesh(PreviewMesh);
	}

	if (PreviewMeshComp)
	{
		PreviewMeshComp->SetWorldScale3D(FVector(PreviewMeshScale, PreviewMeshScale, PreviewMeshScale));
	}
}
#endif