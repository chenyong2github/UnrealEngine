// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SnapshotData.generated.h"


USTRUCT(BlueprintType)
struct FBaseSnapshotData //: public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;		// world coords

	UPROPERTY()
	FVector LinearVelocity;		// world coords

	UPROPERTY()
	FVector AngularVelocity;	// world coords

};

USTRUCT(BlueprintType)
struct FWheelSnapshot
{
	GENERATED_BODY()
	
	UPROPERTY()
	float SuspensionOffset;		// suspension location
	
	UPROPERTY()
	float WheelRotationAngle;	// wheel rotation angle, rotated position
	
	UPROPERTY()
	float SteeringAngle;		// steering position

	UPROPERTY()
	float WheelRadius;			// radius of the wheel can be changed dynamically, to sim damaged ot flat

	UPROPERTY()
	float WheelAngularVelocity;	// speed of rotation of wheel
};

USTRUCT(BlueprintType)
struct FWheeledSnaphotData : public FBaseSnapshotData
{
public:
	GENERATED_BODY()

	UPROPERTY()
	int SelectedGear;		// -ve reverse gear(s), 0 neutral, +ve forward gears

	UPROPERTY()
	float EngineRPM;		// Engine Revolutions Per Minute

	UPROPERTY()
	TArray<FWheelSnapshot> WheelSnapshots;

};
