// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"

namespace FAISystem
{
	AIMODULE_API FVector FindClosestLocation(const FVector& Origin, const TArray<FVector>& Locations);

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
	AIMODULE_API bool CheckIsTargetInSightCone(const FVector& StartLocation, const FVector& ConeDirectionNormal, float PeripheralVisionAngleCos,
		float ConeDirectionBackwardOffset, float NearClippingRadiusSq, float const FarClippingRadiusSq, const FVector& TargetLocation);
}
