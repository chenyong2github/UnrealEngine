// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ScriptMacros.h"
#include "Camera/CameraTypes.h"
#include "CameraShakeBase.generated.h"

class APlayerCameraManager;

/**
 * Parameters for updating a camera shake.
 */
USTRUCT(BlueprintType)
struct ENGINE_API FCameraShakeUpdateParams
{
	GENERATED_BODY()

	FCameraShakeUpdateParams()
	{}

	FCameraShakeUpdateParams(const FMinimalViewInfo& InPOV)
		: POV(InPOV)
	{}

	/** The time elapsed since last update */
	float DeltaTime = 0.f;
	/** The dynamic scale being passed down from the camera manger for this shake */
	float DynamicScale = 1.f;
	/** The auto-computed blend in/out scale, when blending is handled by base class (see UCameraShakeBase::GetShakeInfo) */
	float BlendingWeight = 1.f;
	/** The total scale to apply to the camera shake during the current update. Equals ShakeScale * DynamicScale * BlendingWeight */
	float TotalScale = 1.f;
	/** The current view that this camera shake should modify */
	FMinimalViewInfo POV;
};

/**
 * Flags that camera shakes can return to change base-class behaviour.
 */
UENUM()
enum class ECameraShakeUpdateResultFlags : uint8
{
	/** Apply the result location, rotation, and field of view as absolute values, instead of additive values. */
	ApplyAsAbsolute = 1 << 0,
	/** Do not apply scaling (dynamic scale, blending weight, shake scale), meaning that this will be done in the sub-class. Implied when ApplyAsAbsolute is set. */
	SkipAutoScale = 1 << 1,
	/** Do not re-orient the result based on the play-space. Implied when ApplyAsAbsolute is set. */
	SkipAutoPlaySpace = 1 << 2,

	/** Default flags: the sub-class is returning local, additive offsets, and lets the base class take care of the rest. */
	Default = 0
};
ENUM_CLASS_FLAGS(ECameraShakeUpdateResultFlags);

/**
 * The result of a camera shake update.
 */
USTRUCT(BlueprintType)
struct ENGINE_API FCameraShakeUpdateResult
{
	GENERATED_BODY()

	FCameraShakeUpdateResult()
		: Location(FVector::ZeroVector)
		, Rotation(FRotator::ZeroRotator)
		, FOV(0.f)
		, Flags(ECameraShakeUpdateResultFlags::Default)
	{}

	/** Location offset for the view, or new absolute location if ApplyAsAbsolute flag is set */
	FVector Location;
	/** Rotation offset for the view, or new absolute rotation if ApplyAsAbsolute flag is set */
	FRotator Rotation;
	/** Field-of-view offset for the view, or new absolute field-of-view if ApplyAsAbsolute flag is set */
	float FOV;

	/** Flags for how the base class should handle the result */
	ECameraShakeUpdateResultFlags Flags;
};

/**
 * Camera shake duration type.
 */
UENUM()
enum class ECameraShakeDurationType : uint8
{
	/** Camera shake has a fixed duration */
	Fixed,
	/** Camera shake is playing indefinitely, until explicitly stopped */
	Infinite,
	/** Camera shake has custom/dynamic duration */
	Custom
};

/**
 * Camera shake duration.
 */
USTRUCT(BlueprintType)
struct ENGINE_API FCameraShakeDuration
{
	GENERATED_BODY()

	static FCameraShakeDuration Infinite() { return FCameraShakeDuration { 0.f, ECameraShakeDurationType::Infinite }; }
	static FCameraShakeDuration Custom() { return FCameraShakeDuration { 0.f, ECameraShakeDurationType::Custom }; }

	FCameraShakeDuration() : Duration(0.f), Type(ECameraShakeDurationType::Fixed) {}
	FCameraShakeDuration(float InDuration, ECameraShakeDurationType InType = ECameraShakeDurationType::Fixed) : Duration(InDuration), Type(InType) {}
	
	bool IsFixed() const { return Type == ECameraShakeDurationType::Fixed; }
	bool IsInfinite() const { return Type == ECameraShakeDurationType::Infinite; }
	bool IsCustom() const { return Type == ECameraShakeDurationType::Custom; }

	float Get() const { check(Type == ECameraShakeDurationType::Fixed); return Duration; }

private:
	UPROPERTY()
	float Duration;

	UPROPERTY()
	ECameraShakeDurationType Type;
};

/**
 * Information about a camera shake class.
 */
USTRUCT(BlueprintType)
struct ENGINE_API FCameraShakeInfo
{
	GENERATED_BODY()

	/** The duration of the camera shake */
	UPROPERTY()
	FCameraShakeDuration Duration;

	/** How much blending-in the camera shake should have */
	UPROPERTY()
	float BlendIn = 0.f;

	/** How much blending-out the camera shake should have */
	UPROPERTY()
	float BlendOut = 0.f;
};

/**
 * A CameraShake is an asset that defines how to shake the camera in 
 * a particular way. CameraShakes can be authored as either oscillating shakes, 
 * animated shakes, or both.
 *
 * An oscillating shake will sinusoidally vibrate various camera parameters over time. Each location
 * and rotation axis can be oscillated independently with different parameters to create complex and
 * random-feeling shakes. These are easier to author and tweak, but can still feel mechanical and are
 * limited to vibration-style shakes, such as earthquakes.
 *
 * Animated shakes play keyframed camera animations.  These can take more effort to author, but enable
 * more natural-feeling results and things like directional shakes.  For instance, you can have an explosion
 * to the camera's right push it primarily to the left.
 */
UCLASS(abstract, Blueprintable, EditInlineNew)
class ENGINE_API UCameraShakeBase : public UObject
{
	GENERATED_BODY()

public:

	UCameraShakeBase(const FObjectInitializer& ObjectInitializer);

	/**
	 * Gets the duration of this camera shake in seconds.
	 *
	 * The value could be 0 or negative if the shake uses the oscillator, meaning, respectively,
	 * no oscillation, or indefinite oscillation.
	 */
	FCameraShakeDuration GetCameraShakeDuration() const;

	/**
	 * Gets the duration of this camera shake's blend in and out.
	 *
	 * The values could be 0 or negative if there's no blend in and/or out.
	 */
	void GetCameraShakeBlendTimes(float& OutBlendIn, float& OutBlendOut) const;

	/**
	 * Gets the default duration for camera shakes of the given class.
	 *
	 * @param CameraShakeClass    The class of camera shake
	 * @param OutDuration         Will store the default duration of the given camera shake class, if possible
	 * @return                    Whether a valid default duration was found
	 */
	static bool GetCameraShakeDuration(TSubclassOf<UCameraShakeBase> CameraShakeClass, FCameraShakeDuration& OutDuration)
	{
		if (CameraShakeClass)
		{
			if (const UCameraShakeBase* CDO = CameraShakeClass->GetDefaultObject<UCameraShakeBase>())
			{
				OutDuration = CDO->GetCameraShakeDuration();
				return true;
			}
		}
		return false;
	}

	/**
	 * Gets the default blend in/out durations for camera shakes of the given class.
	 *
	 * @param CameraShakeClass    The class of camera shake
	 * @param OutBlendIn          Will store the default blend-in time of the given camera shake class, if possible
	 * @param OutBlendOut         Will store the default blend-out time of the given camera shake class, if possible
	 * @return                    Whether valid default blend in/out times were found
	 */
	static bool GetCameraShakeBlendTimes(TSubclassOf<UCameraShakeBase> CameraShakeClass, float& OutBlendIn, float& OutBlendOut)
	{
		if (CameraShakeClass)
		{
			if (const UCameraShakeBase* CDO = CameraShakeClass->GetDefaultObject<UCameraShakeBase>())
			{
				CDO->GetCameraShakeBlendTimes(OutBlendIn, OutBlendOut);
				return true;
			}
		}
		return false;
	}

public:
	/** 
	 *  If true to only allow a single instance of this shake class to play at any given time.
	 *  Subsequent attempts to play this shake will simply restart the timer.
	 */
	UPROPERTY(EditAnywhere, Category = CameraShake)
	bool bSingleInstance;

	/** The overall scale to apply to the shake. Only valid when the shake is active. */
	UPROPERTY(transient, BlueprintReadWrite, Category = CameraShake)
	float ShakeScale;

public:

	/** Gets some infromation about this specific camera shake */
	void GetShakeInfo(FCameraShakeInfo& OutInfo) const;

	/** Starts this camera shake with the given parameters */
	void StartShake(APlayerCameraManager* Camera, float Scale, ECameraShakePlaySpace InPlaySpace, FRotator UserPlaySpaceRot = FRotator::ZeroRotator);

	/** Returns whether this camera shake is finished */
	bool IsFinished() const;

	/** Updates this camera shake and applies its effect to the given view */
	void UpdateAndApplyCameraShake(float DeltaTime, float Alpha, FMinimalViewInfo& InOutPOV);

	/** Stops this camera shake */
	void StopShake(bool bImmediately = true);

	/** Tears down this camera shake before destruction or recycling */
	void TeardownShake();

protected:

	void ApplyPlaySpace(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& InOutResult) const;
	void ApplyScale(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& InOutResult) const;
	void ApplyScale(float Scale, FCameraShakeUpdateResult& InOutResult) const;

	/** Gets the current camera manager. Will be null if the shake isn't active. */
	APlayerCameraManager* GetCameraManager() const { return CameraManager; }

	/** Returns the current play space. The value is irrelevant if the shake isn't active. */
	ECameraShakePlaySpace GetPlaySpace() const { return PlaySpace; }
	/** Returns the current play space matrix. The value is irrelevant if the shake isn't active, or if its play space isn't UserDefined. */
	const FMatrix& GetUserPlaySpaceMatrix() const { return UserPlaySpaceMatrix; }
	/** Sets the current play space matrix. This method has no effect if the shake isn't active, or if its play space isn't UserDefined. */
	void SetUserPlaySpaceMatrix(const FMatrix& InMatrix) { UserPlaySpaceMatrix = InMatrix; }

private:

	// UCameraShakeBase interface
	virtual void GetShakeInfoImpl(FCameraShakeInfo& OutInfo) const {}
	virtual void StartShakeImpl() {}
	virtual void UpdateShakeImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult) {}
	virtual bool IsFinishedImpl() const { return true; }
	virtual void StopShakeImpl(bool bImmediately) {}
	virtual void TeardownShakeImpl()  {}

private:

	/** The camera manager owning this camera shake. Only valid when the shake is active. */
	UPROPERTY(transient)
	APlayerCameraManager* CameraManager;

	/** What space to play the shake in before applying to the camera. Only valid when the shake is active. */
	ECameraShakePlaySpace PlaySpace;

	/** Matrix defining a custom play space, used when PlaySpace is UserDefined. Only valid when the shake is active. */
	FMatrix UserPlaySpaceMatrix;

	/** Information about our shake's specific implementation. Only valid when the shake is active. */
	FCameraShakeInfo ActiveInfo;

	/** Transitive state of the shake. Only valid when the shake is active. */
	struct FCameraShakeState
	{
		FCameraShakeState() : 
			ElapsedTime(0.f)
			, bIsActive(false)
			, bHasDuration(false)
			, bHasBlendIn(false)
			, bHasBlendOut(false)
		{}

		float ElapsedTime;
		bool bIsActive : 1;
		bool bHasDuration : 1;
		bool bHasBlendIn : 1;
		bool bHasBlendOut : 1;
	};
	FCameraShakeState State;
};
