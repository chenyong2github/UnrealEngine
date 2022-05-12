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
	if (PolygonPoints.Num() == 4)
	{
		const FTransform PrimaryTransform = Primary.GetTransform();
		const FTransform QuerierTransform = Querier.GetTransform();

		const float HalfHeight = FMath::Max((Height / 2.f), 0.f);
		const float VDist = FMath::Abs((PrimaryTransform.GetLocation().Z + PolygonPoints[0].Z + HalfHeight) - QuerierTransform.GetLocation().Z);
		if (VDist > HalfHeight)
		{
			return false;
		}

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

		return (FMath::Abs(AngleSum) > 0.001f);
	}

	return false;
}

// UContextualAnimSelectionCriterion_Facing
//===========================================================================

bool UContextualAnimSelectionCriterion_Facing::DoesQuerierPassCondition(const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const
{
	if (MaxAngle > 0.f)
	{
		const FTransform PrimaryTransform = Primary.GetTransform();
		const FTransform QuerierTransform = Querier.GetTransform();

		const float FacingCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(MaxAngle), 0.f, PI));
		const FVector ToTarget = (PrimaryTransform.GetLocation() - QuerierTransform.GetLocation()).GetSafeNormal2D();
		const float DotProduct = FVector::DotProduct(QuerierTransform.GetRotation().GetForwardVector(), ToTarget);
		return (DotProduct > FacingCos);
	}

	return true;
}