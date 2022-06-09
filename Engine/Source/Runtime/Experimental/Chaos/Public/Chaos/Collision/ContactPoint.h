// Copyright Epic Games, Inc.All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/UnrealTemplate.h"

#include "Chaos/Core.h"
#include "Chaos/Vector.h"


namespace Chaos
{
	/**
	 * @brief Used in FContactPoint to indicate whether the contact is vertex-plane, edge-edge, etc
	 * 
	 * @note the order here is the order of preference in the solver. I.e., we like to solve Plane contacts before edge contacts before vertex contacts.
	 * This is most impotant for collisions against triangle meshes (or any concave shape) where the second shape is always the triangle, and so a PlaneVertex collision 
	 * counts as a vertex collision.
	*/
	enum class EContactPointType : int8
	{
		Unknown,
		VertexPlane,
		EdgeEdge,
		PlaneVertex,
		VertexVertex,
	};

	/**
	 * @brief Data returned by the low-level collision functions
	*/
	class CHAOS_API FContactPoint
	{
	public:
		// Shape-space contact points on the two bodies
		FVec3 ShapeContactPoints[2];

		// Shape-space contact normal on the second shape with direction that points away from shape 1
		FVec3 ShapeContactNormal;

		// Contact separation (negative for overlap)
		FReal Phi;

		// Face index of the shape we hit. Only valid for Heightfield and Trimesh contact points, otherwise INDEX_NONE
		int32 FaceIndex;

		// Whether this is a vertex-plane contact, edge-edge contact etc.
		EContactPointType ContactType;

		FContactPoint()
			: Phi(TNumericLimits<FReal>::Max())
			, FaceIndex(INDEX_NONE)
			, ContactType(EContactPointType::Unknown)
		{
		}

		// Whether the contact point has been set up with contact data
		bool IsSet() const { return (Phi != TNumericLimits<FReal>::Max()); }

		// Switch the shape indices. For use when calling a collision detection method which takes shape types in the opposite order to what you want.
		// @todo(chaos): remove or fix this function
		// WARNING: this function can no longer be used in isolation as it could when we were calculating world-space contact data. For this to
		// work correctly, the normal must either already be in the space of the first shape, or will need to be transformed after.
		// Alternatively we could start using EContactPointType to indicate normal ownership
		FContactPoint& SwapShapes()
		{
			if (IsSet())
			{
				Swap(ShapeContactPoints[0], ShapeContactPoints[1]);
				ShapeContactNormal = -ShapeContactNormal;
			}
			return *this;
		}
	};
}