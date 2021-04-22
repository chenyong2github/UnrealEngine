// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	/**
	* Helpers and Wrappers for use with GJK to select the appropriate margin and support function
	* based on context. A different margin is used for sweeps and collisions,
	* and margins are used or not depending on the shape pair type involved.
	*/

	// Wraps an FImplicitObject  and provides the API required for GJK, 
	// treating the shape as if it has zero margin. This means spheres will 
	// be spheres, convexes will be the outer hull, etc.
	//
	// See also TGJKCoreShape
	//
	// E.g., to use GJK of a sphere as a point against a marginless convex:
	//		GJKDistance(TGJKCoreShape(MySphere), TGJKShape(MyConvex), ...);
	//
	template<typename T_SHAPE>
	struct TGJKShape
	{
		using FImplicitObjectType = T_SHAPE;

		TGJKShape(const FImplicitObjectType& InShape) : Shape(InShape) {}

		FReal GetMargin() const
		{
			return 0.0f;
		}

		FVec3 SupportCore(const FVec3 Dir, FReal InMargin) const
		{
			return Shape.Support(Dir, InMargin);
		}

		bool IsConvex() const
		{
			return Shape.IsConvex();
		}

		const FImplicitObjectType& Shape;
	};


	// Wraps an FImplicitObject  and provides the API required for GJK, 
	// treating the shape as if it has a reduced "core" shape with a 
	// margin suitable for collision detection where significant overlaps are likely.
	// This means spheres will be points, convexes will be rounded shrunken hulls, etc.
	//
	// See also TGJKShape
	//
	// E.g., to use GJK of a sphere as a point against a marginless convex:
	//		GJKDistance(TGJKCoreShape(MySphere), TGJKShape(MyConvex), ...);
	//
	template<typename T_SHAPE>
	struct TGJKCoreShape
	{
		using FImplicitObjectType = T_SHAPE;

		TGJKCoreShape(const FImplicitObjectType& InShape) 
			: Shape(InShape)
			, Margin(InShape.GetMargin())
		{}

		TGJKCoreShape(const FImplicitObjectType& InShape, FReal InMargin)
			: Shape(InShape)
			, Margin(InMargin)
		{}

		FReal GetMargin() const
		{
			return Margin;
		}

		FVec3 SupportCore(const FVec3 Dir, FReal InMargin) const
		{
			return Shape.SupportCore(Dir, InMargin);
		}

		bool IsConvex() const
		{
			return Shape.IsConvex();
		}

		const FImplicitObjectType& Shape;
		const FReal Margin;
	};


	// Utility for creating TGJKShape objects using template parameter deduction
	template<typename T_SHAPE>
	TGJKShape<T_SHAPE> MakeGJKShape(const T_SHAPE& InShape)
	{
		return TGJKShape<T_SHAPE>(InShape);
	}

	// Utility for creating TGJKCoreShape objects using template parameter deduction
	template<typename T_SHAPE>
	TGJKCoreShape<T_SHAPE> MakeGJKCoreShape(const T_SHAPE& InShape)
	{
		return TGJKCoreShape<T_SHAPE>(InShape);
	}
}
