// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraShakeBase.h"
#include "CameraShake.generated.h"

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
struct ENGINE_API FFOscillator
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
UCLASS(Blueprintable)
class ENGINE_API UMatineeCameraShake : public UCameraShakeBase
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

	/** Returns true if this camera shake will loop forever */
	bool IsLooping() const;

	/** Sets current playback time and applies the shake (both oscillation and cameraanim) to the given POV. */
	UE_DEPRECATED(4.27, "SetCurrentTimeAndApplyShake is deprecated, please use ScrubAndApplyCameraShake")
	void SetCurrentTimeAndApplyShake(float NewTime, FMinimalViewInfo& POV);
	
	/** Sets actor for playing camera anims */
	void SetTempCameraAnimActor(AActor* Actor) { TempCameraActorForCameraAnims = Actor; }

private:

	void DoStartShake();
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
	AActor* TempCameraActorForCameraAnims;

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
UCLASS()
class ENGINE_API UMatineeCameraShakePattern : public UCameraShakePattern
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
