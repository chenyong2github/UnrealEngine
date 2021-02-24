// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Core.h"

namespace Chaos
{
	template<typename T>
	struct TTaperedCapsuleSpecializeSamplingHelper;

	template<class T>
	class TTaperedCapsule: public FImplicitObject
	{
	public:
		TTaperedCapsule()
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCapsule)
		{
			this->bIsConvex = true;
		}
		TTaperedCapsule(const TVec3<T>& X1, const TVec3<T>& X2, const T Radius1, const T Radius2)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCapsule)
		    , Origin(X1)
		    , Axis((X2 - X1).GetSafeNormal())
		    , Height((X2 - X1).Size())
		    , Radius1(Radius1)
		    , Radius2(Radius2)
		    , LocalBoundingBox(X1, X1)
		{
			this->bIsConvex = true;
			LocalBoundingBox.GrowToInclude(X2);
			T MaxRadius = FMath::Max(Radius1, Radius2);
			LocalBoundingBox = TAABB<T, 3>(LocalBoundingBox.Min() - TVec3<T>(MaxRadius), LocalBoundingBox.Max() + TVec3<T>(MaxRadius));
		}
		TTaperedCapsule(const TTaperedCapsule<T>& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCapsule)
		    , Origin(Other.Origin)
		    , Axis(Other.Axis)
		    , Height(Other.Height)
		    , Radius1(Other.Radius1)
		    , Radius2(Other.Radius2)
		    , LocalBoundingBox(Other.LocalBoundingBox)
		{
			this->bIsConvex = true;
		}
		TTaperedCapsule(TTaperedCapsule<T>&& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::TaperedCapsule)
		    , Origin(MoveTemp(Other.Origin))
		    , Axis(MoveTemp(Other.Axis))
		    , Height(Other.Height)
		    , Radius1(Other.Radius1)
		    , Radius2(Other.Radius2)
		    , LocalBoundingBox(MoveTemp(Other.LocalBoundingBox))
		{
			this->bIsConvex = true;
		}
		~TTaperedCapsule() {}

		static constexpr EImplicitObjectType StaticType() { return ImplicitObjectType::TaperedCapsule; }

		/**
		 * Returns sample points centered about the origin.
		 *
		 * \p NumPoints specifies how many points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the capsule.
		 */
		TArray<TVec3<T>> ComputeLocalSamplePoints(const int32 NumPoints) const
		{
			TArray<TVec3<T>> Points;
			TTaperedCapsuleSpecializeSamplingHelper<T>::ComputeSamplePoints(
			    Points,
			    TTaperedCapsule<T>(Origin, Origin + Axis * Height, GetRadius1(), GetRadius2()),
			    NumPoints);
			return Points;
		}
		/** 
		 * Returns sample points centered about the origin. 
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the capsule.
		 */
		TArray<TVec3<T>> ComputeLocalSamplePoints(const T PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ 
			return ComputeLocalSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea(true))), MinPoints, MaxPoints)); 
		}

		/**
		 * Returns sample points at the current location of the capsule.
		 *
		 * \p NumPoints specifies how many points to generate.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the capsule.
		 */
		TArray<TVec3<T>> ComputeSamplePoints(const int32 NumPoints)
		{
			TArray<TVec3<T>> Points;
			TTaperedCapsuleSpecializeSamplingHelper<T>::ComputeSamplePoints(Points, *this, NumPoints);
			return Points;
		}
		/** 
		 * Returns sample points at the current location of the capsule.
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 * \p IncludeEndCaps determines whether or not points are generated on the 
		 *    end caps of the capsule.
		 */
		TArray<TVec3<T>> ComputeSamplePoints(const T PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea(true))), MinPoints, MaxPoints)); }

		virtual const TAABB<T, 3> BoundingBox() const override { return LocalBoundingBox; }

		T PhiWithNormal(const TVec3<T>& x, TVec3<T>& OutNormal) const
		{
			const TVec3<T> OriginToX = x - Origin;
			const T DistanceAlongAxis = FMath::Clamp(TVec3<T>::DotProduct(OriginToX, Axis), (T)0.0, Height);
			const TVec3<T> ClosestPoint = Origin + Axis * DistanceAlongAxis;
			const T Radius = (Height > SMALL_NUMBER) ? FMath::Lerp(Radius1, Radius2, DistanceAlongAxis / Height) : FMath::Max(Radius1, Radius2);
			OutNormal = (x - ClosestPoint);
			const T NormalSize = OutNormal.SafeNormalize();
			return NormalSize - Radius;
		}

		T GetRadius1() const { return Radius1; }
		T GetRadius2() const { return Radius2; }
		T GetHeight() const { return Height; }
		T GetSlantHeight() const { const T R1mR2 = Radius1-Radius2; return FMath::Sqrt(R1mR2*R1mR2 + Height*Height); }
		TVec3<T> GetX1() const { return Origin; }
		TVec3<T> GetX2() const { return Origin + Axis * Height; }
		/** Returns the bottommost hemisphere center of the capsule. */
		TVec3<T> GetOrigin() const { return GetX1(); }
		/** Returns the topmost hemisphere center of capsule . */
		TVec3<T> GetInsertion() const { return GetX2(); }
		TVec3<T> GetCenter() const { return Origin + Axis * (Height * (T)0.5); }
		/** Returns the centroid (center of mass). */
		TVec3<T> GetCenterOfMass() const // centroid
		{
			const T R1R1 = Radius1 * Radius1;
			const T R2R2 = Radius2 * Radius2;
			const T R1R2 = Radius1 * Radius2;
			//  compute center of mass as a distance along the axis from the origin as the shape as the axis as a symmetry line 
			T TaperedSectionCenterOfMass = (Height * (R1R1 + 2.*R1R2 + 3.*R2R2) / 4.*(R1R1 + R1R2 + R2R2));
			T Hemisphere1CenterOfMass = -((T)3.0 * Radius1 / (T)8.0);
			T Hemisphere2CenterOfMass = (Height + ((T)3.0 * Radius2 / (T)8.0));

			// we need to combine all 3 using relative volume ratios
			const T TaperedSectionVolume = GetTaperedSectionVolume(Height, Radius1, Radius2);
			const T Hemisphere1Volume = GetHemisphereVolume(Radius1);
			const T Hemisphere2Volume = GetHemisphereVolume(Radius2);
			const T TotalVolume = TaperedSectionVolume + Hemisphere1Volume + Hemisphere2Volume;

			const T TotalCenterOfMassAlongAxis = ((TaperedSectionCenterOfMass * TaperedSectionVolume) + (Hemisphere1CenterOfMass * Hemisphere1Volume) + (Hemisphere2CenterOfMass * Hemisphere2Volume)) / TotalVolume;
			return TVec3<T>(0,0,1) * TotalCenterOfMassAlongAxis; 
		}
		TVec3<T> GetAxis() const { return Axis; }

		T GetArea(const bool IncludeEndCaps = true) const { return GetArea(Height, Radius1, Radius2, IncludeEndCaps); }
		static T GetArea(const T Height, const T Radius1, const T Radius2, const bool IncludeEndCaps)
		{
			static const T TwoPI = PI * 2;
			T AreaNoCaps = (T)0.0;
			if (Radius1 == Radius2)
			{
				AreaNoCaps = TwoPI * Radius1 * Height;
			}
			else
			{
				const T R1_R2 = Radius1 - Radius2;
				AreaNoCaps = PI * (Radius1 + Radius2) * FMath::Sqrt((R1_R2 * R1_R2) + (Height * Height));
			}
			if (IncludeEndCaps)
			{
				const T Hemisphere1Area = TSphere<T, 3>::GetArea(Radius1) / (FReal)2.;
				const T Hemisphere2Area = TSphere<T, 3>::GetArea(Radius2) / (FReal)2.;
				return AreaNoCaps + Hemisphere1Area + Hemisphere2Area;
			}
			return AreaNoCaps;
		}

		T GetVolume() const { return GetVolume(Height, Radius1, Radius2); }
		static T GetVolume(const T Height, const T Radius1, const T Radius2)
		{
			const T TaperedSectionVolume = GetTaperedSectionVolume(Height, Radius1, Radius2);
			const T Hemisphere1Volume = GetHemisphereVolume(Radius1);
			const T Hemisphere2Volume = GetHemisphereVolume(Radius2);
			return TaperedSectionVolume + Hemisphere1Volume + Hemisphere2Volume;
		}

		PMatrix<T, 3, 3> GetInertiaTensor(const T Mass) const { return GetInertiaTensor(Mass, Height, Radius1, Radius2); }
		static PMatrix<T, 3, 3> GetInertiaTensor(const T Mass, const T Height, const T Radius1, const T Radius2)
		{
			// TODO(chaos) : we should actually take hemispheres in account 
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

		TRotation<T, 3> GetRotationOfMass() const { return GetRotationOfMass(GetAxis()); }
		static TRotation<T, 3> GetRotationOfMass(const TVec3<T>& Axis)
		{
			// since the capsule stores an axis and the InertiaTensor is assumed to be along the ZAxis
			// we need to make sure to return the rotation of the axis from Z
			return TRotation<T, 3>::FromRotatedVector(TVec3<T>(0, 0, 1), Axis);
		}

		virtual uint32 GetTypeHash() const override
		{
			const uint32 OriginAxisHash = HashCombine(::GetTypeHash(Origin), ::GetTypeHash(Axis));
			const uint32 PropertyHash = HashCombine(::GetTypeHash(Height), HashCombine(::GetTypeHash(Radius1), ::GetTypeHash(Radius2)));

			return HashCombine(OriginAxisHash, PropertyHash);
		}

	private:
		//Phi is distance from closest point on plane1
		T GetRadius(const T& Phi) const
		{
			const T Alpha = Phi / Height;
			return FMath::Lerp(Radius1, Radius2, Alpha);
		}

		static T GetHemisphereVolume(const T Radius)
		{
			return (T)2.0 * PI * (Radius * Radius * Radius) / (T)3.0;
		}

		static T GetTaperedSectionVolume(const T Height, const T Radius1, const T Radius2)
		{
			static const T PI_OVER_3 = PI / (T)3.0;
			return PI_OVER_3 * Height * (Radius1 * Radius1 + Radius1 * Radius2 + Radius2 * Radius2);
		}

		TVec3<T> Origin, Axis;
		T Height, Radius1, Radius2;
		TAABB<T, 3> LocalBoundingBox;
	};

	template<typename T>
	struct TTaperedCapsuleSpecializeSamplingHelper
	{
		static FORCEINLINE void ComputeSamplePoints(
		    TArray<TVec3<T>>& Points, const TTaperedCapsule<T>& Capsule,
		    const int32 NumPoints)
		{
			if (NumPoints <= 1 ||
			    (Capsule.GetRadius1() <= KINDA_SMALL_NUMBER &&
				 Capsule.GetRadius2() <= KINDA_SMALL_NUMBER))
			{
				const int32 Offset = Points.Num();
				if (Capsule.GetHeight() <= KINDA_SMALL_NUMBER)
				{
					Points.SetNumUninitialized(Offset + 1);
					Points[Offset] = Capsule.GetCenter();
				}
				else
				{
					Points.SetNumUninitialized(Offset + 3);
					Points[Offset + 0] = Capsule.GetOrigin();
					Points[Offset + 1] = Capsule.GetCenter();
					Points[Offset + 2] = Capsule.GetInsertion();
				}
				return;
			}
			ComputeGoldenSpiralPoints(Points, Capsule, NumPoints);
		}

		static FORCEINLINE void ComputeGoldenSpiralPoints(TArray<TVec3<T>>& Points, const TTaperedCapsule<T>& Capsule, const int32 NumPoints)
		{
			ComputeGoldenSpiralPoints(Points, Capsule.GetOrigin(), Capsule.GetAxis(), Capsule.GetRadius1(), Capsule.GetRadius2(), Capsule.GetHeight(), NumPoints);
		}

		/**
		 * Use the golden spiral method to generate evenly spaced points on a tapered 
		 * capsule (truncated cone with two hemispherical ends).
		 *
		 * The "golden" part is derived from the golden ratio; stand at the center,
		 * turn a golden ratio of whole turns, then emit a point in that direction.
		 *
		 * Points are generated starting from the bottom of the tapered capsule part, ending at 
		 * the top.  Contiguous entries in \p Points generally will not be spatially
		 * adjacent.
		 *
		 * \p Points to append to.
		 * \p Origin is the bottom-most point of the tapered capsule.
		 * \p Axis is the orientation of the tapered capsule.
		 * \p Radius1 is the first radius of the tapered capsule.
		 * \p Radius2 is the second radius of the tapered capsule.
		 * \p Height is the height of the tapered capsule.
		 * \p NumPoints is the number of points to generate.
		 * \p SpiralSeed is the starting index for golden spiral generation.  When 
		 *    using this method to continue a spiral started elsewhere, \p SpiralSeed 
		 *    should equal the number of particles already created.
		 */
		static /*FORCEINLINE*/ void ComputeGoldenSpiralPoints(
		    TArray<TVec3<T>>& Points,
		    const TVec3<T>& Origin,
		    const TVec3<T>& Axis,
		    const T Radius1,
		    const T Radius2,
		    const T Height,
		    const int32 NumPoints,
			const int32 SpiralSeed = 0)
		{
			// Axis should be normalized.
			checkSlow(FMath::Abs(Axis.Size() - 1.0) < KINDA_SMALL_NUMBER);

			const int32 Offset = Points.Num();
			ComputeGoldenSpiralPointsUnoriented(Points, Radius1, Radius2, Height, NumPoints, SpiralSeed);

			// At this point, Points are centered about the origin (0,0,0), built
			// along the Z axis.  Transform them to where they should be.
			const T HalfHeight = Height / 2;
			const TRotation<T, 3> Rotation = TRotation<T, 3>::FromRotatedVector(TVec3<T>(0, 0, 1), Axis);
			checkSlow(((Origin + Axis * Height) - (Rotation.RotateVector(TVec3<T>(0, 0, Height)) + Origin)).Size() < KINDA_SMALL_NUMBER);
			for (int32 i = Offset; i < Points.Num(); i++)
			{
				TVec3<T>& Point = Points[i];
				const TVec3<T> PointNew = Rotation.RotateVector(Point + TVec3<T>(0, 0, HalfHeight)) + Origin;
//				checkSlow(FMath::Abs(TTaperedCapsule<T>(Origin, Origin + Axis * Height, Radius1, Radius2).SignedDistance(PointNew)) < KINDA_SMALL_NUMBER);
				Point = PointNew;
			}
		}

		/**
		 * Generates points on a tapered capsule (truncated cone), oriented about 
		 * the Z axis, varying from [-Height/2, Height/2].
		 *
		 * TODO: Note that this method does not produce evenly spaced points!  It'll 
		 * bunch points together on the side of the capsule with the smaller radius, 
		 * and spread them apart on the larger.  We need a routine that operates in 
		 * conical space, rather than cylindrical.  That said, points are distributed 
		 * evenly between the two end caps, proportional to their respective areas.
		 *
		 * The "golden" part is derived from the golden ratio; stand at the center,
		 * turn a golden ratio of whole turns, then emit a point in that direction.
		 *
		 * Points are generated starting from the bottom of the capsule, ending at 
		 * the top.  Contiguous entries in \p Points generally will not be spatially
		 * adjacent.
		 *
		 * \p Points to append to.
		 * \p Radius1 is the first radius of the tapered capsule.
		 * \p Radius2 is the second radius of the tapered capsule.
		 * \p Height is the height of the capsule.
		 * \p NumPoints is the number of points to generate.
		 * \p SpiralSeed is the starting index for golden spiral generation.  When 
		 *    using this method to continue a spiral started elsewhere, \p SpiralSeed 
		 *    should equal the number of particles already created.
		 */
		static /*FORCEINLINE*/ void ComputeGoldenSpiralPointsUnoriented(
		    TArray<TVec3<T>>& Points,
		    const T Radius1,
		    const T Radius2,
		    const T Height,
		    const int32 NumPoints,
			const int32 SpiralSeed = 0
		)
		{
			// Evenly distribute points between the capsule body and the end caps.
			int32 NumPointsEndCap1;
			int32 NumPointsEndCap2;
			int32 NumPointsTaperedSection;

			const T Cap1Area = TSphere<T, 3>::GetArea(Radius1) / (FReal)2.;
			const T Cap2Area = TSphere<T, 3>::GetArea(Radius2) / (FReal)2.;
			const T TaperedSectionArea = TTaperedCapsule<T>::GetArea(Height, Radius1, Radius2, /*IncludeEndCaps*/ false);
			const T AllArea = TaperedSectionArea + Cap1Area + Cap2Area;
			if (AllArea > KINDA_SMALL_NUMBER)
			{
				NumPointsEndCap1 = static_cast<int32>(round(Cap1Area / AllArea * NumPoints));
				NumPointsEndCap2 = static_cast<int32>(round(Cap2Area / AllArea * NumPoints));
				NumPointsTaperedSection = NumPoints - NumPointsEndCap1 - NumPointsEndCap2;
			}
			else
			{
				NumPointsTaperedSection = 0;
				NumPointsEndCap1 = NumPointsEndCap2 = (NumPoints - (NumPoints % 2)) / 2;
			}
			
			const int32 NumPointsToAdd = NumPointsTaperedSection + NumPointsEndCap1 + NumPointsEndCap2;
			Points.Reserve(Points.Num() + NumPointsToAdd);

			int32 Offset = Points.Num();
			const T HalfHeight = Height / 2;
			{
				// Points vary in Z: [-Radius1-HalfHeight, -HalfHeight]
				TSphereSpecializeSamplingHelper<T, 3>::ComputeBottomHalfSemiSphere(
					Points, TSphere<T, 3>(TVec3<T>(0, 0, -HalfHeight), Radius1), NumPointsEndCap1, SpiralSeed);
				SpiralSeed += Points.Num();

				// Points vary in Z: [-HalfHeight, HalfHeight], about the Z axis.
				TTaperedCylinderSpecializeSamplingHelper<T>::ComputeGoldenSpiralPointsUnoriented(
					Points, Radius1, Radius2, Height, NumPointsTaperedSection, false, SpiralSeed);
				SpiralSeed += Points.Num();

				// Points vary in Z: [HalfHeight, HalfHeight+Radius2]
				TSphereSpecializeSamplingHelper<T, 3>::ComputeTopHalfSemiSphere(
					Points, TSphere<T, 3>(TVec3<T>(0, 0, HalfHeight), Radius2), NumPointsEndCap2, SpiralSeed);
				SpiralSeed += Points.Num();
			}
		}
	};

} // namespace Chaos
