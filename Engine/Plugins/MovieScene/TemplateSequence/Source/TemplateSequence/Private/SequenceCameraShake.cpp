// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequenceCameraShake.h"
#include "Algo/IndexOf.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CineCameraActor.h"
#include "Containers/ArrayView.h"
#include "EntitySystem/MovieSceneBoundSceneComponentInstantiator.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "GameFramework/WorldSettings.h"
#include "MovieSceneFwd.h"
#include "MovieSceneTimeHelpers.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieScenePropertySystemTypes.inl"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "MovieSceneTracksComponentTypes.h"

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

template<typename PropertyTraits>
void UpdateInitialPropertyValues(UMovieSceneEntitySystemLinker* Linker, const TPropertyComponents<PropertyTraits>& PropertyComponents)
{
	const FBuiltInComponentTypes* const BuiltInComponents = FBuiltInComponentTypes::Get();

	const FPropertyDefinition& PropertyDefinition = BuiltInComponents->PropertyRegistry.GetDefinition(PropertyComponents.CompositeID);

	TGetPropertyValues<PropertyTraits> GetProperties(PropertyDefinition.CustomPropertyRegistration);

	FEntityTaskBuilder()
	.Read(BuiltInComponents->BoundObject)
	.ReadOneOf(BuiltInComponents->CustomPropertyIndex, BuiltInComponents->FastPropertyOffset, BuiltInComponents->SlowProperty)
	.Write(PropertyComponents.InitialValue)
	.FilterAll({ PropertyComponents.PropertyTag })
	.SetDesiredThread(Linker->EntityManager.GetGatherThread())
	.RunInline_PerAllocation(&Linker->EntityManager, GetProperties);
}

}
}

USequenceCameraShakeCameraStandIn::USequenceCameraShakeCameraStandIn(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
}

void USequenceCameraShakeCameraStandIn::Initialize(UTemplateSequence* TemplateSequence)
{
	AActor* CameraTemplate = nullptr;
	UMovieScene* MovieScene = TemplateSequence->GetMovieScene();
	const FGuid RootObjectBindingID = TemplateSequence->GetRootObjectBindingID();
	if (MovieScene && RootObjectBindingID.IsValid())
	{
		if (FMovieSceneSpawnable* RootObjectSpawnable = MovieScene->FindSpawnable(RootObjectBindingID))
		{
			CameraTemplate = Cast<AActor>(RootObjectSpawnable->GetObjectTemplate());
		}
	}

	bIsCineCamera = false;
	bool bGotInitialValues = false;

	if (CameraTemplate)
	{
		if (UCineCameraComponent* CineCameraComponent = CameraTemplate->FindComponentByClass<UCineCameraComponent>())
		{
			bIsCineCamera = true;
			bGotInitialValues = true;

			FieldOfView = CineCameraComponent->FieldOfView;
			AspectRatio = CineCameraComponent->AspectRatio;
			PostProcessSettings = CineCameraComponent->PostProcessSettings;
			PostProcessBlendWeight = CineCameraComponent->PostProcessBlendWeight;

			Filmback = CineCameraComponent->Filmback;
			LensSettings = CineCameraComponent->LensSettings;
			FocusSettings = CineCameraComponent->FocusSettings;
			CurrentFocalLength = CineCameraComponent->CurrentFocalLength;
			CurrentAperture = CineCameraComponent->CurrentAperture;
			CurrentFocusDistance = CineCameraComponent->CurrentFocusDistance;

			// Get the world unit to meters scale.
			UWorld const* const World = GetWorld();
			AWorldSettings const* const WorldSettings = World ? World->GetWorldSettings() : nullptr;
			WorldToMeters = WorldSettings ? WorldSettings->WorldToMeters : 100.f;
		}
		else if (UCameraComponent* CameraComponent = CameraTemplate->FindComponentByClass<UCameraComponent>())
		{
			bGotInitialValues = true;

			FieldOfView = CameraComponent->FieldOfView;
			AspectRatio = CameraComponent->AspectRatio;
			PostProcessSettings = CameraComponent->PostProcessSettings;
			PostProcessBlendWeight = CameraComponent->PostProcessBlendWeight;
		}

		// We reset our transform to identity because we want to be able to treat the animated 
		// transform as an additive value in local camera space. As a result, we won't need to 
		// synchronize it with the current view info in Reset below.
		Transform = FTransform::Identity;
	}

	ensureMsgf(
			bGotInitialValues, 
			TEXT("Couldn't initialize sequence camera shake: the given sequence may not be animating a camera!"));
}

void USequenceCameraShakeCameraStandIn::Reset(const FMinimalViewInfo& ViewInfo)
{
	// We reset all the other properties to the current view's values because a lot of them, like 
	// FieldOfView, don't have any "zero" value that makes sense. We'll figure out the delta in the
	// update code.
	bConstrainAspectRatio = ViewInfo.bConstrainAspectRatio;
	AspectRatio = ViewInfo.AspectRatio;
	FieldOfView = ViewInfo.FOV;
	PostProcessSettings = ViewInfo.PostProcessSettings;
	PostProcessBlendWeight = ViewInfo.PostProcessBlendWeight;

	// We've set the FieldOfView we have to update the CurrentFocalLength accordingly.
	CurrentFocalLength = (Filmback.SensorWidth / 2.f) / FMath::Tan(FMath::DegreesToRadians(FieldOfView / 2.f));

	RecalcDerivedData();
}

void USequenceCameraShakeCameraStandIn::RecalcDerivedData()
{
	if (bIsCineCamera)
	{
		CurrentFocalLength = FMath::Clamp(CurrentFocalLength, LensSettings.MinFocalLength, LensSettings.MaxFocalLength);
		CurrentAperture = FMath::Clamp(CurrentAperture, LensSettings.MinFStop, LensSettings.MaxFStop);

		float const MinFocusDistInWorldUnits = LensSettings.MinimumFocusDistance * (WorldToMeters / 1000.f);	// convert mm to uu
		FocusSettings.ManualFocusDistance = FMath::Max(FocusSettings.ManualFocusDistance, MinFocusDistInWorldUnits);

		float const HorizontalFieldOfView = (CurrentFocalLength > 0.f)
			? FMath::RadiansToDegrees(2.f * FMath::Atan(Filmback.SensorWidth / (2.f * CurrentFocalLength)))
			: 0.f;
		FieldOfView = HorizontalFieldOfView;
		Filmback.SensorAspectRatio = (Filmback.SensorHeight > 0.f) ? (Filmback.SensorWidth / Filmback.SensorHeight) : 0.f;
		AspectRatio = Filmback.SensorAspectRatio;
	}
}

USequenceCameraShakePattern::USequenceCameraShakePattern(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, PlayRate(1.f)
	, Scale(1.f)
	, BlendInTime(0.2f)
	, BlendOutTime(0.4f)
	, RandomSegmentDuration(0.f)
	, bRandomSegment(false)
{
	CameraStandIn = CreateDefaultSubobject<USequenceCameraShakeCameraStandIn>(TEXT("CameraStandIn"), true);
	Player = CreateDefaultSubobject<USequenceCameraShakeSequencePlayer>(TEXT("Player"), true);

	// Make sure we have our custom accessors registered for our stand-in class.
	RegisterCameraStandIn();
}

void USequenceCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
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

void USequenceCameraShakePattern::StartShakePatternImpl(const FCameraShakeStartParams& Params)
{
	using namespace UE::MovieScene;

	if (!ensure(Sequence))
	{
		return;
	}

	// Initialize our stand-in object.
	CameraStandIn->Initialize(Sequence);
	
	// Make the player always use our stand-in object whenever a sequence wants to spawn or possess an object.
	Player->SetBoundObjectOverride(CameraStandIn);

	// Initialize it and start playing.
	Player->Initialize(Sequence);
	Player->Play(bRandomSegment, bRandomSegment);
}

void USequenceCameraShakePattern::UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	const FFrameRate TickResolution = Player->GetInputRate();
	const FFrameTime NewPosition = Player->GetCurrentPosition() + Params.DeltaTime * PlayRate * TickResolution;
	UpdateCamera(NewPosition, Params.POV, OutResult);
}

void USequenceCameraShakePattern::ScrubShakePatternImpl(const FCameraShakeScrubParams& Params, FCameraShakeUpdateResult& OutResult)
{
	Player->StartScrubbing();
	{
		const FFrameRate TickResolution = Player->GetInputRate();
		const FFrameTime NewPosition = Params.AbsoluteTime * PlayRate * TickResolution;
		UpdateCamera(NewPosition, Params.POV, OutResult);
	}
	Player->EndScrubbing();
}

void USequenceCameraShakePattern::StopShakePatternImpl(const FCameraShakeStopParams& Params)
{
	using namespace UE::MovieScene;

	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (ensure(Player != nullptr && MovieScene != nullptr))
	{
		if (Params.bImmediately)
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

void USequenceCameraShakePattern::TeardownShakePatternImpl()
{
	using namespace UE::MovieScene;

	Player->Stop();
	
	if (UMovieSceneEntitySystemLinker* Linker = Player->GetEvaluationTemplate().GetEntitySystemLinker())
	{
		Linker->Reset();
	}
}

void USequenceCameraShakePattern::RegisterCameraStandIn()
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

void USequenceCameraShakePattern::UpdateCamera(FFrameTime NewPosition, const FMinimalViewInfo& InPOV, FCameraShakeUpdateResult& OutResult)
{
	using namespace UE::MovieScene;

	check(CameraStandIn);

	// Reset the camera stand-in's properties based on the new "current" (unshaken) values.
	CameraStandIn->Reset(InPOV);

	// Sequencer animates things based on the initial values cached when the sequence started. But here we want
	// to animate things based on the moving current values of the camera... i.e., we want to shake a constantly
	// moving camera. So every frame, we need to update the initial values that sequencer uses.
	UpdateInitialCameraStandInPropertyValues();

	// Get the "unshaken" properties that need to be treated additively.
	const float OriginalFieldOfView = CameraStandIn->FieldOfView;

	// Update the sequence.
	Player->Update(NewPosition);

	// Recalculate properties that might be invalidated by other properties having been animated.
	CameraStandIn->RecalcDerivedData();

	// Grab the final animated (shaken) values, figure out the delta, apply scale, and feed that into the 
	// camera shake result.
	// Transform is always treated as a local, additive value. The data better be good.
	const FTransform ShakenTransform = CameraStandIn->GetTransform();
	OutResult.Location = ShakenTransform.GetLocation() * Scale;
	OutResult.Rotation = ShakenTransform.GetRotation().Rotator() * Scale;

	// FieldOfView follows the current camera's value every frame, so we can compute how much the shake is
	// changing it.
	const float ShakenFieldOfView = CameraStandIn->FieldOfView;
	const float DeltaFieldOfView = ShakenFieldOfView - OriginalFieldOfView;
	OutResult.FOV = DeltaFieldOfView * Scale;

	// The other properties aren't treated as additive.
	OutResult.PostProcessSettings = CameraStandIn->PostProcessSettings;
	OutResult.PostProcessBlendWeight = CameraStandIn->PostProcessBlendWeight;
}

void USequenceCameraShakePattern::UpdateInitialCameraStandInPropertyValues()
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TrackComponents = FMovieSceneTracksComponentTypes::Get();

	check(Player);
	UMovieSceneEntitySystemLinker* Linker = Player->GetEvaluationTemplate().GetEntitySystemLinker();

	check(Linker);
	UE::MovieScene::UpdateInitialPropertyValues(Linker, TrackComponents->Float);
	// TODO: also do uint8:1/boolean properties?
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
	// Create our own private linker, always.
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
	check(Status == EMovieScenePlayerStatus::Playing || Status == EMovieScenePlayerStatus::Scrubbing);
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

void USequenceCameraShakeSequencePlayer::StartScrubbing()
{
	ensure(Status == EMovieScenePlayerStatus::Playing);
	Status = EMovieScenePlayerStatus::Scrubbing;
}

void USequenceCameraShakeSequencePlayer::EndScrubbing()
{
	ensure(Status == EMovieScenePlayerStatus::Scrubbing);
	Status = EMovieScenePlayerStatus::Playing;
}

