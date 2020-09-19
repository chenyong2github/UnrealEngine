// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraShakeBase.h"
#include "CameraAnimationSequence.h"
#include "EntitySystem/MovieSceneBoundSceneComponentInstantiator.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "TemplateSequencePlayer.h"
#include "UObject/WeakObjectPtr.h"
#include "SequenceCameraShake.generated.h"

class UMovieSceneEntitySystemLinker;
class UMovieSceneSequence;
class USequenceCameraShakeSequencePlayer;

/**
 * A dummy class that we give to a sequence in lieu of an actual camera actor.
 */
UCLASS()
class USequenceCameraShakeCameraStandIn : public UObject, public IMovieSceneSceneComponentImpersonator
{
public:

	GENERATED_BODY()

	USequenceCameraShakeCameraStandIn(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	FTransform GetTransform() const { return Transform; }
	void SetTransform(const FTransform& InTransform) { Transform = InTransform; }

	UPROPERTY()
	float FieldOfView;

private:

	FTransform Transform;
};

/**
 * A camera shake that plays a sequencer animation.
 */
UCLASS(Blueprintable)
class TEMPLATESEQUENCE_API USequenceCameraShake : public UCameraShakeBase
{
public:

	GENERATED_BODY()

	USequenceCameraShake(const FObjectInitializer& ObjInit);

public:

	/** Source camera animation sequence to play. */
	UPROPERTY(EditAnywhere, Category=CameraShake)
	class UCameraAnimationSequence* Sequence;

	/** Scalar defining how fast to play the anim. */
	UPROPERTY(EditAnywhere, Category=CameraShake, meta=(ClampMin="0.001"))
	float PlayRate;

	/** Scalar defining how "intense" to play the anim. */
	UPROPERTY(EditAnywhere, Category=CameraShake, meta=(ClampMin="0.0"))
	float Scale;

	/** Linear blend-in time. */
	UPROPERTY(EditAnywhere, Category=CameraShake, meta=(ClampMin="0.0"))
	float BlendInTime;

	/** Linear blend-out time. */
	UPROPERTY(EditAnywhere, Category=CameraShake, meta=(ClampMin="0.0"))
	float BlendOutTime;

	/** When bRandomSegment is true, defines how long the sequence should play. */
	UPROPERTY(EditAnywhere, Category=CameraShake, meta=(ClampMin="0.0", EditCondition="bRandomSegment"))
	float RandomSegmentDuration;

	/**
	 * When true, plays a random snippet of the sequence for RandomSegmentDuration seconds.
	 *
	 * @note The sequence we be forced to loop when bRandomSegment is enabled, in case the duration
	 *       is longer than what's left to play from the random start time.
	 */
	UPROPERTY(EditAnywhere, Category=CameraShake)
	bool bRandomSegment;

private:

	// UCameraShakeBase interface
	virtual void GetShakeInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakeImpl() override;
	virtual void UpdateShakeImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) override;
	virtual void StopShakeImpl(bool bImmediately) override;
	virtual void TeardownShakeImpl() override;

	static void RegisterCameraStandIn();

private:

	/** The player we use to play the camera animation sequence */
	UPROPERTY(transient)
	USequenceCameraShakeSequencePlayer* Player;

	/** Standin for the camera actor and components */
	UPROPERTY(transient)
	USequenceCameraShakeCameraStandIn* CameraStandIn;
};

/**
 * A spawn register that accepts a "wildcard" object.
 */
class FSequenceCameraShakeSpawnRegister : public FMovieSceneSpawnRegister
{
public:
	void SetSpawnedObject(UObject* InObject) { SpawnedObject = InObject; }

	virtual UObject* SpawnObject(FMovieSceneSpawnable&, FMovieSceneSequenceIDRef, IMovieScenePlayer&) override { return SpawnedObject.Get(); }
	virtual void DestroySpawnedObject(UObject&) override {}

#if WITH_EDITOR
	virtual bool CanSpawnObject(UClass* InClass) const override { return SpawnedObject.IsValid() && SpawnedObject.Get()->GetClass()->IsChildOf(InClass); }
#endif

private:
	FWeakObjectPtr SpawnedObject;
};

/**
 * A lightweight sequence player for playing camera animation sequences.
 */
UCLASS()
class USequenceCameraShakeSequencePlayer
	: public UObject
	, public IMovieScenePlayer
{
public:
	GENERATED_BODY()

	USequenceCameraShakeSequencePlayer(const FObjectInitializer& ObjInit);
	virtual ~USequenceCameraShakeSequencePlayer();

	/** Initializes this player with the given sequence */
	void Initialize(UMovieSceneSequence* InSequence);
	/** Start playing the sequence */
	void Play(bool bLoop = false, bool bRandomStartTime = false);
	/** Advance play to the given time */
	void Update(FFrameTime NewPosition);
	/** Jumps to the given time */
	void Jump(FFrameTime NewPosition);
	/** Stop playing the sequence */
	void Stop();

	/** Gets the current play position */
	FFrameTime GetCurrentPosition() const { return PlayPosition.GetCurrentPosition(); }
	/** Get the sequence tick resolution */
	FFrameRate GetInputRate() const { return PlayPosition.GetInputRate(); }

	/** Sets an object that can be used to bind everything in the sequence */
	void SetBoundObjectOverride(UObject* InObject);

public:

	// IMovieScenePlayer interface
	virtual FMovieSceneRootEvaluationTemplateInstance& GetEvaluationTemplate() override { return RootTemplateInstance; }
	virtual UMovieSceneEntitySystemLinker* ConstructEntitySystemLinker() override;
	virtual EMovieScenePlayerStatus::Type GetPlaybackStatus() const override;
	virtual UObject* AsUObject() override { return this; }
	virtual FMovieSceneSpawnRegister& GetSpawnRegister() override { return SpawnRegister; }

	virtual void SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus) override {}
	virtual void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) override {}
	virtual void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const override {}
	virtual bool CanUpdateCameraCut() const override { return false; }
	virtual void UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams) override {}
	virtual void ResolveBoundObjects(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, UMovieSceneSequence& InSequence, UObject* InResolutionContext, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;

	// UObject interface 
	virtual bool IsDestructionThreadSafe() const override { return false; }
	virtual void BeginDestroy() override;

private:

	FSequenceCameraShakeSpawnRegister SpawnRegister;

	/** Bound object overrides */
	UPROPERTY(transient)
	UObject* BoundObjectOverride;

	/** The sequence to play back */
	UPROPERTY(transient)
	UMovieSceneSequence* Sequence;

	/** The evaluation template instance */
	UPROPERTY(transient)
	FMovieSceneRootEvaluationTemplateInstance RootTemplateInstance;

	/** Play position helper */
	FMovieScenePlaybackPosition PlayPosition;

	/** Start frame for the sequence */
	FFrameNumber StartFrame;

	/** The sequence duration in frames */
	FFrameNumber DurationFrames;

	/** Whether we should be looping */
	bool bIsLooping;

	/** Movie player status. */
	TEnumAsByte<EMovieScenePlayerStatus::Type> Status;
};
