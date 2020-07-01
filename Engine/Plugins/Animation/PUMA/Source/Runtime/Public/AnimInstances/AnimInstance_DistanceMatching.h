// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/CachedAnimData.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "AnimInstance_DistanceMatching.generated.h"

UCLASS(transient, Blueprintable, hideCategories=AnimInstance, BlueprintType, meta=(BlueprintThreadSafe), Within=SkeletalMeshComponent)
class PUMARUNTIME_API UAnimInstance_DistanceMatching : public UAnimInstance
{
	GENERATED_UCLASS_BODY()

protected:
	// Begin UAnimInstance interface
	virtual void NativeBeginPlay() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;
	// End UAnimInstance interface

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	FVector CharacterVelocity{ FVector::ZeroVector };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	float CharacterSpeed{ 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	float CharacterSpeed2D{ 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	float CharacterSpeedZ{ 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Locomotion, Meta = (DisplayName = "Min Character Speed Mag Threshold"))
	float MinCharacterSpeedThreshold{ 25.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	FVector CharacterAcceleration{ FVector::ZeroVector };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	float CharacterAccelerationMag{ 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	float CharacterAccelerationMag2D{ 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = Locomotion)
	float CharacterAccelerationMagZ{ 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Locomotion, Meta = (DisplayName = "Min Character Acceleration Mag Threshold"))
	float MinCharacterAccelerationMagThreshold{ 25.f };

	UPROPERTY(BlueprintReadOnly, Transient, Category = Locomotion)
	float MaxCharacterSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Move", Meta = (DisplayName = "Walk Speed Ideal"))
	float WalkSpeedIdeal{ 150.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Move", Meta = (DisplayName = "Walk Speed Max"))
	float WalkSpeedMax{ 200.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Move")
	FCachedAnimStateData WalkStateData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Move", Meta = (DisplayName = "Jog Speed Ideal"))
	float JogSpeedIdeal{ 400.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Move", Meta = (DisplayName = "Jog Speed Max"))
	float JogSpeedMax{ 500.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Move")
	FCachedAnimStateData JogStateData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Move", Meta = (DisplayName = "Sprint Speed Ideal"))
	float SprintSpeedIdeal{ 600.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Move", Meta = (DisplayName = "Sprint Speed Max"))
	float SprintSpeedMax{ 750.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Move")
	FCachedAnimStateData SprintStateData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|DistanceCurve")
	FDistanceCurve DistanceCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Start")
	FCachedAnimStateData StartStateData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Start")
	FCachedAnimTransitionData StartTransitionData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Start")
	UAnimSequence* WalkStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Start")
	UAnimSequence* JogStart;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Start")
	UAnimSequence* StartAnimation;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Start")
	uint8 bStartTransitionTriggered : 1;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Start")
	uint8 bStartEarlyOut : 1;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Start")
	float StartAnimPosition;

	UPROPERTY(Transient)
	float StartAnimDistanceTraveled;

	UPROPERTY(Transient)
	float StartActualDistanceFromMarker;

	UPROPERTY(Transient)
	float StartAnimTimeElapsed;

	UPROPERTY(Transient)
	FVector StartAnimLocation;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Locomotion|Start")
	bool bStartComplete;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Locomotion|Start")
	bool bPlayStart;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Locomotion|Start")
	bool bWalkToStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Stop")
	FCachedAnimStateData StopStateData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Stop")
	FCachedAnimTransitionData StopTransitionData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Stop")
	UAnimSequence* WalkStop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Stop")
	UAnimSequence* JogStop;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Stop")
	UAnimSequence* StopAnimation;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Stop")
	uint8 bStopTransitionTriggered : 1;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Stop")
	uint8 bStopEarlyOut : 1;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Stop")
	float StopAnimPosition;

	UPROPERTY(Transient)
	float StopAnimDistanceTraveled;

	UPROPERTY(Transient)
	float StopActualDistanceFromMarker;

	UPROPERTY(Transient)
	float StopAnimTimeElapsed;

	UPROPERTY(Transient)
	FVector StopAnimLocation;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Locomotion|Stop")
	bool bStopArrived;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Locomotion|Stop")
	bool bStopComplete;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|PlayRateAndStrideWarping")
	FCachedFloatCurve PlayRateStrideWarpAlphaCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|PlayRateAndStrideWarping")
	float PlayRateStrideWarpAlpha{ 0.5f };

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|PlayRateAndStrideWarping")
	float PlayRateValue;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|PlayRateAndStrideWarping")
	float StrideWarpingValue;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Slope")
	float SlopeAngle;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Slope")
	float SlopeWarpingAlpha;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Locomotion|Slope")
	float SlopeWarpingAdjustmentAlpha;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Slope")
	float MaxCharacterSpeedForSlopeWarpingDamping{ 275.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Slope", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MinSlopeWarpingAlpha{ 0.5f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Slope", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MaxSlopeWarpingAlpha{ 1.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Slope")
	float MaxCharacterSpeedForSlopeAngleAdjustmentRange{ 400.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Slope", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MinSlopeAngleAdjustmentSpeed{ 45.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Slope", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MaxSlopeAngleAdjustmentSpeed{ 90.f };

private:

	void UpdateCharacterLocomotionProperties(const ACharacter& CharacterOwner, const UCharacterMovementComponent& CharacterMovement, float DeltaSeconds);

	// Distance-matched transitions
	void UpdateStartTransition(const ACharacter& CharacterOwner, const UCharacterMovementComponent& CharacterMovement, float DeltaSeconds);
	void UpdateStopTransition(const ACharacter& CharacterOwner, const UCharacterMovementComponent& CharacterMovement, float DeltaSeconds);

	void CalculateStrideWarpingValues(const float& InRateWarpCurveAlpha, const float& InOverallRate);

	FTransform IKFootRootTransformLastFrame;
};
