// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIHelpers.h"

namespace FAISystem
{
	FVector FindClosestLocation(const FVector& Origin, const TArray<FVector>& Locations)
	{
		FVector BestLocation = FAISystem::InvalidLocation;
		float BestDistance = FLT_MAX;

		for (const FVector& Candidate : Locations)
		{
			const float CurrentDistanceSq = FVector::DistSquared(Origin, Candidate);
			if (CurrentDistanceSq < BestDistance)
			{
				BestDistance = CurrentDistanceSq;
				BestLocation = Candidate;
			}
		}

		return BestLocation;
	}

	//----------------------------------------------------------------------//
	// CheckIsTargetInSightCone
	//                     F
	//                   *****  
	//              *             *
	//          *                     *
	//       *                           *
	//     *                               *
	//   *                                   * 
	//    \                                 /
	//     \                               /
	//      \                             /
	//       \             X             /
	//        \                         /
	//         \          ***          /
	//          \     *    N    *     /
	//           \ *               * /
	//            N                 N
	//            
	//           
	//           
	//           
	//
	// 
	//                     B 
	//
	// X = StartLocation
	// B = Backward offset
	// N = Near Clipping Radius (from the StartLocation adjusted by Backward offset)
	// F = Far Clipping Radius (from the StartLocation adjusted by Backward offset)
	//----------------------------------------------------------------------//
	bool CheckIsTargetInSightCone(const FVector& StartLocation, const FVector& ConeDirectionNormal, float PeripheralVisionAngleCos,
		float ConeDirectionBackwardOffset, float NearClippingRadiusSq, float const FarClippingRadiusSq, const FVector& TargetLocation)
	{
		const FVector BaseLocation = FMath::IsNearlyZero(ConeDirectionBackwardOffset) ? StartLocation : StartLocation - ConeDirectionNormal * ConeDirectionBackwardOffset;
		const FVector ActorToTarget = TargetLocation - BaseLocation;
		const float DistToTargetSq = ActorToTarget.SizeSquared();
		if (DistToTargetSq <= FarClippingRadiusSq && DistToTargetSq >= NearClippingRadiusSq)
		{
			// Will return true if squared distance to Target is smaller than SMALL_NUMBER
			if (DistToTargetSq < SMALL_NUMBER)
			{
				return true;
			}
			
			// Calculate the normal here instead of calling GetUnsafeNormal as we already have the DistToTargetSq (optim)
			const FVector DirectionToTargetNormal = ActorToTarget * FMath::InvSqrt(DistToTargetSq);

			return FVector::DotProduct(DirectionToTargetNormal, ConeDirectionNormal) > PeripheralVisionAngleCos;
		}

		return false;
	}
}
