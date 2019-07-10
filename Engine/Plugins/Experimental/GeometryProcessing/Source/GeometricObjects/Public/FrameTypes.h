// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "VectorTypes.h"
#include "VectorUtil.h"
#include "RayTypes.h"
#include "Quaternion.h"

/**
 * TFrame3 is an object that represents an oriented 3D coordinate frame, ie orthogonal X/Y/Z axes at a point in space.
 * One can think of this Frame as a local coordinate space measured along these axes.
 * Functions are provided to map geometric objects to/from the Frame coordinate space.
 * 
 * Internally the representation is the same as an FTransform, except a Frame has no Scale.
 */
template<typename RealType>
struct TFrame3
{
	/**
	 * Origin of the frame
	 */
	FVector3<RealType> Origin;

	/**
	 * Rotation of the frame. Think of this as the rotation of the unit X/Y/Z axes to the 3D frame axes.
	 */
	TQuaternion<RealType> Rotation;

	/**
	 * Construct a frame positioned at (0,0,0) aligned to the unit axes
	 */
	TFrame3()
	{
		Origin = FVector3<RealType>::Zero();
		Rotation = TQuaternion<RealType>::Identity();
	}

	/**
	 * Construct a frame at the given Origin aligned to the unit axes
	 */
	TFrame3(const FVector3<RealType>& OriginIn)
	{
		Origin = OriginIn;
		Rotation = TQuaternion<RealType>::Identity();
	}

	/**
	 * Construct a Frame from the given Origin and Rotation
	 */
	TFrame3(const FVector3<RealType>& OriginIn, const TQuaternion<RealType> RotationIn)
	{
		Origin = OriginIn;
		Rotation = RotationIn;
	}

	/**
	 * Construct a frame with the Z axis aligned to a target axis
	 * @param OriginIn origin of frame
	 * @param SetZ target Z axis
	 */
	TFrame3(const FVector3<RealType>& OriginIn, const FVector3<RealType>& SetZ)
	{
		Origin = OriginIn;
		Rotation.SetFromTo(FVector3<RealType>::UnitZ(), SetZ);
	}

	/**
	 * Construct Frame from X/Y/Z axis vectors. Vectors must be mutually orthogonal.
	 * @param OriginIn origin of frame
	 * @param X desired X axis of frame
	 * @param Y desired Y axis of frame
	 * @param Z desired Z axis of frame
	 */
	TFrame3(const FVector3<RealType>& OriginIn, const FVector3<RealType>& X, const FVector3<RealType>& Y, const FVector3<RealType>& Z)
	{
		Origin = OriginIn;
		Rotation = TQuaternion<RealType>( TMatrix3<RealType>(X, Y, Z, false) );
	}

	/** Construct a Frame from an FTransform */
	TFrame3(const FTransform& Transform)
	{
		Origin = Transform.GetTranslation();
		Rotation = Transform.GetRotation();
	}

	
	/**
	 * @param AxisIndex index of axis of frame, either 0, 1, or 2
	 * @return axis vector
	 */
	FVector3<RealType> GetAxis(int AxisIndex) const
	{
		switch (AxisIndex)
		{
		case 0:
			return Rotation.AxisX();
		case 1:
			return Rotation.AxisY();
		case 2:
			return Rotation.AxisZ();
		default:
			checkNoEntry();
			return FVector3<RealType>::Zero(); // compiler demands a return value
		}
	}

	/** @return X axis of frame (axis 0) */
	FVector3<RealType> X() const
	{
		return Rotation.AxisX();
	}

	/** @return Y axis of frame (axis 1) */
	FVector3<RealType> Y() const
	{
		return Rotation.AxisY();
	}

	/** @return Z axis of frame (axis 2) */
	FVector3<RealType> Z() const
	{
		return Rotation.AxisZ();
	}

	/** @return conversion of this Frame to FTransform */
	FTransform ToFTransform() const
	{
		return FTransform(Rotation, Origin);
	}


	/** @return point at distances along frame axes */
	FVector3<RealType> PointAt(RealType X, RealType Y, RealType Z) const
	{
		return Rotation * FVector3<RealType>(X,Y,Z) + Origin;
	}

	/** @return point at distances along frame axes */
	FVector3<RealType> PointAt(const FVector3<RealType>& Point) const
	{
		return Rotation * FVector3<RealType>(Point.X, Point.Y, Point.Z) + Origin;
	}
	
	/** @return input Point transformed into local coordinate system of Frame */
	FVector3<RealType> ToFramePoint(const FVector3<RealType>& Point) const
	{
		return Rotation.InverseMultiply((Point-Origin));
	}
	/** @return input Point transformed from local coordinate system of Frame into "World" coordinate system */
	FVector3<RealType> FromFramePoint(const FVector3<RealType>& Point) const
	{
		return Rotation * Point + Origin;
	}


	/** @return input Vector transformed into local coordinate system of Frame */
	FVector3<RealType> ToFrameVector(const FVector3<RealType>& Vector) const
	{
		return Rotation.InverseMultiply(Vector);
	}
	/** @return input Vector transformed from local coordinate system of Frame into "World" coordinate system */
	FVector3<RealType> FromFrameVector(const FVector3<RealType>& Vector) const
	{
		return Rotation * Vector;
	}


	/** @return input Quaternion transformed into local coordinate system of Frame */
	TQuaternion<RealType> ToFrame(const TQuaternion<RealType>& Quat) const
	{
		return Rotation.Inverse() * Quat;
	}
	/** @return input Quaternion transformed from local coordinate system of Frame into "World" coordinate system */
	TQuaternion<RealType> FromFrame(const TQuaternion<RealType>& Quat) const
	{
		return Rotation * Quat;
	}


	/** @return input Ray transformed into local coordinate system of Frame */
	TRay3<RealType> ToFrame(const TRay3<RealType>& Ray) const
	{
		return TRay3<RealType>(ToFramePoint(Ray.Origin), ToFrameVector(Ray.Direction));
	}
	/** @return input Ray transformed from local coordinate system of Frame into "World" coordinate system */
	TRay3<RealType> FromFrame(const TRay3<RealType>& Ray) const
	{
		return TRay3<RealType>(ToFramePoint(Ray.Origin), ToFrameVector(Ray.Direction));
	}


	/** @return input Frame transformed into local coordinate system of this Frame */
	TFrame3<RealType> ToFrame(const TFrame3<RealType>& Frame) const
	{
		return TFrame3<RealType>(ToFramePoint(Frame.Origin), ToFrame(Frame.Rotation));
	}
	/** @return input Frame transformed from local coordinate system of this Frame into "World" coordinate system */
	TFrame3<RealType> FromFrame(const TFrame3<RealType>& Frame) const
	{
		return TFrame3<RealType>(ToFramePoint(Frame.Origin), FromFrame(Frame.Rotation));
	}




	/**
	 * Project 3D point into plane and convert to UV coordinates in that plane
	 * @param Pos 3D position
	 * @param PlaneNormalAxis which plane to project onto, identified by perpendicular normal. Default is 2, ie normal is Z, plane is (X,Y)
	 * @return 2D coordinates in UV plane, relative to origin
	 */
	FVector2<RealType> ToPlaneUV(const FVector3<RealType>& Pos, int PlaneNormalAxis) const
	{
		int Axis0 = 0, Axis1 = 1;
		if (PlaneNormalAxis == 0)
		{
			Axis0 = 2;
		}
		else if (PlaneNormalAxis == 1)
		{
			Axis1 = 2;
		}
		FVector3<RealType> LocalPos = Pos - Origin;
		RealType U = LocalPos.Dot(GetAxis(Axis0));
		RealType V = LocalPos.Dot(GetAxis(Axis1));
		return FVector2<RealType>(U, V);
	}



	/**
	 * Map a point from local UV plane coordinates to the corresponding 3D point in one of the planes of the frame
	 * @param PosUV local UV plane coordinates
	 * @param PlaneNormalAxis which plane to map to, identified by perpendicular normal. Default is 2, ie normal is Z, plane is (X,Y)
	 * @return 3D coordinates in frame's plane (including Origin translation)
	 */
	FVector3<RealType> FromPlaneUV(const FVector2<RealType>& PosUV, int PlaneNormalAxis) const
	{
		FVector3<RealType> PlanePos(PosUV[0], PosUV[1], 0);
		if (PlaneNormalAxis == 0)
		{
			PlanePos[0] = 0; PlanePos[2] = PosUV[0];
		}
		else if (PlaneNormalAxis == 1)
		{
			PlanePos[1] = 0; PlanePos[2] = PosUV[1];
		}
		return Rotation*PlanePos + Origin;
	}



	/**
	 * Project a point onto one of the planes of the frame
	 * @param Pos 3D position
	 * @param PlaneNormalAxis which plane to project onto, identified by perpendicular normal. Default is 2, ie normal is Z, plane is (X,Y)
	 * @return 3D coordinate in the plane
	 */
	FVector3<RealType> ToPlane(const FVector3<RealType>& Pos, int PlaneNormalAxis) const
	{
		FVector3<RealType> Normal = GetAxis(PlaneNormalAxis);
		FVector3<RealType> LocalVec = Pos - Origin;
		RealType SignedDist = LocalVec.Dot(Normal);
		return Pos - SignedDist * Normal;
	}




	/**
	 * Rotate this frame by given quaternion
	 */
	void Rotate(const TQuaternion<RealType>& Quat)
	{
		Rotation = Quat * Rotation;
	}


	/**
	 * transform this frame by the given transform
	 */
	void Transform(const FTransform& XForm)
	{
		Origin = (FVector3<RealType>)XForm.TransformPosition((FVector3f)Origin);
		Rotation = TQuaternion<RealType>(XForm.GetRotation()) * Rotation;
	}


	/**
	 * Align an axis of this frame with a target direction
	 * @param AxisIndex which axis to align
	 * @param ToDirection target direction
	 */
	void AlignAxis(int AxisIndex, const FVector3<RealType>& ToDirection)
	{
		TQuaternion<RealType> RelRotation(GetAxis(AxisIndex), ToDirection);
		Rotate(RelRotation);
	}


	/**
	 * Compute rotation around vector that best-aligns axis of frame with target direction
	 * @param AxisIndex which axis to try to align
	 * @param ToDirection target direction
	 * @param AroundVector rotation is constrained to be around this vector (ie this direction in frame stays constant)
	 */
	void ConstrainedAlignAxis(int AxisIndex, const FVector3<RealType>& ToDirection, const FVector3<RealType>& AroundVector)
	{
		//@todo PlaneAngleSigned does acos() and then SetAxisAngleD() does cos/sin...can we optimize this?
		FVector3<RealType> AxisVec = GetAxis(AxisIndex);
		RealType AngleDeg = VectorUtil::PlaneAngleSignedD(AxisVec, ToDirection, AroundVector);
		TQuaternion<RealType> RelRotation;
		RelRotation.SetAxisAngleD(AroundVector, AngleDeg);
		Rotate(RelRotation);
	}


	/**
	 * Compute rotation around NormalAxis that best-aligns one of the other two frame axes with either given UpAxis or FallbackAxis
	 * (FallbackAxis is required if Dot(NormalAxis,UpAxis) > UpDotTolerance, ie if the Normal and Up directions are too closely aligned.
	 * Basically this divides direction-sphere into three regions - polar caps with size defined by UpDotTolerance, and
	 * a wide equator band covering the rest. When crossing between these regions the alignment has a discontinuity.
	 * It is impossible to avoid this discontinuity because it is impossible to comb a sphere.
	 * @param PerpAxis1 Index of first axis orthogonal to NormalAxis
	 * @param PerpAxis2 Index of second axis orthogonal to NormalAxis
	 * @param NormalAxis Axis of frame to rotate around
	 * @param UpAxis Target axis in equator region, defaults to UnitZ
	 * @param FallbackAxis Target axis in polar region, defaults to UnitX
	 * @param UpDotTolerance defaults to cos(45), ie flip between regions happens roughly half way to poles
	 */
	void ConstrainedAlignPerpAxes(int PerpAxis1 = 0, int PerpAxis2 = 1, int NormalAxis = 2, 
		const FVector3<RealType>& UpAxis = FVector3<RealType>::UnitZ(),
		const FVector3<RealType>& FallbackAxis = FVector3<RealType>::UnitX(),
		RealType UpDotTolerance = (RealType)0.707)
	{
		check(PerpAxis1 != PerpAxis2 && PerpAxis1 != NormalAxis && PerpAxis2 != NormalAxis);
		const FVector3<RealType> NormalVec = GetAxis(NormalAxis);

		// decide if we should use Fallback (polar-cap) axis or main (equator-region) axis
		const FVector3<RealType>& TargetAxis =
			(TMathUtil<RealType>::Abs(NormalVec.Dot(UpAxis)) > UpDotTolerance) ?
			FallbackAxis : UpAxis;

		// figure out which PerpAxis is closer to target, and align that one to +/- TargetAxis (whichever is smaller rotation)
		FVector2<RealType> Dots(GetAxis(PerpAxis1).Dot(TargetAxis), GetAxis(PerpAxis2).Dot(TargetAxis));
		int UseAxis = (TMathUtil<RealType>::Abs(Dots.X) > TMathUtil<RealType>::Abs(Dots.Y)) ? 0 : 1;
		RealType UseSign = Dots[UseAxis] < 0 ? -1 : 1;
		ConstrainedAlignAxis(UseAxis, UseSign*TargetAxis, NormalVec);
	}


	/**
	 * Compute intersection of ray with plane defined by frame origin and axis as normal
	 * @param RayOrigin origin of ray
	 * @param RayDirection direction of ray
	 * @param PlaneNormalAxis which axis of frame to use as plane normal
	 * @return intersection point, or FVector3::Max() if ray is parallel to plane
	 */
	FVector3<RealType> RayPlaneIntersection(const FVector3<RealType>& RayOrigin, const FVector3<RealType>& RayDirection, int PlaneNormalAxis)
	{
		FVector3<RealType> Normal = GetAxis(PlaneNormalAxis);
		RealType PlaneD = -Origin.Dot(Normal);
		RealType NormalDot = RayDirection.Dot(Normal);
		if (VectorUtil::EpsilonEqual(NormalDot, (RealType)0, TMathUtil<RealType>::ZeroTolerance))
			return FVector3<RealType>::Max();
		RealType t = -( RayOrigin.Dot(Normal) + PlaneD) / NormalDot;
		return RayOrigin + t * RayDirection;
	}

};

typedef TFrame3<float> FFrame3f;
typedef TFrame3<double> FFrame3d;

