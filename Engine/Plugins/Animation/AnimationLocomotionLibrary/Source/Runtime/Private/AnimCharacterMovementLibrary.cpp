// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimCharacterMovementLibrary.h"
#include "Animation/AnimSequenceBase.h"
#include "KismetAnimationLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimCharacterMovementLibrary, Verbose, All);

void UAnimCharacterMovementLibrary::UpdateCharacterMovementSnapshot(const FTransform& WorldTransform, const FVector& WorldVelocity, const FVector& WorldAcceleration, bool bIsOnGround,
	float RootYawOffset, UPARAM(ref) FAnimCharacterMovementSnapshot& Snapshot)
{
	// Position

	const FVector WorldLocation = WorldTransform.GetLocation();
	Snapshot.Distance2DTraveledSinceLastUpdate = FVector::Dist2D(Snapshot.WorldLocation, WorldLocation);
	Snapshot.WorldLocation = WorldLocation;

	// Velocity

	Snapshot.WorldVelocity = WorldVelocity;
	Snapshot.LocalVelocity = WorldTransform.InverseTransformVectorNoScale(Snapshot.WorldVelocity);
	Snapshot.Speed2D = Snapshot.WorldVelocity.Size2D();

	// Acceleration

	Snapshot.WorldAcceleration = WorldAcceleration;
	Snapshot.LocalAcceleration = WorldTransform.InverseTransformVectorNoScale(Snapshot.WorldAcceleration);
	Snapshot.AccelerationSize2D = Snapshot.WorldAcceleration.Size2D();

	// Movement angle

	const FRotator Rotation = WorldTransform.GetRotation().Rotator();

	if (FMath::IsNearlyZero(Snapshot.Speed2D))
	{
		Snapshot.VelocityYawAngle = 0.0f;
		Snapshot.AccelerationYawAngle = 0.0f;
	}
	else
	{
		Snapshot.VelocityYawAngle = UKismetAnimationLibrary::CalculateDirection(Snapshot.WorldVelocity, Rotation);
		Snapshot.VelocityYawAngle = FRotator::NormalizeAxis(Snapshot.VelocityYawAngle - RootYawOffset);

		Snapshot.AccelerationYawAngle = UKismetAnimationLibrary::CalculateDirection(Snapshot.WorldAcceleration, Rotation);
		Snapshot.AccelerationYawAngle = FRotator::NormalizeAxis(Snapshot.AccelerationYawAngle - RootYawOffset);
	}

	// Movement state

	Snapshot.bIsOnGround = bIsOnGround;
	
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	FString DebugString = FString::Printf(TEXT(
		"WorldVelocity(%s) | LocalVelocity(%s) | Speed2D(%.3f) | Distance2DTraveledSinceLastUpdate(%.3f)\n"
		"WorldAcceleration(%s) | LocalAcceleration(%s) | AccelerationSize2D(%.3f)\n"
		"VelocityYawAngle(%.3f) | AccelerationYawAngle(%.3f)\n"
		"bIsOnGround(%d)\n"),
		*Snapshot.WorldVelocity.ToString(), *Snapshot.LocalVelocity.ToString(), Snapshot.Speed2D, Snapshot.Distance2DTraveledSinceLastUpdate,
		*Snapshot.WorldAcceleration.ToString(), *Snapshot.LocalAcceleration.ToString(), Snapshot.AccelerationSize2D,
		Snapshot.VelocityYawAngle, Snapshot.AccelerationYawAngle,
		Snapshot.bIsOnGround);

	UE_LOG(LogAnimCharacterMovementLibrary, VeryVerbose, TEXT("%s"), *DebugString);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

EAnimCardinalDirection UAnimCharacterMovementLibrary::GetCardinalDirectionFromAngle(EAnimCardinalDirection PreviousCardinalDirection, float AngleInDegrees, float DeadZoneAngle)
{
	// Use deadzone offset to favor backpedal on S & W, and favor frontpedal on N & E
	const float AbsoluteAngle = FMath::Abs(AngleInDegrees);
	if (PreviousCardinalDirection == EAnimCardinalDirection::North)
	{
		if (AbsoluteAngle <= (45.f + DeadZoneAngle + DeadZoneAngle))
		{
			return EAnimCardinalDirection::North;
		}
		else if (AbsoluteAngle >= 135.f - DeadZoneAngle)
		{
			return EAnimCardinalDirection::South;
		}
		else if (AngleInDegrees > 0.f)
		{
			return EAnimCardinalDirection::East;
		}
		return EAnimCardinalDirection::West;
	}
	else if (PreviousCardinalDirection == EAnimCardinalDirection::South)
	{
		if (AbsoluteAngle <= 45.f + DeadZoneAngle)
		{
			return EAnimCardinalDirection::North;
		}
		else if (AbsoluteAngle >= (135.f - DeadZoneAngle - DeadZoneAngle))
		{
			return EAnimCardinalDirection::South;
		}
		else if (AngleInDegrees > 0.f)
		{
			return EAnimCardinalDirection::East;
		}
		return EAnimCardinalDirection::West;
	}

	// East and West
	if (AbsoluteAngle <= (45.f + DeadZoneAngle))
	{
		return EAnimCardinalDirection::North;
	}
	else if (AbsoluteAngle >= (135.f - DeadZoneAngle))
	{
		return EAnimCardinalDirection::South;
	}
	else if (AngleInDegrees > 0.f)
	{
		return EAnimCardinalDirection::East;
	}
	return EAnimCardinalDirection::West;
}

const UAnimSequence* UAnimCharacterMovementLibrary::SelectAnimForCardinalDirection(EAnimCardinalDirection CardinalDirection, const FCardinalDirectionAnimSet& AnimSet)
{
	switch (CardinalDirection)
	{
		case EAnimCardinalDirection::North:
			return AnimSet.NorthAnim;
		case EAnimCardinalDirection::East:
			return AnimSet.EastAnim;
		case EAnimCardinalDirection::South:
			return AnimSet.SouthAnim;
		case EAnimCardinalDirection::West:
			return AnimSet.WestAnim;
		default:
			checkNoEntry();
			return nullptr;
	}

	return nullptr;
}

FVector UAnimCharacterMovementLibrary::PredictGroundMovementStopLocation(const FAnimCharacterMovementSnapshot& MovementSnapshot, const FAnimCharacterMovementPredictionSnapshot& PredictionSnapshot)
{
	FVector PredictedStopLocation = FVector::ZeroVector;

	float ActualBrakingFriction = (PredictionSnapshot.bUseSeparateBrakingFriction ? PredictionSnapshot.BrakingFriction : PredictionSnapshot.GroundFriction);
	const float FrictionFactor = FMath::Max(0.f, PredictionSnapshot.BrakingFrictionFactor);
	ActualBrakingFriction = FMath::Max(0.f, ActualBrakingFriction * FrictionFactor);
	float BrakingDeceleration = FMath::Max(0.f, PredictionSnapshot.BrakingDecelerationWalking);

	const FVector WorldVelocity2D = MovementSnapshot.WorldVelocity * FVector(1.f, 1.f, 0.f);
	FVector WorldVelocityDir2D;
	float Speed2D;
	WorldVelocity2D.ToDirectionAndLength(WorldVelocityDir2D, Speed2D);

	const float Divisor = ActualBrakingFriction * Speed2D + BrakingDeceleration;
	if (Divisor > 0.f)
	{
		const float TimeToStop = Speed2D / Divisor;
		PredictedStopLocation = WorldVelocity2D * TimeToStop + 0.5f * ((-ActualBrakingFriction) * WorldVelocity2D - BrakingDeceleration * WorldVelocityDir2D) * TimeToStop * TimeToStop;
	}

	return PredictedStopLocation;
}

FVector UAnimCharacterMovementLibrary::PredictGroundMovementPivotLocation(const FAnimCharacterMovementSnapshot& MovementSnapshot, float GroundFriction)
{
	FVector PredictedPivotLocation = FVector::ZeroVector;

	const FVector WorldAcceleration2D = MovementSnapshot.WorldAcceleration * FVector(1.f, 1.f, 0.f);
	
	FVector WorldAccelerationDir2D;
	float WorldAccelerationSize2D;
	WorldAcceleration2D.ToDirectionAndLength(WorldAccelerationDir2D, WorldAccelerationSize2D);

	const float VelocityAlongAcceleration = (MovementSnapshot.WorldVelocity | WorldAccelerationDir2D);
	if (VelocityAlongAcceleration < 0.0f)
	{
		const float SpeedAlongAcceleration = -VelocityAlongAcceleration;
		const float Divisor = WorldAccelerationSize2D + 2.f * SpeedAlongAcceleration * GroundFriction;
		const float TimeToDirectionChange = SpeedAlongAcceleration / Divisor;

		const FVector AccelerationForce = MovementSnapshot.WorldAcceleration - 
			(MovementSnapshot.WorldVelocity - WorldAccelerationDir2D * MovementSnapshot.Speed2D) * GroundFriction;
	
		PredictedPivotLocation = MovementSnapshot.WorldVelocity * TimeToDirectionChange + 0.5f * AccelerationForce * TimeToDirectionChange * TimeToDirectionChange;
	}

	return PredictedPivotLocation;
}