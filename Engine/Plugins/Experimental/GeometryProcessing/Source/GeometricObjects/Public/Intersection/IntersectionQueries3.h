// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "SphereTypes.h"
#include "CapsuleTypes.h"
#include "HalfspaceTypes.h"
#include "OrientedBoxTypes.h"

namespace UE
{
	namespace Geometry
	{
		//
		// Halfspace Intersection Queries
		//

		/** @return true if Halfspace and Sphere intersect */
		template<typename RealType>
		bool TestIntersection(const THalfspace3<RealType>& Halfspace, const TSphere3<RealType>& Sphere);

		/** @return true if Halfspace and Capsule intersect */
		template<typename RealType>
		bool TestIntersection(const THalfspace3<RealType>& Halfspace, const TCapsule3<RealType>& Capsule);

		/** @return true if Halfspace and Box intersect */
		template<typename RealType>
		bool TestIntersection(const THalfspace3<RealType>& Halfspace, const TOrientedBox3<RealType>& Box);


	}
}