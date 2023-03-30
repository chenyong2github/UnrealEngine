// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "Components/SplineComponent.h" // Required for ESplineCoordinateSpace::World

#include "LearningAgentsHelpers.generated.h"

class UMeshComponent;

UCLASS()
class LEARNINGAGENTS_API ULearningAgentsHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	//------------------------------------------------------------------
	// General Math Helpers
	//------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "LearningAgents|Helpers")
	static FTransform ProjectTransformOntoGroundPlane(const FTransform Transform, const FVector LocalForwardVector = FVector::ForwardVector);

	UFUNCTION(BlueprintPure, Category = "LearningAgents|Helpers")
	static void ProjectPositionRotationOntoGroundPlane(FVector& OutPosition, FRotator& OutRotation, const FVector InPosition, const FRotator InRotation, const FVector LocalForwardVector = FVector::ForwardVector);

	//------------------------------------------------------------------
	// MeshComponent Helpers 
	//------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "LearningAgents|Helpers")
	static void GetMeshBonePositions(const UMeshComponent* MeshComponent, const TArray<FName>& BoneNames, TArray<FVector>& OutBonePositions);

	//------------------------------------------------------------------
	// SplineComponent Helpers 
	//------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "LearningAgents|Helpers")
	static float GetProportionAlongSpline(const USplineComponent* SplineComponent, const float DistanceAlongSpline);

	UFUNCTION(BlueprintPure, Category = "LearningAgents|Helpers")
	static float GetProportionAlongSplineAsAngle(const USplineComponent* SplineComponent, const float DistanceAlongSpline);

	UFUNCTION(BlueprintPure, Category = "LearningAgents|Helpers")
	static void GetPositionsAlongSpline(TArray<FVector>& OutPositions, const USplineComponent* SplineComponent, const int32 PositionNum, const float StartDistanceAlongSpline, const float StopDistanceAlongSpline, const ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::World);
	
	UFUNCTION(BlueprintPure, Category = "LearningAgents|Helpers")
	static float GetVelocityAlongSpline(const USplineComponent* SplineComponent, const FVector Position, const FVector Velocity, const float FiniteDifferenceDelta = 1.0f, const ESplineCoordinateSpace::Type CoordinateSpace = ESplineCoordinateSpace::World);
};
