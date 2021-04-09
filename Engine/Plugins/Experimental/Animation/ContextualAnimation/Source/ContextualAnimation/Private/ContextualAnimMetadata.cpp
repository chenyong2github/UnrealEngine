// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimMetadata.h"
#include "ContextualAnimSceneAsset.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "UObject/ObjectSaveContext.h"

// Copied from GeomTools.cpp and changed second param to be an ArrayView
static bool IsPointInPolygon(const FVector2D& TestPoint, const TArrayView<FVector2D>& PolygonPoints)
{
	const int NumPoints = PolygonPoints.Num();
	float AngleSum = 0.0f;
	for (int PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		const FVector2D& VecAB = PolygonPoints[PointIndex] - TestPoint;
		const FVector2D& VecAC = PolygonPoints[(PointIndex + 1) % NumPoints] - TestPoint;
		const float Angle = FMath::Sign(FVector2D::CrossProduct(VecAB, VecAC)) * FMath::Acos(FMath::Clamp(FVector2D::DotProduct(VecAB, VecAC) / (VecAB.Size() * VecAC.Size()), -1.0f, 1.0f));
		AngleSum += Angle;
	}
	return (FMath::Abs(AngleSum) > 0.001f);
}

UContextualAnimMetadata::UContextualAnimMetadata(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UContextualAnimSceneAsset* UContextualAnimMetadata::GetSceneAssetOwner() const
{
	return Cast<UContextualAnimSceneAsset>(GetOuter());
}

bool UContextualAnimMetadata::DoesQuerierPassConditions(const FContextualAnimQuerier& Querier, const FContextualAnimQueryContext& Context, const FTransform& EntryTransform) const
{
	//@TODO: EntryTransform can be get from here by accessing the AnimData that owns us

	const FTransform ToWorldTransform = Context.Actor.IsValid() ? Context.Actor->GetActorTransform() : Context.Transform;
	const FTransform QueryTransform = Querier.Actor.IsValid() ? Querier.Actor->GetActorTransform() : Querier.Transform;

	// Facing Test
	//--------------------------------------------------
	if (Facing > 0.f)
	{
		//@TODO: Cache this
		const float FacingCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(Facing), 0.f, PI));
		const FVector ToTarget = (ToWorldTransform.GetLocation() - QueryTransform.GetLocation()).GetSafeNormal2D();
		if (FVector::DotProduct(QueryTransform.GetRotation().GetForwardVector(), ToTarget) < FacingCos)
		{
			return false;
		}
	}

	// Sector Test
	//--------------------------------------------------

	if (MaxDistance > 0.f)
	{
		FVector Origin = ToWorldTransform.GetLocation();
		FVector Direction = (EntryTransform.GetLocation() - ToWorldTransform.GetLocation()).GetSafeNormal2D();

		if (DirectionOffset != 0.f)
		{
			Direction = Direction.RotateAngleAxis(DirectionOffset, FVector::UpVector);
		}

		if (OriginOffset.X != 0.f)
		{
			Origin = Origin + Direction * OriginOffset.X;
		}

		if (OriginOffset.Y != 0.f)
		{
			Origin = Origin + (Direction.ToOrientationQuat().GetRightVector()) * OriginOffset.Y;
		}

		if (NearWidth > 0.f || FarWidth > 0.f)
		{
			const float HalfNearWidth = (NearWidth / 2.f);
			const FVector RightVector = Direction.ToOrientationQuat().GetRightVector();
			const FVector A = Origin + (-RightVector * HalfNearWidth);
			const FVector B = Origin + (RightVector * HalfNearWidth);

			const float HalfFarWidth = (FarWidth / 2.f);
			const FVector FarEdgeCenter = Origin + (Direction * MaxDistance);
			const FVector C = FarEdgeCenter + (-RightVector * HalfFarWidth);
			const FVector D = FarEdgeCenter + (RightVector * HalfFarWidth);

			const float DistSq = FVector::DistSquared2D(Origin, QueryTransform.GetLocation());
			if (DistSq > FMath::Square((D - Origin).Size2D()))
			{
				return false;
			}

			// @TODO: Cache this if the owner is static 
			// or could we cache it in local space and perform the query in local space?
			TArray<FVector2D, TInlineAllocator<4>> PolygonPoints = { FVector2D(A), FVector2D(C), FVector2D(D), FVector2D(B) };

			FVector2D TestPoint = FVector2D(QueryTransform.GetLocation());
			if (!IsPointInPolygon(TestPoint, PolygonPoints))
			{
				return false;
			}
		}
		else
		{
			const float DistSq = FVector::DistSquared2D(Origin, QueryTransform.GetLocation());
			if (DistSq > FMath::Square(MaxDistance))
			{
				return false;
			}
		}
	}

	return true;
}