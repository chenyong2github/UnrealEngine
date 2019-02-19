// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/SceneComponent.h"
#include "KismetAnimationTypes.h"
#include "KismetAnimationLibrary.generated.h"

class USkeletalMeshComponent;

UCLASS(meta=(ScriptName="AnimGraphLibrary"))
class ANIMGRAPHRUNTIME_API UKismetAnimationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintPure, Category = "Utilities|Animation", meta = (DisplayName = "Two Bone IK Function", ScriptName = "TwoBoneIK", bAllowStretching = "false", StartStretchRatio = "1.0", MaxStretchScale = "1.2"))
	static void K2_TwoBoneIK(const FVector& RootPos, const FVector& JointPos, const FVector& EndPos, const FVector& JointTarget, const FVector& Effector, FVector& OutJointPos, FVector& OutEndPos, bool bAllowStretching, float StartStretchRatio, float MaxStretchScale);

	UFUNCTION(BlueprintPure, Category = "Utilities|Animation", meta = (DisplayName = "Look At Function", ScriptName = "LookAt", bUseUpVector = "false"))
	static FTransform K2_LookAt(const FTransform& CurrentTransform, const FVector& TargetPosition, FVector LookAtVector, bool bUseUpVector, FVector UpVector, float ClampConeInDegree);

	UFUNCTION(BlueprintPure, Category = "Utilities|Animation", meta = (DisplayName = "Get Distance Between Two Sockets", ScriptName = "DistanceBetweenSockets", bRemapRange= "false"))
	static float K2_DistanceBetweenTwoSocketsAndMapRange(const USkeletalMeshComponent* Component, const FName SocketOrBoneNameA, ERelativeTransformSpace SocketSpaceA, const FName SocketOrBoneNameB, ERelativeTransformSpace SocketSpaceB, bool bRemapRange, float InRangeMin, float InRangeMax, float OutRangeMin, float OutRangeMax);

	UFUNCTION(BlueprintPure, Category = "Utilities|Animation", meta = (DisplayName = "Get Direction Between Sockets", ScriptName = "DirectionBetweenSockets"))
	static FVector K2_DirectionBetweenSockets(const USkeletalMeshComponent* Component, const FName SocketOrBoneNameFrom, const FName SocketOrBoneNameTo);

	/* This function creates perlin noise from input X, Y, Z, and then range map to RangeOut, and out put to OutX, OutY, OutZ */
	UFUNCTION(BlueprintPure, Category = "Utilities|Animation", meta = (DisplayName = "Make Perlin Noise Vector and Remap", ScriptName = "MakeVectorFromPerlinNoise", RangeOutMinX = "-1.f", RangeOutMaxX = "1.f", RangeOutMinY = "-1.f", RangeOutMaxY = "1.f", RangeOutMinZ = "-1.f", RangeOutMaxZ = "1.f"))
	static FVector K2_MakePerlinNoiseVectorAndRemap(float X, float Y, float Z, float RangeOutMinX, float RangeOutMaxX, float RangeOutMinY, float RangeOutMaxY, float RangeOutMinZ, float RangeOutMaxZ);

	UFUNCTION(BlueprintPure, Category = "Utilities|Animation", meta = (DisplayName = "Make Perlin Noise and Remap", ScriptName = "MakeFloatFromPerlinNoise", RangeOutMinX = "-1.f", RangeOutMaxX = "1.f", RangeOutMinY = "-1.f", RangeOutMaxY = "1.f", RangeOutMinZ = "-1.f", RangeOutMaxZ = "1.f"))
	static float K2_MakePerlinNoiseAndRemap(float Value, float RangeOutMin, float RangeOutMax);

	/** 
		This function calculates the velocity of a position changing over time.
		You need to hook up a valid PositionHistory variable to this for storage.

		@param Position The position to track over time.
		@param History The history to use for storage.
		@param NumberOfSamples The number of samples to use for the history. The higher the number of samples - the smoother the velocity changes.
		@param VelocityMin The minimum velocity to use for normalization (if both min and max are set to 0, normalization is turned off)
		@param VelocityMax The maximum velocity to use for normalization (if both min and max are set to 0, normalization is turned off)
	*/
	UFUNCTION(BlueprintCallable, Category = "Utilities|Animation", meta = (DisplayName = "Calculate Velocity From Position History", ScriptName = "CalculateVelocityFromPositionHistory", NumberOfSamples = "16", VelocityMin = "0.f", VelocityMax = "128.f"))
	static float K2_CalculateVelocityFromPositionHistory(
		float DeltaSeconds,
		FVector Position,
		UPARAM(ref) FPositionHistory& History,
		int32 NumberOfSamples,
		float VelocityMin,
		float VelocityMax
	);

	UFUNCTION(BlueprintPure, Category = "Utilities|Animation", meta = (DisplayName = "Scalar Easing", ScriptName = "ScalarEasing", Value = "0.f"))
	static float K2_ScalarEasing(float Value, EEasingFuncType EasingType);

	/** 
		This function calculates the velocity of an offset position on a bone / socket over time.
		The bone's / socket's motion can be expressed within a reference frame (another bone / socket). 
		You need to hook up a valid PositionHistory variable to this for storage.

		@param SocketOrBoneName The name of the bone / socket to track.
		@param ReferenceSocketOrBone The name of the bone / socket to use as a frame of reference (or None if no frame of reference == world space).
		@param OffsetInBoneSpace The relative position in the space of the bone / socket to track over time.
		@param History The history to use for storage.
		@param NumberOfSamples The number of samples to use for the history. The higher the number of samples - the smoother the velocity changes.
		@param VelocityMin The minimum velocity to use for normalization (if both min and max are set to 0, normalization is turned off)
		@param VelocityMax The maximum velocity to use for normalization (if both min and max are set to 0, normalization is turned off)
	*/
	UFUNCTION(BlueprintCallable, Category = "Utilities|Animation", meta = (DisplayName = "Calculate Velocity From Sockets", ScriptName = "CalculateVelocityFromSockets", NumberOfSamples = "16", VelocityMin = "0.f", VelocityMax = "128.f"))
	static float K2_CalculateVelocityFromSockets(
		float DeltaSeconds, 
		USkeletalMeshComponent * Component,
		const FName SocketOrBoneName,
		const FName ReferenceSocketOrBone,
		ERelativeTransformSpace SocketSpace,
		FVector OffsetInBoneSpace, 
		UPARAM(ref) FPositionHistory& History, 
		int32 NumberOfSamples,
		float VelocityMin, 
		float VelocityMax,
		EEasingFuncType EasingType
	);
};

