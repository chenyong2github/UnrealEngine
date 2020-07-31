// Copyright Epic Games, Inc. All Rights Reserved.

// ported from geometry3Sharp Box3

#pragma once

#include "VectorTypes.h"
#include "BoxTypes.h"
#include "FrameTypes.h"

/**
 * TOrientedBox3 is a non-axis-aligned 3D box defined by a 3D frame and extents along the axes of that frame
 * The frame is at the center of the box.
 */
template<typename RealType>
struct TOrientedBox3
{
	// available for porting: ContainPoint, MergeBoxes()


	/** 3D position (center) and orientation (axes) of the box */
	TFrame3<RealType> Frame;
	/** Half-dimensions of box measured along the three axes */
	FVector3<RealType> Extents;

	TOrientedBox3() : Extents(1,1,1) {}

	/**
	 * Create axis-aligned box with given Origin and Extents
	 */
	TOrientedBox3(const FVector3<RealType>& Origin, const FVector3<RealType> & ExtentsIn)
		: Frame(Origin), Extents(ExtentsIn)
	{
	}

	/**
	 * Create oriented box with given Frame and Extents
	 */
	TOrientedBox3(const TFrame3<RealType>& FrameIn, const FVector3<RealType> & ExtentsIn)
		: Frame(FrameIn), Extents(ExtentsIn)
	{
	}

	/**
	 * Create oriented box from axis-aligned box
	 */
	TOrientedBox3(const TAxisAlignedBox3<RealType>& AxisBox)
		: Frame(AxisBox.Center()), Extents((RealType)0.5 * AxisBox.Diagonal())
	{
	}


	/** @return box with unit dimensions centered at origin */
	static TOrientedBox3<RealType> UnitZeroCentered() { return TOrientedBox3<RealType>(FVector3<RealType>::Zero(), (RealType)0.5*FVector3<RealType>::One()); }

	/** @return box with unit dimensions where minimum corner is at origin */
	static TOrientedBox3<RealType> UnitPositive() { return TOrientedBox3<RealType>((RealType)0.5*FVector3<RealType>::One(), (RealType)0.5*FVector3<RealType>::One()); }


	/** @return center of the box */
	FVector3<RealType> Center() const { return Frame.Origin; }

	/** @return X axis of the box */
	FVector3<RealType> AxisX() const { return Frame.X(); }

	/** @return Y axis of the box */
	FVector3<RealType> AxisY() const { return Frame.Y(); }

	/** @return Z axis of the box */
	FVector3<RealType> AxisZ() const { return Frame.Z(); }

	/** @return an axis of the box */
	FVector3<RealType> GetAxis(int AxisIndex) const { return Frame.GetAxis(AxisIndex); }

	/** @return maximum extent of box */
	inline RealType MaxExtent() const
	{
		return Extents.MaxAbs();
	}

	/** @return minimum extent of box */
	inline RealType MinExtent() const
	{
		return Extents.MinAbs();
	}

	/** @return vector from minimum-corner to maximum-corner of box */
	inline FVector3<RealType> Diagonal() const
	{
		return Frame.PointAt(Extents.X, Extents.Y, Extents.Z) - Frame.PointAt(-Extents.X, -Extents.Y, -Extents.Z);
	}

	/** @return volume of box */
	inline RealType Volume() const
	{
		return (RealType)8 * (Extents.X) * (Extents.Y) * (Extents.Z);
	}

	/** @return true if box contains point */
	inline bool Contains(const FVector3<RealType>& Point) const
	{
		FVector3<RealType> InFramePoint = Frame.ToFramePoint(Point);
		return (TMathUtil<RealType>::Abs(InFramePoint.X) <= Extents.X) &&
			(TMathUtil<RealType>::Abs(InFramePoint.Y) <= Extents.Y) &&
			(TMathUtil<RealType>::Abs(InFramePoint.Z) <= Extents.Z);
	}


	// corners [ (-x,-y), (x,-y), (x,y), (-x,y) ], -z, then +z
	//
	//   7---6     +z       or        3---2     -z
	//   |\  |\                       |\  |\
    //   4-\-5 \                      0-\-1 \
    //    \ 3---2                      \ 7---6   
	//     \|   |                       \|   |
	//      0---1  -z                    4---5  +z
	//
	// @todo does this ordering make sense for UE? we are in LHS instead of RHS here
	// if this is modified, likely need to update IndexUtil::BoxFaces and BoxFaceNormals

	/**
	 * @param Index corner index in range 0-7
	 * @return Corner point on the box identified by the given index. See diagram in OrientedBoxTypes.h for index/corner mapping.
	 */
	FVector3<RealType> GetCorner(int Index) const
	{
		check(Index >= 0 && Index <= 7);
		RealType dx = (((Index & 1) != 0) ^ ((Index & 2) != 0)) ? (Extents.X) : (-Extents.X);
		RealType dy = ((Index / 2) % 2 == 0) ? (-Extents.Y) : (Extents.Y);
		RealType dz = (Index < 4) ? (-Extents.Z) : (Extents.Z);
		return Frame.PointAt(dx, dy, dz);
	}

	/**
	 * Call CornerPointFunc(FVector3) for each of the 8 box corners. Order is the same as GetCorner(X).
	 * This is more efficient than calling GetCorner(X) because the Rotation matrix is only computed once.
	 */
	template<typename PointFuncType>
	void EnumerateCorners(PointFuncType CornerPointFunc) const
	{
		TMatrix3<RealType> RotMatrix = Frame.Rotation.ToRotationMatrix();
		RealType X = Extents.X, Y = Extents.Y, Z = Extents.Z;
		CornerPointFunc( RotMatrix*FVector3<RealType>(-X,-Y,-Z) + Frame.Origin );
		CornerPointFunc( RotMatrix*FVector3<RealType>( X,-Y,-Z) + Frame.Origin );
		CornerPointFunc( RotMatrix*FVector3<RealType>( X, Y,-Z) + Frame.Origin );
		CornerPointFunc( RotMatrix*FVector3<RealType>(-X, Y,-Z) + Frame.Origin );
		CornerPointFunc( RotMatrix*FVector3<RealType>(-X,-Y, Z) + Frame.Origin );
		CornerPointFunc( RotMatrix*FVector3<RealType>( X,-Y, Z) + Frame.Origin );
		CornerPointFunc( RotMatrix*FVector3<RealType>( X, Y, Z) + Frame.Origin );
		CornerPointFunc( RotMatrix*FVector3<RealType>(-X, Y, Z) + Frame.Origin );
	}


	/**
	 * Call CornerPointPredicate(FVector3) for each of the 8 box corners, with early-out if any call returns false
	 * @return true if all tests pass
	 */
	template<typename PointPredicateType>
	bool TestCorners(PointPredicateType CornerPointPredicate) const
	{
		TMatrix3<RealType> RotMatrix = Frame.Rotation.ToRotationMatrix();
		RealType X = Extents.X, Y = Extents.Y, Z = Extents.Z;
		return CornerPointPredicate(RotMatrix * FVector3<RealType>(-X, -Y, -Z) + Frame.Origin) &&
			CornerPointPredicate(RotMatrix * FVector3<RealType>(X, -Y, -Z) + Frame.Origin) &&
			CornerPointPredicate(RotMatrix * FVector3<RealType>(X, Y, -Z) + Frame.Origin) &&
			CornerPointPredicate(RotMatrix * FVector3<RealType>(-X, Y, -Z) + Frame.Origin) &&
			CornerPointPredicate(RotMatrix * FVector3<RealType>(-X, -Y, Z) + Frame.Origin) &&
			CornerPointPredicate(RotMatrix * FVector3<RealType>(X, -Y, Z) + Frame.Origin) &&
			CornerPointPredicate(RotMatrix * FVector3<RealType>(X, Y, Z) + Frame.Origin) &&
			CornerPointPredicate(RotMatrix * FVector3<RealType>(-X, Y, Z) + Frame.Origin);
	}



	/**
	 * Get whether the corner at Index (see diagram in GetCorner documentation comment) is in the negative or positive direction for each axis
	 * @param Index corner index in range 0-7
	 * @return Index3i with 0 or 1 for each axis, 0 if corner is in the negative direction for that axis, 1 if in the positive direction
	 */
	static FIndex3i GetCornerSide(int Index)
	{
		check(Index >= 0 && Index <= 7);
		return FIndex3i(
			(((Index & 1) != 0) ^ ((Index & 2) != 0)) ? 1 : 0,
			((Index / 2) % 2 == 0) ? 0 : 1,
			(Index < 4) ? 0 : 1
		);
	}



	/**
	 * Find squared distance to box.
	 * @param Point input point
	 * @return squared distance from point to box, or 0 if point is inside box
	 */
	RealType DistanceSquared(FVector3<RealType> Point)
	{
		 // Ported from WildMagic5 Wm5DistPoint3Box3.cpp
			 
		 // Work in the box's coordinate system.
		 Point -= Frame.Origin;

		// Compute squared distance and closest point on box.
		RealType sqrDistance = 0;
		RealType delta;
		FVector3<RealType> closest(0, 0, 0);
		for (int i = 0; i < 3; ++i) 
		{
			closest[i] = Point.Dot(GetAxis(i));
			if (closest[i] < -Extents[i]) 
			{
				delta = closest[i] + Extents[i];
				sqrDistance += delta * delta;
				closest[i] = -Extents[i];
			}
			else if (closest[i] > Extents[i])
			{
				delta = closest[i] - Extents[i];
				sqrDistance += delta * delta;
				closest[i] = Extents[i];
			}
		}

		return sqrDistance;
	}




	 /**
	  * Find closest point on box
	  * @param Point input point
	  * @return closest point on box. Input point is returned if it is inside box.
	  */
	 FVector3<RealType> ClosestPoint(FVector3<RealType> Point)
	 {
		 // Work in the box's coordinate system.
		 Point -= Frame.Origin;

		 // Compute squared distance and closest point on box.
		 RealType sqrDistance = 0;
		 RealType delta;
		 FVector3<RealType> closest;
		 FVector3<RealType> Axes[3];
		 for (int i = 0; i < 3; ++i) 
		 {
			 Axes[i] = GetAxis(i);
			 closest[i] = Point.Dot(Axes[i]);
			 RealType extent = Extents[i];
			 if (closest[i] < -extent) 
			 {
				 delta = closest[i] + extent;
				 sqrDistance += delta * delta;
				 closest[i] = -extent;
			 }
			 else if (closest[i] > extent) 
			 {
				 delta = closest[i] - extent;
				 sqrDistance += delta * delta;
				 closest[i] = extent;
			 }
		 }

		 return Frame.Origin + closest.X*Axes[0] + closest.Y*Axes[1] + closest.Z*Axes[2];
	 }



};




typedef TOrientedBox3<float> FOrientedBox3f;
typedef TOrientedBox3<double> FOrientedBox3d;
