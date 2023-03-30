// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsHelpers.h"

#include "LearningLog.h"

#include "Components/MeshComponent.h"
#include "Components/SplineComponent.h"

//------------------------------------------------------------------

FTransform ULearningAgentsHelpers::ProjectTransformOntoGroundPlane(const FTransform Transform, const FVector LocalForwardVector)
{
	FVector Position = Transform.GetLocation();
	Position.Z = 0.0f;

	const FVector Direction = (FVector(1.0f, 1.0f, 0.0f) * Transform.TransformVectorNoScale(LocalForwardVector)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

	return FTransform(FQuat::FindBetweenNormals(FVector::ForwardVector, Direction), Position);
}

void ULearningAgentsHelpers::ProjectPositionRotationOntoGroundPlane(FVector& OutPosition, FRotator& OutRotation, const FVector InPosition, const FRotator InRotation, const FVector LocalForwardVector)
{
	OutPosition = InPosition;
	OutPosition.Z = 0.0f;

	const FVector Direction = (FVector(1.0f, 1.0f, 0.0f) * InRotation.RotateVector(LocalForwardVector)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	OutRotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Direction).Rotator();
}

//------------------------------------------------------------------

void ULearningAgentsHelpers::GetMeshBonePositions(const UMeshComponent* MeshComponent, const TArray<FName>& BoneNames, TArray<FVector>& OutBonePositions)
{
	if (!MeshComponent)
	{
		UE_LOG(LogLearning, Warning, TEXT("Mesh Component was nullptr."));
		return;
	}

	const int32 BoneNum = BoneNames.Num();

	OutBonePositions.SetNumUninitialized(BoneNum);

	for (int32 BoneIdx = 0; BoneIdx < BoneNum; BoneIdx++)
	{
		OutBonePositions[BoneIdx] = MeshComponent->GetSocketLocation(BoneNames[BoneIdx]);
	}
}

//------------------------------------------------------------------

float ULearningAgentsHelpers::GetProportionAlongSpline(const USplineComponent* SplineComponent, const float DistanceAlongSpline)
{
	if (!SplineComponent)
	{
		UE_LOG(LogLearning, Warning, TEXT("Spline Component was nullptr."));
		return 0.0f;
	}

	return FMath::Clamp(DistanceAlongSpline / FMath::Max(SplineComponent->GetSplineLength(), UE_SMALL_NUMBER), 0.0f, 1.0f);
}

float ULearningAgentsHelpers::GetProportionAlongSplineAsAngle(const USplineComponent* SplineComponent, const float DistanceAlongSpline)
{
	if (!SplineComponent)
	{
		UE_LOG(LogLearning, Warning, TEXT("Spline Component was nullptr."));
		return 0.0f;
	}

	if (!SplineComponent->IsClosedLoop())
	{
		UE_LOG(LogLearning, Warning, TEXT("Getting proportion along spline as angle, but spline is not closed loop. Consider using GetPropotionAlongSpline instead."));
	}

	const float TotalDistance = SplineComponent->GetSplineLength();

	const float Ratio = UE_TWO_PI * (FMath::Wrap(DistanceAlongSpline, 0.0f, TotalDistance) / FMath::Max(TotalDistance, UE_SMALL_NUMBER)) - UE_PI;

	return FMath::RadiansToDegrees(Ratio);
}

void ULearningAgentsHelpers::GetPositionsAlongSpline(TArray<FVector>& OutPositions, const USplineComponent* SplineComponent, const int32 PositionNum, const float StartDistanceAlongSpline, const float StopDistanceAlongSpline, const ESplineCoordinateSpace::Type CoordinateSpace)
{
	if (!SplineComponent)
	{
		UE_LOG(LogLearning, Warning, TEXT("Spline Component was nullptr."));
		return;
	}

	OutPositions.SetNumUninitialized(PositionNum);

	const float TotalDistance = SplineComponent->GetSplineLength();
	const bool bIsClosedLoop = SplineComponent->IsClosedLoop();

	for (int32 PositionIdx = 0; PositionIdx < PositionNum; PositionIdx++)
	{
		float PositionDistance = PositionNum == 1 ? 
			(StartDistanceAlongSpline + StopDistanceAlongSpline) / 2.0f : FMath::Lerp(
				StartDistanceAlongSpline,
				StopDistanceAlongSpline,
				((float)PositionIdx) / (PositionNum - 1));

		PositionDistance = bIsClosedLoop ? FMath::Wrap(PositionDistance, 0.0f, TotalDistance) : PositionDistance;

		OutPositions[PositionIdx] = SplineComponent->GetLocationAtDistanceAlongSpline(PositionDistance, CoordinateSpace);
	}
}

float ULearningAgentsHelpers::GetVelocityAlongSpline(const USplineComponent* SplineComponent, const FVector Position, const FVector Velocity, const float FiniteDifferenceDelta, const ESplineCoordinateSpace::Type CoordinateSpace)
{
	if (!SplineComponent)
	{
		UE_LOG(LogLearning, Warning, TEXT("Spline Component was nullptr."));
		return 0.0f;
	}

	float Distance0 = SplineComponent->GetDistanceAlongSplineAtLocation(Position, CoordinateSpace);
	float Distance1 = SplineComponent->GetDistanceAlongSplineAtLocation(Position + FiniteDifferenceDelta * Velocity, CoordinateSpace);

	if (SplineComponent->IsClosedLoop())
	{
		const float SplineDistance = SplineComponent->GetSplineLength();

		if (FMath::Abs(Distance0 - (Distance1 + SplineDistance)) < FMath::Abs(Distance0 - Distance1))
		{
			Distance1 = Distance1 + SplineDistance;
		}
		else if (FMath::Abs((Distance0 + SplineDistance) - Distance1) < FMath::Abs(Distance0 - Distance1))
		{
			Distance0 = Distance0 + SplineDistance;
		}
	}

	return (Distance1 - Distance0) / FMath::Max(FiniteDifferenceDelta, UE_SMALL_NUMBER);
}

