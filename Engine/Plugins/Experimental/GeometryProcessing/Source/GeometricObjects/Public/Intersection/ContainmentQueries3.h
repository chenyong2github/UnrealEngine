// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "SphereTypes.h"
#include "CapsuleTypes.h"
#include "OrientedBoxTypes.h"
#include "HalfspaceTypes.h"
#include "Intersection/IntersectionQueries3.h"


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
		template<typename RealType, typename EnumerablePointsType, typename E = decltype(DeclVal<EnumerablePointsType>().begin())>
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
		template<typename RealType, typename EnumerablePointsType, typename E = decltype(DeclVal<EnumerablePointsType>().begin())>
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
		template<typename RealType, typename EnumerablePointsType, typename E = decltype(DeclVal<EnumerablePointsType>().begin())>
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



		//
		// Convex Hull/Volume containment queries
		//

		/** 
		 * Test if the convex volume defined by a set of Halfspaces contains InnerSphere. 
		 * Each Halfspace normal should point "outwards", ie it defines the outer halfspace that is not inside the Convex Volume.
		 * @return false if InnerSphere intersects any of the Halfspaces
		 */
		template<typename RealType>
		bool IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, const TSphere3<RealType>& InnerSphere);

		/** 
		 * Test if the convex volume defined by a set of Halfspaces contains InnerCapsule. 
		 * Each Halfspace normal should point "outwards", ie it defines the outer halfspace that is not inside the Convex Volume.
		 * @return false if InnerCapsule intersects any of the Halfspaces
		 */
		template<typename RealType>
		bool IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, const TCapsule3<RealType>& InnerCapsule);

		/** 
		 * Test if the convex volume defined by a set of Halfspaces contains InnerBox. 
		 * Each Halfspace normal should point "outwards", ie it defines the outer halfspace that is not inside the Convex Volume.
		 * @return false if InnerBox intersects any of the Halfspaces
		 */
		template<typename RealType>
		bool IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, const TOrientedBox3<RealType>& InnerBox);

		/** 
		 * Test if the convex volume defined by a set of Halfspaces contains InnerSphere. 
		 * Each Halfspace normal should point "outwards", ie it defines the outer halfspace that is not inside the Convex Volume.
		 * @return false any of the Halfspaces contain any of the Points
		 */
		template<typename RealType, typename EnumerablePointsType, typename E = decltype(DeclVal<EnumerablePointsType>().begin())>
		bool IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, EnumerablePointsType EnumerablePts);


	}
}



template<typename RealType>
bool UE::Geometry::IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, const TSphere3<RealType>& InnerSphere)
{
	for (const THalfspace3<RealType>& Halfspace : Halfspaces)
	{
		if (UE::Geometry::TestIntersection(Halfspace, InnerSphere))
		{
			return false;
		}
	}
	return true;
}


template<typename RealType>
bool UE::Geometry::IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, const TCapsule3<RealType>& InnerCapsule)
{
	for (const THalfspace3<RealType>& Halfspace : Halfspaces)
	{
		if (UE::Geometry::TestIntersection(Halfspace, InnerCapsule))
		{
			return false;
		}
	}
	return true;
}


template<typename RealType>
bool UE::Geometry::IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, const TOrientedBox3<RealType>& InnerBox)
{
	for (const THalfspace3<RealType>& Halfspace : Halfspaces)
	{
		if (UE::Geometry::TestIntersection(Halfspace, InnerBox))
		{
			return false;
		}
	}
	return true;
}


template<typename RealType, typename EnumerablePointsType, typename E>
bool UE::Geometry::IsInsideHull(TArrayView<THalfspace3<RealType>> Halfspaces, EnumerablePointsType EnumerablePts)
{
	for (const THalfspace3<RealType>& Halfspace : Halfspaces)
	{
		for (FVector3<RealType> Point : EnumerablePts)
		{
			if (Halfspace.Contains(Point))
			{
				return false;
			}
		}
	}
	return true;
}