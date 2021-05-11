// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "Evaluation/MovieSceneCameraShakeTemplate.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "MatineeCameraShake.generated.h"

class AActor;

/************************************************************
 * Parameters for defining oscillating camera shakes
 ************************************************************/

/** Types of waveforms that can be used for camera shake oscillators */
UENUM(BlueprintType)
enum class EOscillatorWaveform : uint8
{
	/** A sinusoidal wave */
	SineWave,

	/** Perlin noise */
	PerlinNoise,
};

/** Shake start offset parameter */
UENUM()
enum EInitialOscillatorOffset
{
	/** Start with random offset (default). */
	EOO_OffsetRandom UMETA(DisplayName = "Random"),
	/** Start with zero offset. */
	EOO_OffsetZero UMETA(DisplayName = "Zero"),
	EOO_MAX,
};

/** Defines oscillation of a single number. */
USTRUCT(BlueprintType)
struct GAMEPLAYCAMERAS_API FFOscillator
{
	GENERATED_USTRUCT_BODY()

	/** Amplitude of the sinusoidal oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FOscillator)
	float Amplitude;

	/** Frequency of the sinusoidal oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FOscillator)
	float Frequency;

	/** Defines how to begin (either at zero, or at a randomized value. */
	UPROPERTY(EditAnywhere, Category=FOscillator)
	TEnumAsByte<enum EInitialOscillatorOffset> InitialOffset;
	
	/** Type of waveform to use for oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FOscillator)
	EOscillatorWaveform Waveform;

	FFOscillator()
		: Amplitude(0)
		, Frequency(0)
		, InitialOffset(0)
		, Waveform( EOscillatorWaveform::SineWave ) 
	{}

	/** Advances the oscillation time and returns the current value. */
	static float UpdateOffset(FFOscillator const& Osc, float& CurrentOffset, float DeltaTime);

	/** Returns the initial value of the oscillator. */
	static float GetInitialOffset(FFOscillator const& Osc);

	/** Returns the offset at the given time */
	static float GetOffsetAtTime(FFOscillator const& Osc, float InitialOffset, float Time);
};

/** Defines FRotator oscillation. */
USTRUCT(BlueprintType)
struct FROscillator
{
	GENERATED_USTRUCT_BODY()

	/** Pitch oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ROscillator)
	struct FFOscillator Pitch;

	/** Yaw oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ROscillator)
	struct FFOscillator Yaw;

	/** Roll oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ROscillator)
	struct FFOscillator Roll;

};

/** Defines FVector oscillation. */
USTRUCT(BlueprintType)
struct FVOscillator
{
	GENERATED_USTRUCT_BODY()

	/** Oscillation in the X axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = VOscillator)
	struct FFOscillator X;

	/** Oscillation in the Y axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = VOscillator)
	struct FFOscillator Y;

	/** Oscillation in the Z axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = VOscillator)
	struct FFOscillator Z;
};


/**
 * Legacy camera shake which can do either oscillation or run camera anims.
 */
UCLASS(Blueprintable, HideCategories=(CameraShakePattern))
class GAMEPLAYCAMERAS_API UMatineeCameraShake : public UCameraShakeBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Duration in seconds of current screen shake. Less than 0 means indefinite, 0 means no oscillation. */
	UPROPERTY(EditAnywhere, Category=Oscillation)
	float OscillationDuration;

	/** Duration of the blend-in, where the oscillation scales from 0 to 1. */
	UPROPERTY(EditAnywhere, Category=Oscillation, meta=(ClampMin = "0.0"))
	float OscillationBlendInTime;

	/** Duration of the blend-out, where the oscillation scales from 1 to 0. */
	UPROPERTY(EditAnywhere, Category=Oscillation, meta = (ClampMin = "0.0"))
	float OscillationBlendOutTime;

	/** Rotational oscillation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Oscillation)
	struct FROscillator RotOscillation;

	/** Positional oscillation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Oscillation)
	struct FVOscillator LocOscillation;

	/** FOV oscillation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Oscillation)
	struct FFOscillator FOVOscillation;

	/************************************************************
	 * Parameters for defining CameraAnim-driven camera shakes
	 ************************************************************/

	/** Scalar defining how fast to play the anim. */
	UPROPERTY(EditAnywhere, Category=AnimShake, meta=(ClampMin = "0.001"))
	float AnimPlayRate;

	/** Scalar defining how "intense" to play the anim. */
	UPROPERTY(EditAnywhere, Category=AnimShake, meta=(ClampMin = "0.0"))
	float AnimScale;

	/** Linear blend-in time. */
	UPROPERTY(EditAnywhere, Category=AnimShake, meta=(ClampMin = "0.0"))
	float AnimBlendInTime;

	/** Linear blend-out time. */
	UPROPERTY(EditAnywhere, Category=AnimShake, meta=(ClampMin = "0.0"))
	float AnimBlendOutTime;

	/** When bRandomAnimSegment is true, this defines how long the anim should play. */
	UPROPERTY(EditAnywhere, Category = AnimShake, meta = (ClampMin = "0.0", editcondition = "bRandomAnimSegment"))
	float RandomAnimSegmentDuration;

	/** Source camera animation to play. Can be null. */
	UPROPERTY(EditAnywhere, Category = AnimShake)
	class UCameraAnim* Anim;

	/** Source camera animation sequence to play. Can be null, but can't have both Anim and AnimSequence. */
	UPROPERTY(EditAnywhere, Category = AnimShake)
	class UCameraAnimationSequence* AnimSequence;

	/**
	* If true, play a random snippet of the animation of length Duration.  Implies bLoop and bRandomStartTime = true for the CameraAnim.
	* If false, play the full anim once, non-looped. Useful for getting variety out of a single looped CameraAnim asset.
	*/
	UPROPERTY(EditAnywhere, Category = AnimShake)
	uint32 bRandomAnimSegment : 1;

public:

	/** Time remaining for oscillation shakes. Less than 0.f means shake infinitely. */
	UPROPERTY(transient, BlueprintReadOnly, Category = CameraShake)
	float OscillatorTimeRemaining;

	/** The playing instance of the CameraAnim-based shake, if any. */
	UPROPERTY(transient, BlueprintReadOnly, Category = CameraShake)
	class UCameraAnimInst* AnimInst;

public:

	// Blueprint API

	/** Called when the shake starts playing */
	UFUNCTION(BlueprintImplementableEvent, Category = CameraShake)
	void ReceivePlayShake(float Scale);

	/** Called every tick to let the shake modify the point of view */
	UFUNCTION(BlueprintImplementableEvent, Category = CameraShake)
	void BlueprintUpdateCameraShake(float DeltaTime, float Alpha, const FMinimalViewInfo& POV, FMinimalViewInfo& ModifiedPOV);

	/** Called to allow a shake to decide when it's finished playing. */
	UFUNCTION(BlueprintNativeEvent, Category = CameraShake)
	bool ReceiveIsFinished() const;

	/**
	 * Called when the shake is explicitly stopped.
	 * @param bImmediatly		If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = CameraShake)
	void ReceiveStopShake(bool bImmediately);

public:

	/**
	 * Backwards compatible method used by core BP redirectors. This is needed because the return value is specifically a Matinee camera shake,
	 * which some BP logic often uses directly to set oscillator/anim properties.
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera Shakes")
	static UMatineeCameraShake* StartMatineeCameraShake(APlayerCameraManager* PlayerCameraManager, TSubclassOf<UMatineeCameraShake> ShakeClass, float Scale = 1.f, ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal, FRotator UserPlaySpaceRot = FRotator::ZeroRotator);

	/**
	 * Backwards compatible method used by core BP redirectors. This is needed because the return value is specifically a Matinee camera shake,
	 * which some BP logic often uses directly to set oscillator/anim properties.
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera Shakes")
	static UMatineeCameraShake* StartMatineeCameraShakeFromSource(APlayerCameraManager* PlayerCameraManager, TSubclassOf<UMatineeCameraShake> ShakeClass, UCameraShakeSourceComponent* SourceComponent, float Scale = 1.f, ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal, FRotator UserPlaySpaceRot = FRotator::ZeroRotator);

public:

	/** Returns true if this camera shake will loop forever */
	bool IsLooping() const;

	/** Sets current playback time and applies the shake (both oscillation and cameraanim) to the given POV. */
	UE_DEPRECATED(4.27, "SetCurrentTimeAndApplyShake is deprecated, please use ScrubAndApplyCameraShake")
	void SetCurrentTimeAndApplyShake(float NewTime, FMinimalViewInfo& POV);
	
	/** Sets actor for playing camera anims */
	void SetTempCameraAnimActor(AActor* Actor) { TempCameraActorForCameraAnims = Actor; }

private:

	void DoStartShake(const FCameraShakeStartParams& Params);
	void DoUpdateShake(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult);
	void DoScrubShake(const FCameraShakeScrubParams& Params, FCameraShakeUpdateResult& OutResult);
	void DoStopShake(bool bImmediately);
	bool DoGetIsFinished() const;

protected:

	/** Current location sinusoidal offset. */
	FVector LocSinOffset;

	/** Current rotational sinusoidal offset. */
	FVector RotSinOffset;

	/** Current FOV sinusoidal offset. */
	float FOVSinOffset;

	/** Initial offset (could have been assigned at random). */
	FVector InitialLocSinOffset;

	/** Initial offset (could have been assigned at random). */
	FVector InitialRotSinOffset;

	/** Initial offset (could have been assigned at random). */
	float InitialFOVSinOffset;

	/** Temp actor to use for playing camera anims. Used when playing a camera anim in non-gameplay context, e.g. in the editor */
	TWeakObjectPtr<AActor> TempCameraActorForCameraAnims;

	/** Sequence shake pattern for when using a sequence instead of a camera anim */
	UPROPERTY(Instanced)
	class USequenceCameraShakePattern* SequenceShakePattern;

	/** State tracking for the sequence shake pattern */
	FCameraShakeState SequenceShakeState;

private:

	float CurrentBlendInTime;
	float CurrentBlendOutTime;
	bool bBlendingIn : 1;
	bool bBlendingOut : 1;

	friend class UMatineeCameraShakePattern;
};

/**
 * Shake pattern for the UMatineeCameraShake class.
 *
 * It doesn't do anything because, for backwards compatibility reasons, all the data
 * was left on the shake class itself... so this pattern delegates everything back
 * to the owner shake.
 */
UCLASS(HideDropdown)
class GAMEPLAYCAMERAS_API UMatineeCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

private:

	// UCameraShakePattern interface
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakeStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(const FCameraShakeScrubParams& Params, FCameraShakeUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;
	virtual void StopShakePatternImpl(const FCameraShakeStopParams& Params) override;
};

/** Backwards compatible name for the Matinee camera shake, for C++ code. */
UE_DEPRECATED(4.26, "Please use UMatineeCameraShake")
typedef UMatineeCameraShake UCameraShake;

/** Custom sequencer evaluation code for Matinee camera shakes */
UCLASS()
class UMovieSceneMatineeCameraShakeEvaluator : public UMovieSceneCameraShakeEvaluator
{
	GENERATED_BODY()

	UMovieSceneMatineeCameraShakeEvaluator(const FObjectInitializer& ObjInit);

	static UMovieSceneCameraShakeEvaluator* BuildMatineeShakeEvaluator(UCameraShakeBase* ShakeInstance);

	virtual bool Setup(const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, UCameraShakeBase* ShakeInstance) override;
	virtual bool Evaluate(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, UCameraShakeBase* ShakeInstance) override;
};

/**
 * Blueprint function library for autocasting from a base camera shake to a matinee camera shake.
 * This prevents breaking Blueprints now that APlayerCameraManager::StartCameraShake returns the base class.
 */
UCLASS()
class GAMEPLAYCAMERAS_API UMatineeCameraShakeFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="Camera Shakes", meta=(BlueprintAutocast))
	static UMatineeCameraShake* Conv_MatineeCameraShake(UCameraShakeBase* CameraShake)
	{
		return CastChecked<UMatineeCameraShake>(CameraShake, ECastCheckedType::NullAllowed);
	}
};

