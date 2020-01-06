// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Plane.h"

namespace Chaos
{
	template<typename T>
	struct TTaperedCylinderSpecializeSamplingHelper;

	template<class T>
	class TTaperedCylinder : public FImplicitObject
	{
	public:
		TTaperedCylinder()
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCylinder)
		{
			this->bIsConvex = true;
		}
		TTaperedCylinder(const TVector<T, 3>& x1, const TVector<T, 3>& x2, const T Radius1, const T Radius2)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCylinder)
		    , MPlane1(x1, (x2 - x1).GetSafeNormal())
		    , MPlane2(x2, -MPlane1.Normal())
		    , MHeight((x2 - x1).Size())
		    , MRadius1(Radius1)
		    , MRadius2(Radius2)
		    , MLocalBoundingBox(x1, x1)
		{
			this->bIsConvex = true;
			MLocalBoundingBox.GrowToInclude(x2);
			T MaxRadius = MRadius1;
			if (MaxRadius < MRadius2)
				MaxRadius = MRadius2;
			MLocalBoundingBox = TAABB<T, 3>(MLocalBoundingBox.Min() - TVector<T, 3>(MaxRadius), MLocalBoundingBox.Max() + TVector<T, 3>(MaxRadius));
		}
		TTaperedCylinder(const TTaperedCylinder<T>& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCylinder)
		    , MPlane1(Other.MPlane1)
		    , MPlane2(Other.MPlane2)
		    , MHeight(Other.MHeight)
		    , MRadius1(Other.MRadius1)
		    , MRadius2(Other.MRadius2)
		    , MLocalBoundingBox(Other.MLocalBoundingBox)
		{
			this->bIsConvex = true;
		}
		TTaperedCylinder(TTaperedCylinder<T>&& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCylinder)
		    , MPlane1(MoveTemp(Other.MPlane1))
		    , MPlane2(MoveTemp(Other.MPlane2))
		    , MHeight(Other.MHeight)
		    , MRadius1(Other.MRadius1)
		    , MRadius2(Other.MRadius2)
		    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
		{
			this->bIsConvex = true;
		}
		~TTaperedCylinder() {}

		static constexpr EImplicitObjectType StaticType() { return ImplicitObjectType::TaperedCylinder; }

		/**
		 * Returns sample points centered about the origin.
		 *
		 * \p NumPoints specifies how many points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeLocalSamplePoints(const int32 NumPoints, const bool IncludeEndCaps = true) const
		{
			TArray<TVector<T, 3>> Points;
			const TVector<T, 3> Mid = GetCenter();
			TTaperedCylinderSpecializeSamplingHelper<T>::ComputeSamplePoints(
			    Points,
			    TTaperedCylinder<T>(MPlane1.X() - Mid, MPlane2.X() - Mid, GetRadius1(), GetRadius2()),
			    NumPoints, IncludeEndCaps);
			return Points;
		}
		/** 
		 * Returns sample points centered about the origin. 
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeLocalSamplePoints(const T PointsPerUnitArea, const bool IncludeEndCaps = true, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeLocalSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea(IncludeEndCaps))), MinPoints, MaxPoints), IncludeEndCaps); }

		/**
		 * Returns sample points at the current location of the cylinder.
		 *
		 * \p NumPoints specifies how many points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeSamplePoints(const int32 NumPoints, const bool IncludeEndCaps = true) const
		{
			TArray<TVector<T, 3>> Points;
			TTaperedCylinderSpecializeSamplingHelper<T>::ComputeSamplePoints(Points, *this, NumPoints, IncludeEndCaps);
			return Points;
		}
		/** 
		 * Returns sample points at the current location of the cylinder.
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeSamplePoints(const T PointsPerUnitArea, const bool IncludeEndCaps = true, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea(IncludeEndCaps))), MinPoints, MaxPoints), IncludeEndCaps); }

		virtual const TAABB<T, 3>& BoundingBox() const override { return MLocalBoundingBox; }

		T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const
		{
			TVector<T, 3> Normal1, Normal2;
			const T Distance1 = MPlane1.PhiWithNormal(x, Normal1);
			if (Distance1 < SMALL_NUMBER)
			{
				ensure(MPlane2.PhiWithNormal(x, Normal2) > 0.);
				const TVector<T, 3> v = x - TVector<T, 3>(Normal1 * Distance1 + MPlane1.X());
				if (v.Size() > MRadius1)
				{
					const TVector<T, 3> Corner = v.GetSafeNormal() * MRadius1 + MPlane1.X();
					const TVector<T, 3> CornerVector = x - Corner;
					Normal = CornerVector.GetSafeNormal();
					return CornerVector.Size();
				}
				else
				{
					Normal = -Normal1;
					return -Distance1;
				}
			}
			const T Distance2 = MPlane2.PhiWithNormal(x, Normal2);
			if (Distance2 < SMALL_NUMBER)
			{
				ensure(MPlane1.PhiWithNormal(x, Normal1) > 0.);
				const TVector<T, 3> v = x - TVector<T, 3>(Normal2 * Distance2 + MPlane2.X());
				if (v.Size() > MRadius2)
				{
					const TVector<T, 3> Corner = v.GetSafeNormal() * MRadius2 + MPlane2.X();
					const TVector<T, 3> CornerVector = x - Corner;
					Normal = CornerVector.GetSafeNormal();
					return CornerVector.Size();
				}
				else
				{
					Normal = -Normal2;
					return -Distance2;
				}
			}
			ensure(Distance1 <= MHeight && Distance2 <= MHeight);
			const TVector<T, 3> SideVector = (x - TVector<T, 3>(Normal1 * Distance1 + MPlane1.X()));
			const T SideDistance = SideVector.Size() - GetRadius(Distance1);
			if (SideDistance < 0.)
			{
				const T TopDistance = Distance1 < Distance2 ? Distance1 : Distance2;
				if (TopDistance < -SideDistance)
				{
					Normal = Distance1 < Distance2 ? -Normal1 : -Normal2;
					return -TopDistance;
				}
			}
			Normal = SideVector.GetSafeNormal();
			return SideDistance;
		}

		Pair<TVector<T, 3>, bool> FindClosestIntersection(const TVector<T, 3>& StartPoint, const TVector<T, 3>& EndPoint, const T Thickness)
		{
			TArray<Pair<T, TVector<T, 3>>> Intersections;
			T DeltaRadius = FGenericPlatformMath::Abs(MRadius2 - MRadius1);
			if (DeltaRadius == 0)
				return TCylinder<T>(MPlane1.X(), MPlane2.X(), MRadius1).FindClosestIntersection(StartPoint, EndPoint, Thickness);
			TVector<T, 3> BaseNormal;
			T BaseRadius;
			TVector<T, 3> BaseCenter;
			if (MRadius2 > MRadius1)
			{
				BaseNormal = MPlane2.Normal();
				BaseRadius = MRadius2 + Thickness;
				BaseCenter = MPlane2.X();
			}
			else
			{
				BaseNormal = MPlane1.Normal();
				BaseRadius = MRadius1 + Thickness;
				BaseCenter = MPlane1.X();
			}
			TVector<T, 3> Top = BaseRadius / DeltaRadius * MHeight * BaseNormal + BaseCenter;
			T theta = atan2(BaseRadius, (Top - BaseCenter).Size());
			T costheta = cos(theta);
			T cossqtheta = costheta * costheta;
			check(theta > 0 && theta < PI / 2);
			TVector<T, 3> Direction = EndPoint - StartPoint;
			T Length = Direction.Size();
			Direction = Direction.GetSafeNormal();
			auto DDotN = TVector<T, 3>::DotProduct(Direction, -BaseNormal);
			auto SMT = StartPoint - Top;
			auto SMTDotN = TVector<T, 3>::DotProduct(SMT, -BaseNormal);
			T a = DDotN * DDotN - cossqtheta;
			T b = 2 * (DDotN * SMTDotN - TVector<T, 3>::DotProduct(Direction, SMT) * cossqtheta);
			T c = SMTDotN * SMTDotN - SMT.SizeSquared() * cossqtheta;
			T Determinant = b * b - 4 * a * c;
			if (Determinant == 0)
			{
				T Root = -b / (2 * a);
				TVector<T, 3> RootPoint = Root * Direction + StartPoint;
				if (Root >= 0 && Root <= Length && TVector<T, 3>::DotProduct(RootPoint - Top, -BaseNormal) >= 0)
				{
					Intersections.Add(MakePair(Root, RootPoint));
				}
			}
			if (Determinant > 0)
			{
				T Root1 = (-b - sqrt(Determinant)) / (2 * a);
				T Root2 = (-b + sqrt(Determinant)) / (2 * a);
				TVector<T, 3> Root1Point = Root1 * Direction + StartPoint;
				TVector<T, 3> Root2Point = Root2 * Direction + StartPoint;
				if (Root1 < 0 || Root1 > Length || TVector<T, 3>::DotProduct(Root1Point - Top, -BaseNormal) < 0)
				{
					if (Root2 >= 0 && Root2 <= Length && TVector<T, 3>::DotProduct(Root2Point - Top, -BaseNormal) >= 0)
					{
						Intersections.Add(MakePair(Root2, Root2Point));
					}
				}
				else if (Root2 < 0 || Root2 > Length || TVector<T, 3>::DotProduct(Root2Point - Top, -BaseNormal) < 0)
				{
					Intersections.Add(MakePair(Root1, Root1Point));
				}
				else if (Root1 < Root2 && TVector<T, 3>::DotProduct(Root1Point - Top, -BaseNormal) >= 0)
				{
					Intersections.Add(MakePair(Root1, Root1Point));
				}
				else if (TVector<T, 3>::DotProduct(Root2Point - Top, -BaseNormal) >= 0)
				{
					Intersections.Add(MakePair(Root2, Root2Point));
				}
			}
			auto Plane1Intersection = MPlane1.FindClosestIntersection(StartPoint, EndPoint, Thickness);
			if (Plane1Intersection.Second)
				Intersections.Add(MakePair((Plane1Intersection.First - StartPoint).Size(), Plane1Intersection.First));
			auto Plane2Intersection = MPlane2.FindClosestIntersection(StartPoint, EndPoint, Thickness);
			if (Plane2Intersection.Second)
				Intersections.Add(MakePair((Plane2Intersection.First - StartPoint).Size(), Plane2Intersection.First));
			Intersections.Sort([](const Pair<T, TVector<T, 3>>& Elem1, const Pair<T, TVector<T, 3>>& Elem2) { return Elem1.First < Elem2.First; });
			for (const auto& Elem : Intersections)
			{
				if (SignedDistance(Elem.Second) <= (Thickness + 1e-4))
				{
					return MakePair(Elem.Second, true);
				}
			}
			return MakePair(TVector<T, 3>(0), false);
		}

		T GetRadius1() const { return MRadius1; }
		T GetRadius2() const { return MRadius2; }
		T GetHeight() const { return MHeight; }
		T GetSlantHeight() const { const T R1mR2 = MRadius1-MRadius2; return FMath::Sqrt(R1mR2*R1mR2 + MHeight*MHeight); }
		const TVector<T, 3>& GetX1() const { return MPlane1.X(); }
		const TVector<T, 3>& GetX2() const { return MPlane2.X(); }
		/** Returns the bottommost point on the cylinder. */
		const TVector<T, 3>& GetOrigin() const { return MPlane1.X(); }
		/** Returns the topmost point on the cylinder. */
		const TVector<T, 3>& GetInsertion() const { return MPlane2.X(); }
		TVector<T, 3> GetCenter() const { return (MPlane1.X() + MPlane2.X()) * (T)0.5; }
		/** Returns the centroid (center of mass). */
		TVector<T, 3> GetCenterOfMass() const // centroid
		{
			const T R1R1 = MRadius1 * MRadius1;
			const T R2R2 = MRadius2 * MRadius2;
			const T R1R2 = MRadius1 * MRadius2;
			return TVector<T, 3>(0, 0, MHeight*(R1R1 + 2.*R1R2 + 3.*R2R2) / 4.*(R1R1 + R1R2 + R2R2));
		}
		TVector<T, 3> GetAxis() const { return (MPlane2.X() - MPlane1.X()).GetSafeNormal(); }

		T GetArea(const bool IncludeEndCaps = true) const { return GetArea(MHeight, MRadius1, MRadius2, IncludeEndCaps); }
		static T GetArea(const T Height, const T Radius1, const T Radius2, const bool IncludeEndCaps)
		{
			static const T TwoPI = PI * 2;
			if (Radius1 == Radius2)
			{
				const T TwoPIR1 = TwoPI * Radius1;
				return IncludeEndCaps ?
				    TwoPIR1 * Height + TwoPIR1 * Radius1 :
				    TwoPIR1 * Height;
			}
			else
			{
				const T R1_R2 = Radius1 - Radius2;
				const T CylArea = PI * (Radius1 + Radius2) * FMath::Sqrt((R1_R2 * R1_R2) + (Height * Height));
				return IncludeEndCaps ?
				    CylArea + PI * Radius1 * Radius1 + PI * Radius2 * Radius2 :
				    CylArea;
			}
		}

		T GetVolume() const { return GetVolume(MHeight, MRadius1, MRadius2); }
		static T GetVolume(const T Height, const T Radius1, const T Radius2)
		{
			static const T PI_3 = PI / 3;
			return PI_3 * Height * (Radius1 * Radius1 + Radius1 * Radius2 + Radius2 * Radius2);
		}

		PMatrix<T, 3, 3> GetInertiaTensor(const T Mass) const { return GetInertiaTensor(Mass, MHeight, MRadius1, MRadius2); }
		static PMatrix<T, 3, 3> GetInertiaTensor(const T Mass, const T Height, const T Radius1, const T Radius2)
		{
			// https://www.wolframalpha.com/input/?i=conical+frustum
			const T R1 = FMath::Min(Radius1, Radius2);
			const T R2 = FMath::Max(Radius1, Radius2);
			const T HH = Height * Height;
			const T R1R1 = R1 * R1;
			const T R1R2 = R1 * R2;
			const T R2R2 = R2 * R2;

			const T Num1 = 2. * HH * (R1R1 + 3. * R1R2 + 6. * R2R2); // 2H^2 * (R1^2 + 3R1R2 + 6R2^2)
			const T Num2 = 3. * (R1R1 * R1R1 + R1R1 * R1R2 + R1R2 * R1R2 + R1R2 * R2R2 + R2R2 * R2R2); // 3 * (R1^4 + R1^3R2 + R1^2R2^2 + R1R2^3 + R2^4)
			const T Den1 = PI * (R1R1 + R1R2 + R2R2); // PI * (R1^2 + R1R2 + R2^2)

			const T Diag12 = Mass * (Num1 + Num2) / (20. * Den1);
			const T Diag3 = Mass * Num2 / (10. * Den1);

			return PMatrix<T, 3, 3>(Diag12, Diag12, Diag3);
		}

		static TRotation<T, 3> GetRotationOfMass()
		{ return TRotation<T, 3>::FromIdentity(); }

		virtual uint32 GetTypeHash() const override
		{
			const uint32 PlaneHashes = HashCombine(MPlane1.GetTypeHash(), MPlane2.GetTypeHash());
			const uint32 PropertyHash = HashCombine(::GetTypeHash(MHeight), HashCombine(::GetTypeHash(MRadius1), ::GetTypeHash(MRadius2)));

			return HashCombine(PlaneHashes, PropertyHash);
		}

	private:
		//Phi is distance from closest point on plane1
		T GetRadius(const T& Phi) const
		{
			const T Alpha = Phi / MHeight;
			return MRadius1 * (1. - Alpha) + MRadius2 * Alpha;
		}

		TPlane<T, 3> MPlane1, MPlane2;
		T MHeight, MRadius1, MRadius2;
		TAABB<T, 3> MLocalBoundingBox;
	};

	template<typename T>
	struct TTaperedCylinderSpecializeSamplingHelper
	{
		static FORCEINLINE void ComputeSamplePoints(
		    TArray<TVector<T, 3>>& Points, const TTaperedCylinder<T>& Cylinder,
		    const int32 NumPoints, const bool IncludeEndCaps = true)
		{
			if (NumPoints <= 1 ||
			    (Cylinder.GetRadius1() <= KINDA_SMALL_NUMBER &&
			        Cylinder.GetRadius2() <= KINDA_SMALL_NUMBER))
			{
				const int32 Offset = Points.Num();
				if (Cylinder.GetHeight() <= KINDA_SMALL_NUMBER)
				{
					Points.SetNumUninitialized(Offset + 1);
					Points[Offset] = Cylinder.GetCenter();
				}
				else
				{
					Points.SetNumUninitialized(Offset + 3);
					Points[Offset + 0] = Cylinder.GetOrigin();
					Points[Offset + 1] = Cylinder.GetCenter();
					Points[Offset + 2] = Cylinder.GetInsertion();
				}
				return;
			}
			ComputeGoldenSpiralPoints(Points, Cylinder, NumPoints, IncludeEndCaps);
		}

		static FORCEINLINE void ComputeGoldenSpiralPoints(TArray<TVector<T, 3>>& Points, const TTaperedCylinder<T>& Cylinder, const int32 NumPoints, const bool IncludeEndCaps = true)
		{
			ComputeGoldenSpiralPoints(Points, Cylinder.GetOrigin(), Cylinder.GetAxis(), Cylinder.GetRadius1(), Cylinder.GetRadius2(), Cylinder.GetHeight(), NumPoints, IncludeEndCaps);
		}

		/**
		 * Use the golden spiral method to generate evenly spaced points on a tapered 
		 * cylinder (truncated cone).
		 *
		 * The "golden" part is derived from the golden ratio; stand at the center,
		 * turn a golden ratio of whole turns, then emit a point in that direction.
		 *
		 * Points are generated starting from the bottom of the cylinder, ending at 
		 * the top.  Contiguous entries in \p Points generally will not be spatially
		 * adjacent.
		 *
		 * \p Points to append to.
		 * \p Origin is the bottom-most point of the tapered cylinder.
		 * \p Axis is the orientation of the tapered cylinder.
		 * \p Radius1 is the first radius of the tapered cylinder.
		 * \p Radius2 is the second radius of the tapered cylinder.
		 * \p Height is the height of the tapered cylinder.
		 * \p NumPoints is the number of points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the tapered cylinder.
		 * \p SpiralSeed is the starting index for golden spiral generation.  When 
		 *    using this method to continue a spiral started elsewhere, \p SpiralSeed 
		 *    should equal the number of particles already created.
		 */
		static /*FORCEINLINE*/ void ComputeGoldenSpiralPoints(
		    TArray<TVector<T, 3>>& Points,
		    const TVector<T, 3>& Origin,
		    const TVector<T, 3>& Axis,
		    const T Radius1,
		    const T Radius2,
		    const T Height,
		    const int32 NumPoints,
		    const bool IncludeEndCaps = true,
		    int32 SpiralSeed = 0)
		{
			// Axis should be normalized.
			checkSlow(FMath::Abs(Axis.Size() - 1.0) < KINDA_SMALL_NUMBER);

			const int32 Offset = Points.Num();
			ComputeGoldenSpiralPointsUnoriented(Points, Radius1, Radius2, Height, NumPoints, IncludeEndCaps, SpiralSeed);

			// At this point, Points are centered about the origin (0,0,0), built
			// along the Z axis.  Transform them to where they should be.
			const T HalfHeight = Height / 2;
			const TRotation<float, 3> Rotation = TRotation<float, 3>::FromRotatedVector(TVector<float, 3>(0, 0, 1), Axis);
			checkSlow(((Origin + Axis * Height) - (Rotation.RotateVector(TVector<T, 3>(0, 0, Height)) + Origin)).Size() < KINDA_SMALL_NUMBER);
			for (int32 i = Offset; i < Points.Num(); i++)
			{
				TVector<T, 3>& Point = Points[i];
				const TVector<T, 3> PointNew = Rotation.RotateVector(Point + TVector<T, 3>(0, 0, HalfHeight)) + Origin;
//				checkSlow(FMath::Abs(TTaperedCylinder<T>(Origin, Origin + Axis * Height, Radius1, Radius2).SignedDistance(PointNew)) < KINDA_SMALL_NUMBER);
				Point = PointNew;
			}
		}

		/**
		 * Generates points on a tapered cylinder (truncated cone), oriented about 
		 * the Z axis, varying from [-Height/2, Height/2].
		 *
		 * TODO: Note that this method does not produce evenly spaced points!  It'll 
		 * bunch points together on the side of the cylinder with the smaller radius, 
		 * and spread them apart on the larger.  We need a routine that operates in 
		 * conical space, rather than cylindrical.  That said, points are distributed 
		 * evenly between the two end caps, proportional to their respective areas.
		 *
		 * The "golden" part is derived from the golden ratio; stand at the center,
		 * turn a golden ratio of whole turns, then emit a point in that direction.
		 *
		 * Points are generated starting from the bottom of the cylinder, ending at 
		 * the top.  Contiguous entries in \p Points generally will not be spatially
		 * adjacent.
		 *
		 * \p Points to append to.
		 * \p Radius1 is the first radius of the tapered cylinder.
		 * \p Radius2 is the second radius of the tapered cylinder.
		 * \p Height is the height of the cylinder.
		 * \p NumPoints is the number of points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the cylinder.
		 * \p SpiralSeed is the starting index for golden spiral generation.  When 
		 *    using this method to continue a spiral started elsewhere, \p SpiralSeed 
		 *    should equal the number of particles already created.
		 */
		static /*FORCEINLINE*/ void ComputeGoldenSpiralPointsUnoriented(
		    TArray<TVector<T, 3>>& Points,
		    const T Radius1,
		    const T Radius2,
		    const T Height,
		    const int32 NumPoints,
		    const bool IncludeEndCaps = true,
		    int32 SpiralSeed = 0)
		{
			// Evenly distribute points between the cylinder body and the end caps.
			int32 NumPointsEndCap1;
			int32 NumPointsEndCap2;
			int32 NumPointsCylinder;
			if (IncludeEndCaps)
			{
				const T Cap1Area = PI * Radius1 * Radius1;
				const T Cap2Area = PI * Radius2 * Radius2;
				const T CylArea =
				    PI * Radius2 * (Radius2 + FMath::Sqrt(Height * Height + Radius2 * Radius2)) -
				    PI * Radius1 * (Radius1 + FMath::Sqrt(Height * Height + Radius1 * Radius1));
				const T AllArea = CylArea + Cap1Area + Cap2Area;
				if (AllArea > KINDA_SMALL_NUMBER)
				{
					NumPointsEndCap1 = static_cast<int32>(round(Cap1Area / AllArea * NumPoints));
					NumPointsEndCap2 = static_cast<int32>(round(Cap2Area / AllArea * NumPoints));
					NumPointsCylinder = NumPoints - NumPointsEndCap1 - NumPointsEndCap2;
				}
				else
				{
					NumPointsCylinder = 0;
					NumPointsEndCap1 = NumPointsEndCap2 = (NumPoints - (NumPoints % 2)) / 2;
				}
			}
			else
			{
				NumPointsCylinder = NumPoints;
				NumPointsEndCap1 = 0;
				NumPointsEndCap2 = 0;
			}
			const int32 NumPointsToAdd = NumPointsCylinder + NumPointsEndCap1 + NumPointsEndCap2;
			Points.Reserve(Points.Num() + NumPointsToAdd);

			int32 Offset = Points.Num();
			const T HalfHeight = Height / 2;
			TArray<TVector<T, 2>> Points2D;
			Points2D.Reserve(NumPointsEndCap1);
			if (IncludeEndCaps)
			{
				TSphereSpecializeSamplingHelper<T, 2>::ComputeGoldenSpiralPoints(
				    Points2D, TVector<T, 2>((T)0.0), Radius1, NumPointsEndCap1, SpiralSeed);
				Offset = Points.AddUninitialized(Points2D.Num());
				for (int32 i = 0; i < Points2D.Num(); i++)
				{
					const TVector<T, 2>& Pt = Points2D[i];
					checkSlow(Pt.Size() < Radius1 + KINDA_SMALL_NUMBER);
					Points[i + Offset] = TVector<T, 3>(Pt[0], Pt[1], -HalfHeight);
				}
				// Advance the SpiralSeed by the number of points generated.
				SpiralSeed += Points2D.Num();
			}

			Offset = Points.AddUninitialized(NumPointsCylinder);
			if (NumPointsCylinder == 1)
			{
				Points[Offset] = TVector<T, 3>(0, 0, HalfHeight);
			}
			else
			{
				static const T Increment = PI * (1.0 + sqrt(5));
				for (int32 i = 0; i < NumPointsCylinder; i++)
				{
					// In the 2D sphere (disc) case, we vary R so it increases monotonically,
					// which spreads points out across the disc:
					//     const T R = FMath::Sqrt((0.5 + Index) / NumPoints) * Radius;
					// But we're mapping to a cylinder, which means we want to keep R constant.
					const T R = FMath::Lerp(Radius1, Radius2, static_cast<T>(i) / (NumPointsCylinder - 1));
					const T Theta = Increment * (0.5 + i + SpiralSeed);

					// Map polar coordinates to Cartesian, and vary Z by [-HalfHeight, HalfHeight].
					const T Z = FMath::LerpStable(-HalfHeight, HalfHeight, static_cast<T>(i) / (NumPointsCylinder - 1));
					Points[i + Offset] =
					    TVector<T, 3>(
					        R * FMath::Cos(Theta),
					        R * FMath::Sin(Theta),
					        Z);

					//checkSlow(FMath::Abs(TVector<T, 2>(Points[i + Offset][0], Points[i + Offset][1]).Size() - Radius) < KINDA_SMALL_NUMBER);
				}
			}
			// Advance the SpiralSeed by the number of points generated.
			SpiralSeed += NumPointsCylinder;

			if (IncludeEndCaps)
			{
				Points2D.Reset();
				TSphereSpecializeSamplingHelper<T, 2>::ComputeGoldenSpiralPoints(
				    Points2D, TVector<T, 2>((T)0.0), Radius2, NumPointsEndCap2, SpiralSeed);
				Offset = Points.AddUninitialized(Points2D.Num());
				for (int32 i = 0; i < Points2D.Num(); i++)
				{
					const TVector<T, 2>& Pt = Points2D[i];
					checkSlow(Pt.Size() < Radius2 + KINDA_SMALL_NUMBER);
					Points[i + Offset] = TVector<T, 3>(Pt[0], Pt[1], HalfHeight);
				}
			}
		}
	};

} // namespace Chaos
