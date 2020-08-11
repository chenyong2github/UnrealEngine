// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "SphereTypes.h"
#include "CapsuleTypes.h"
#include "OrientedBoxTypes.h"

namespace UE
{
	namespace Geometry
	{
		//
		// Sphere Containment Queries
		//

		/** @return true if InnerSphere is fully contained within OuterSphere */
		template<typename RealType>
		bool IsInside(const TSphere3<RealType>& OuterSphere, const TSphere3<RealType>& InnerSphere);

		/** @return true if InnerCapsule is fully contained within OuterSphere */
		template<typename RealType>
		bool IsInside(const TSphere3<RealType>& OuterSphere, const TCapsule3<RealType>& InnerCapsule);

		/** @return true if InnerBox is fully contained within OuterSphere */
		template<typename RealType>
		bool IsInside(const TSphere3<RealType>& OuterSphere, const TOrientedBox3<RealType>& InnerBox);

		/** @return true if all all points in range-based for over EnumerablePts are inside OuterSphere */
		template<typename RealType, typename EnumerablePointsType>
		bool IsInside(const TSphere3<RealType>& OuterSphere, EnumerablePointsType EnumerablePts)
		{
			for (FVector3<RealType> Point : EnumerablePts)
			{
				if (OuterSphere.Contains(Point) == false)
				{
					return false;
				}
			}
			return true;
		}



		//
		// Capsule Containment Queries
		//

		/** @return true if InnerCapsule is fully contained within OuterCapsule */
		template<typename RealType>
		bool IsInside(const TCapsule3<RealType>& OuterCapsule, const TCapsule3<RealType>& InnerCapsule);

		/** @return true if InnerSphere is fully contained within OuterCapsule */
		template<typename RealType>
		bool IsInside(const TCapsule3<RealType>& OuterCapsule, const TSphere3<RealType>& InnerSphere);

		/** @return true if InnerBox is fully contained within OuterCapsule */
		template<typename RealType>
		bool IsInside(const TCapsule3<RealType>& OuterCapsule, const TOrientedBox3<RealType>& InnerBox);

		/** @return true if all all points in range-based for over EnumerablePts are inside OuterCapsule */
		template<typename RealType, typename EnumerablePointsType>
		bool IsInside(const TCapsule3<RealType>& OuterCapsule, EnumerablePointsType EnumerablePts)
		{
			for (FVector3<RealType> Point : EnumerablePts)
			{
				if (OuterCapsule.Contains(Point) == false)
				{
					return false;
				}
			}
			return true;
		}



		//
		// OrientedBox Containment Queries
		//

		/** @return true if InnerBox is fully contained within OuterBox */
		template<typename RealType>
		bool IsInside(const TOrientedBox3<RealType>& OuterBox, const TOrientedBox3<RealType>& InnerBox);

		/** @return true if InnerSphere is fully contained within OuterBox */
		template<typename RealType>
		bool IsInside(const TOrientedBox3<RealType>& OuterBox, const TSphere3<RealType>& InnerSphere);

		/** @return true if InnerCapsule is fully contained within OuterBox */
		template<typename RealType>
		bool IsInside(const TOrientedBox3<RealType>& OuterBox, const TCapsule3<RealType>& InnerCapsule);

		/** @return true if all all points in range-based for over EnumerablePts are inside OuterBox */
		template<typename RealType, typename EnumerablePointsType>
		bool IsInside(const TOrientedBox3<RealType>& OuterBox, EnumerablePointsType EnumerablePts)
		{
			for (FVector3<RealType> Point : EnumerablePts)
			{
				if (OuterBox.Contains(Point) == false)
				{
					return false;
				}
			}
			return true;
		}

	}
}