// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSelectionCriterion.h"
#include "ContextualAnimSceneAsset.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "UObject/ObjectSaveContext.h"
#include "SceneManagement.h"

UContextualAnimSelectionCriterion::UContextualAnimSelectionCriterion(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UContextualAnimSceneAsset* UContextualAnimSelectionCriterion::GetSceneAssetOwner() const
{
	return Cast<UContextualAnimSceneAsset>(GetOuter());
}

// UContextualAnimSelectionCriterion_Blueprint
//===========================================================================
UContextualAnimSelectionCriterion_Blueprint::UContextualAnimSelectionCriterion_Blueprint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) 
{
}

const UContextualAnimSceneAsset* UContextualAnimSelectionCriterion_Blueprint::GetSceneAsset() const
{
	return GetSceneAssetOwner();
}

bool UContextualAnimSelectionCriterion_Blueprint::DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const
{
	return BP_DoesQuerierPassCondition(Primary, Querier);
}

// UContextualAnimSelectionCriterion_TriggerArea
//===========================================================================

UContextualAnimSelectionCriterion_TriggerArea::UContextualAnimSelectionCriterion_TriggerArea(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//@TODO: I think we could initialize this from the animations so it automatically creates an area from the primary to the owner of this criterion
	PolygonPoints.Add(FVector(100, -100, 0));
	PolygonPoints.Add(FVector(-100, -100, 0));
	PolygonPoints.Add(FVector(-100, 100, 0));
	PolygonPoints.Add(FVector(100, 100, 0));
}

bool UContextualAnimSelectionCriterion_TriggerArea::DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const
{
	check(PolygonPoints.Num() == 4);

	bool bResult = false;

	const FTransform PrimaryTransform = Primary.GetTransform();
	const FTransform QuerierTransform = Querier.GetTransform();

	const float HalfHeight = FMath::Max((Height / 2.f), 0.f);
	const float VDist = FMath::Abs((PrimaryTransform.GetLocation().Z + PolygonPoints[0].Z + HalfHeight) - QuerierTransform.GetLocation().Z);
	if (VDist <= HalfHeight)
	{
		const FVector2D TestPoint = FVector2D(QuerierTransform.GetLocation());
		const int32 NumPoints = PolygonPoints.Num();
		float AngleSum = 0.0f;
		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			const FVector2D& VecAB = FVector2D(PrimaryTransform.TransformPositionNoScale(PolygonPoints[PointIndex])) - TestPoint;
			const FVector2D& VecAC = FVector2D(PrimaryTransform.TransformPositionNoScale(PolygonPoints[(PointIndex + 1) % NumPoints])) - TestPoint;
			const float Angle = FMath::Sign(FVector2D::CrossProduct(VecAB, VecAC)) * FMath::Acos(FMath::Clamp(FVector2D::DotProduct(VecAB, VecAC) / (VecAB.Size() * VecAC.Size()), -1.0f, 1.0f));
			AngleSum += Angle;
		}

		bResult = (FMath::Abs(AngleSum) > 0.001f);
	}

	UE_LOG(LogContextualAnim, Verbose, TEXT("UContextualAnimSelectionCriterion_TriggerArea: Primary: %s Querier: %s Result: %d"),
		*GetNameSafe(Primary.GetActor()), *GetNameSafe(Querier.GetActor()), bResult);

	return bResult;
}

// UContextualAnimSelectionCriterion_Angle
//===========================================================================

bool UContextualAnimSelectionCriterion_Angle::DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const
{
	const FTransform PrimaryTransform = Primary.GetTransform();
	const FTransform QuerierTransform = Querier.GetTransform();

	auto CalculateAngle = [](const FTransform& A, const FTransform& B, bool bSignedAngle)
	{
		const FVector ToTarget = (A.GetLocation() - B.GetLocation()).GetSafeNormal2D();
		const float ForwardCosAngle = FVector::DotProduct(B.GetRotation().GetForwardVector(), ToTarget);
		const float ForwardDeltaDegree = FMath::RadiansToDegrees(FMath::Acos(ForwardCosAngle));
		
		if(bSignedAngle)
		{
			const float RightCosAngle = FVector::DotProduct(B.GetRotation().GetRightVector(), ToTarget);
			return (RightCosAngle < 0) ? (ForwardDeltaDegree * -1) : ForwardDeltaDegree;
		}
		else
		{
			return ForwardDeltaDegree;
		}
	};

	float Angle = 0.f;
	if (Mode == EContextualAnimCriterionAngleMode::ToPrimary)
	{
		Angle = CalculateAngle(PrimaryTransform, QuerierTransform, bUseSignedAngle);
	}
	else if (Mode == EContextualAnimCriterionAngleMode::FromPrimary)
	{
		Angle = CalculateAngle(QuerierTransform, PrimaryTransform, bUseSignedAngle);
	}

	bool bResult = false;
	if (MinAngle <= 0 && MaxAngle >= 0)
	{
		bResult = FMath::IsWithinInclusive(FMath::Abs(Angle), FMath::Abs(MinAngle), FMath::Abs(MaxAngle));
	}
	else
	{
		bResult = FMath::IsWithinInclusive(Angle, MinAngle, MaxAngle);
	}

	UE_LOG(LogContextualAnim, Verbose, TEXT("UContextualAnimSelectionCriterion_Angle: Primary: %s Querier: %s Mode: %s bUseSignedAngle: %d MinAngle: %.1f MaxAngle: %.1f Angle: %.1f Result: %d"), 
		*GetNameSafe(Primary.GetActor()), *GetNameSafe(Querier.GetActor()), *UEnum::GetValueAsString(TEXT("ContextualAnimation.EContextualAnimCriterionAngleMode"), Mode), bUseSignedAngle, MinAngle, MaxAngle, Angle, bResult);

	return bResult;
}

// UContextualAnimSelectionCriterion_Distance
//===========================================================================

bool UContextualAnimSelectionCriterion_Distance::DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const
{
	const FTransform PrimaryTransform = Primary.GetTransform();
	const FTransform QuerierTransform = Querier.GetTransform();

	float Distance = 0.f;
	if (Mode == EContextualAnimCriterionDistanceMode::Distance_2D)
	{
		Distance = FVector::Dist2D(PrimaryTransform.GetLocation(), QuerierTransform.GetLocation());
	}
	else if (Mode == EContextualAnimCriterionDistanceMode::Distance_3D)
	{
		Distance = FVector::Dist(PrimaryTransform.GetLocation(), QuerierTransform.GetLocation());
	}

	const bool bResult = FMath::IsWithinInclusive(Distance, MinDistance, MaxDistance);

	UE_LOG(LogContextualAnim, Verbose, TEXT("UContextualAnimSelectionCriterion_Distance: Primary: %s Querier: %s Mode: %s MaxDistance: %.1f MaxDist: %.1f Dist: %.1f Result: %d"),
		*GetNameSafe(Primary.GetActor()), *GetNameSafe(Querier.GetActor()), *UEnum::GetValueAsString(TEXT("ContextualAnimation.EContextualAnimCriterionDistanceMode"), Mode), MinDistance, MaxDistance, Distance, bResult);

	return bResult;
}