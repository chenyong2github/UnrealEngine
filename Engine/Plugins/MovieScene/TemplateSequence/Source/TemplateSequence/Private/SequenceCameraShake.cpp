// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequenceCameraShake.h"
#include "Algo/IndexOf.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Containers/ArrayView.h"
#include "EntitySystem/MovieSceneBoundSceneComponentInstantiator.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "MovieSceneTracksComponentTypes.h"
#include "MovieSceneFwd.h"
#include "MovieSceneTimeHelpers.h"

#if !IS_MONOLITHIC
	UE::MovieScene::FEntityManager*& GEntityManagerForDebugging = UE::MovieScene::GEntityManagerForDebuggingVisualizers;
#endif

namespace UE
{
namespace MovieScene
{

FIntermediate3DTransform GetCameraStandInTransform(const UObject* Object)
{
	const USequenceCameraShakeCameraStandIn* CameraStandIn = CastChecked<const USequenceCameraShakeCameraStandIn>(Object);
	FIntermediate3DTransform Result;
	ConvertOperationalProperty(CameraStandIn->GetTransform(), Result);
	return Result;
}

void SetCameraStandInTransform(UObject* Object, const FIntermediate3DTransform& InTransform)
{
	USequenceCameraShakeCameraStandIn* CameraStandIn = CastChecked<USequenceCameraShakeCameraStandIn>(Object);
	FTransform Result;
	ConvertOperationalProperty(InTransform, Result);
	CameraStandIn->SetTransform(Result);
}

}
}

USequenceCameraShake::USequenceCameraShake(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, PlayRate(1.f)
	, Scale(1.f)
	, BlendInTime(0.2f)
	, BlendOutTime(0.4f)
	, RandomSegmentDuration(0.f)
	, bRandomSegment(false)
{
	CameraStandIn = ObjInit.CreateDefaultSubobject<USequenceCameraShakeCameraStandIn>(this, TEXT("SequenceCameraShake_CameraStandIn"), true);
	Player = ObjInit.CreateDefaultSubobject<USequenceCameraShakeSequencePlayer>(this, TEXT("SequenceCameraShake_Player"), true);

	// Make the player always use our stand-in object whenever a sequence wants to spawn or possess an object.
	Player->SetBoundObjectOverride(CameraStandIn);

	// Make sure we have our custom accessors registered for our stand-in class.
	RegisterCameraStandIn();
}

void USequenceCameraShake::GetShakeInfoImpl(FCameraShakeInfo& OutInfo) const
{
	if (Sequence != nullptr)
	{
		if (UMovieScene* MovieScene = Sequence->GetMovieScene())
		{
			if (bRandomSegment)
			{
				OutInfo.Duration = FCameraShakeDuration(RandomSegmentDuration);
			}
			else
			{
				const FFrameRate TickResolution = MovieScene->GetTickResolution();
				const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
				const float Duration = TickResolution.AsSeconds(PlaybackRange.Size<FFrameNumber>());
				OutInfo.Duration = FCameraShakeDuration(Duration);
			}

			OutInfo.BlendIn = BlendInTime;
			OutInfo.BlendOut = BlendOutTime;
		}
	}
}

void USequenceCameraShake::StartShakeImpl()
{
	using namespace UE::MovieScene;

	if (!ensure(Sequence))
	{
		return;
	}

	// Initialize it and start playing.
	Player->Initialize(Sequence);

	Player->Play(bRandomSegment, bRandomSegment);
}

void USequenceCameraShake::UpdateShakeImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	using namespace UE::MovieScene;

	// Reset the stand-in values.
	check(CameraStandIn);
	CameraStandIn->SetTransform(FTransform::Identity);
	CameraStandIn->FieldOfView = 0.f;

	// Update the sequence.
	const FFrameRate TickResolution = Player->GetInputRate();
	const FFrameTime NewPosition = Player->GetCurrentPosition() + Params.DeltaTime * PlayRate * TickResolution;
	Player->Update(NewPosition);

	// Grab the values and feed that into the camera shake result.
	const FTransform AnimatedTransform = CameraStandIn->GetTransform();
	OutResult.Location = AnimatedTransform.GetLocation();
	OutResult.Rotation = AnimatedTransform.GetRotation().Rotator();
	OutResult.FOV = CameraStandIn->FieldOfView;
}

void USequenceCameraShake::StopShakeImpl(bool bImmediately)
{
	using namespace UE::MovieScene;

	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (ensure(Player != nullptr && MovieScene != nullptr))
	{
		if (bImmediately)
		{
			// Stop playing!
			Player->Stop();
		}
		else
		{
			// Move the playback position to the start of the blend out.
			const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
			if (PlaybackRange.HasUpperBound())
			{
				const FFrameRate TickResolution = Player->GetInputRate();
				const FFrameTime BlendOutTimeInFrames  = BlendOutTime * TickResolution;
				const FFrameTime BlendOutStartFrame = FFrameTime(PlaybackRange.GetUpperBoundValue()) - BlendOutTimeInFrames;
				Player->Jump(BlendOutStartFrame);
			}
		}
	}
}

void USequenceCameraShake::TeardownShakeImpl()
{
	using namespace UE::MovieScene;

	Player->Stop();
	
	if (UMovieSceneEntitySystemLinker* Linker = Player->GetEvaluationTemplate().GetEntitySystemLinker())
	{
		Linker->Reset();
	}
}

void USequenceCameraShake::RegisterCameraStandIn()
{
	using namespace UE::MovieScene;

	static bool bHasRegistered = false;
	if (!bHasRegistered)
	{
		FMovieSceneTracksComponentTypes* TracksComponentTypes = FMovieSceneTracksComponentTypes::Get();
		TracksComponentTypes->Accessors.ComponentTransform.Add(USequenceCameraShakeCameraStandIn::StaticClass(), TEXT("Transform"), &GetCameraStandInTransform, &SetCameraStandInTransform);

		bHasRegistered = true;
	}
}

USequenceCameraShakeSequencePlayer::USequenceCameraShakeSequencePlayer(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, StartFrame(0)
	, Status(EMovieScenePlayerStatus::Stopped)
{
	PlayPosition.Reset(FFrameTime(0));
}

USequenceCameraShakeSequencePlayer::~USequenceCameraShakeSequencePlayer()
{
}

void USequenceCameraShakeSequencePlayer::BeginDestroy()
{
	RootTemplateInstance.BeginDestroy();

	Super::BeginDestroy();
}

UMovieSceneEntitySystemLinker* USequenceCameraShakeSequencePlayer::ConstructEntitySystemLinker()
{
	UMovieSceneEntitySystemLinker* Linker = NewObject<UMovieSceneEntitySystemLinker>(GetTransientPackage());
	return Linker;
}

EMovieScenePlayerStatus::Type USequenceCameraShakeSequencePlayer::GetPlaybackStatus() const
{
	return Status;
}

void USequenceCameraShakeSequencePlayer::ResolveBoundObjects(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, UMovieSceneSequence& InSequence, UObject* InResolutionContext, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	if (BoundObjectOverride)
	{
		OutObjects.Add(BoundObjectOverride);
	}
}

void USequenceCameraShakeSequencePlayer::SetBoundObjectOverride(UObject* InObject)
{
	BoundObjectOverride = InObject;

	SpawnRegister.SetSpawnedObject(InObject);
}

void USequenceCameraShakeSequencePlayer::Initialize(UMovieSceneSequence* InSequence)
{
	checkf(InSequence, TEXT("Invalid sequence given to player"));
	
	if (Sequence)
	{
		Stop();
	}

	Sequence = InSequence;

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (ensure(MovieScene))
	{
		const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const EMovieSceneEvaluationType EvaluationType = MovieScene->GetEvaluationType();
		
		PlayPosition.SetTimeBase(DisplayRate, TickResolution, EvaluationType);

		const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
		StartFrame = UE::MovieScene::DiscreteInclusiveLower(PlaybackRange);
		DurationFrames = PlaybackRange.Size<FFrameNumber>();
	}
	else
	{
		StartFrame = FFrameNumber(0);
		DurationFrames = 0;
	}

	PlayPosition.Reset(StartFrame);

	RootTemplateInstance.Initialize(*Sequence, *this, nullptr);
}

void USequenceCameraShakeSequencePlayer::Play(bool bLoop, bool bRandomStartTime)
{
	checkf(Sequence, TEXT("No sequence is set on this player, did you forget to call Initialize?"));
	checkf(RootTemplateInstance.IsValid(), TEXT("No evaluation template was created, did you forget to call Initialize?"));
	checkf(Status == EMovieScenePlayerStatus::Stopped, TEXT("This player must be stopped before it can play"));

	// Move the playback position randomly in our playback range if we want a random start time.
	if (bRandomStartTime)
	{
		UMovieScene* MovieScene = Sequence->GetMovieScene();

		const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
		
		const int32 RandomStartFrameOffset = FMath::RandHelper(DurationFrames.Value);
		PlayPosition.Reset(StartFrame + RandomStartFrameOffset);
	}

	// Start playing by evaluating the sequence at the start time.
	bIsLooping = bLoop;
	Status = EMovieScenePlayerStatus::Playing;

	const FMovieSceneEvaluationRange Range = PlayPosition.PlayTo(PlayPosition.GetCurrentPosition());
	const FMovieSceneContext Context(Range, Status);
	RootTemplateInstance.Evaluate(Context, *this);
}

void USequenceCameraShakeSequencePlayer::Update(FFrameTime NewPosition)
{
	check(Status == EMovieScenePlayerStatus::Playing);
	check(RootTemplateInstance.IsValid());

	if (bIsLooping)
	{
		// Unlike the level sequence player, we don't care about making sure to play the last few frames
		// of the sequence before looping: we can jump straight to the looped time because we know we
		// don't have any events to fire or anything like that.
		//
		// Arguably we could have some cumulative animation mode running on some properties but let's call
		// this a limitation for now.
		//
		while (NewPosition.FrameNumber >= StartFrame + DurationFrames)
		{
			NewPosition.FrameNumber -= DurationFrames;
			PlayPosition.Reset(StartFrame);
		}
	}

	const FMovieSceneEvaluationRange Range = PlayPosition.PlayTo(NewPosition);
	const FMovieSceneContext Context(Range, Status);
	RootTemplateInstance.Evaluate(Context, *this);
}

void USequenceCameraShakeSequencePlayer::Jump(FFrameTime NewPosition)
{
	PlayPosition.JumpTo(NewPosition);
}

void USequenceCameraShakeSequencePlayer::Stop()
{
	Status = EMovieScenePlayerStatus::Stopped;

	PlayPosition.Reset(StartFrame);

	if (RootTemplateInstance.IsValid())
	{
		RootTemplateInstance.Finish(*this);
	}
}
