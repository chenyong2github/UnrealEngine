// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Curves/CurveFloat.h"
#include "ForceFeedbackEffect.generated.h"

class UForceFeedbackEffect;
struct FForceFeedbackValues;
class UInputDeviceProperty;

USTRUCT()
struct FForceFeedbackChannelDetails
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category="ChannelDetails")
	uint32 bAffectsLeftLarge:1;

	UPROPERTY(EditAnywhere, Category="ChannelDetails")
	uint32 bAffectsLeftSmall:1;

	UPROPERTY(EditAnywhere, Category="ChannelDetails")
	uint32 bAffectsRightLarge:1;

	UPROPERTY(EditAnywhere, Category="ChannelDetails")
	uint32 bAffectsRightSmall:1;

	UPROPERTY(EditAnywhere, Category="ChannelDetails")
	FRuntimeFloatCurve Curve;

	FForceFeedbackChannelDetails()
		: bAffectsLeftLarge(true)
		, bAffectsLeftSmall(true)
		, bAffectsRightLarge(true)
		, bAffectsRightSmall(true)
	{
	}
};

/** This structure is used to pass arguments to ClientPlayForceFeedback() client RPC function */
USTRUCT()
struct FForceFeedbackParameters
{
	GENERATED_BODY()

		FForceFeedbackParameters()
		: bLooping(false)
		, bIgnoreTimeDilation(false)
		, bPlayWhilePaused(false)
	{}

	UPROPERTY()
	FName Tag;

	UPROPERTY()
	bool bLooping;

	UPROPERTY()
	bool bIgnoreTimeDilation;

	UPROPERTY()
	bool bPlayWhilePaused;
};

USTRUCT()
struct ENGINE_API FActiveForceFeedbackEffect
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TObjectPtr<class UForceFeedbackEffect> ForceFeedbackEffect;

	FForceFeedbackParameters Parameters;
	float PlayTime;

	/** The platform user that should receive this effect */
	FPlatformUserId PlatformUser = PLATFORMUSERID_NONE;

	FActiveForceFeedbackEffect()
		: ForceFeedbackEffect(nullptr)
		, PlayTime(0.f)
		, PlatformUser(PLATFORMUSERID_NONE)
	{
	}

	FActiveForceFeedbackEffect(UForceFeedbackEffect* InEffect, FForceFeedbackParameters InParameters, FPlatformUserId InPlatformUser)
		: ForceFeedbackEffect(InEffect)
		, Parameters(InParameters)
		, PlayTime(0.f)
		, PlatformUser(InPlatformUser)
	{
	}

	// Updates the final force feedback values based on this effect.  Returns true if the effect should continue playing, false if it is finished.
	bool Update(float DeltaTime, FForceFeedbackValues& Values);

	/** Reset any device properties that may need to be after the duration of this effect has ended. */
	void ResetDeviceProperties();

	// Gets the current values at the stored play time
	void GetValues(FForceFeedbackValues& Values) const;
};

/**
 * A predefined force-feedback effect to be played on a controller
 */
UCLASS(BlueprintType, MinimalAPI)
class UForceFeedbackEffect : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category="ForceFeedbackEffect")
	TArray<FForceFeedbackChannelDetails> ChannelDetails;

	/** A map of input device properties that we want to set while this effect is playing */
	UPROPERTY(EditAnywhere, Instanced, Category = "ForceFeedbackEffect")
	TArray<TObjectPtr<UInputDeviceProperty>> DeviceProperties;

	/** Duration of force feedback pattern in seconds. */
	UPROPERTY(Category=Info, AssetRegistrySearchable, VisibleAnywhere, BlueprintReadOnly)
	float Duration;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty( struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	float GetDuration();

	void GetValues(const float EvalTime, FForceFeedbackValues& Values, float ValueMultiplier = 1.f) const;

	void SetDeviceProperties(const FPlatformUserId PlatformUser, const float DeltaTime, const float EvalTime);

	/** Reset any device properties that may need to be after the duration of this effect has ended. */
	void ResetDeviceProperties(const FPlatformUserId PlatformUser);
};
