// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Cylinder.h"
#include "Chaos/GJKShape.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Sphere.h"
#include "Chaos/Segment.h"
#include "ChaosArchive.h"

#include "UObject/ReleaseObjectVersion.h"

namespace Chaos
{
	struct FCapsuleSpecializeSamplingHelper;

	class FCapsule final : public FImplicitObject
	{
	public:
		using FImplicitObject::SignedDistance;
		using FImplicitObject::GetTypeName;

		FCapsule()
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Capsule)
		{}
		FCapsule(const FVec3& x1, const FVec3& x2, const FReal Radius)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Capsule)
			, MSegment(x1, x2)
		{
			SetRadius(Radius);
		}

		FCapsule(const FCapsule& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Capsule)
			, MSegment(Other.MSegment)
		{
			SetRadius(Other.GetRadius());
		}

		FCapsule(FCapsule&& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Capsule)
			, MSegment(MoveTemp(Other.MSegment))
		{
			SetRadius(Other.GetRadius());
		}

		FCapsule& operator=(FCapsule&& InSteal)
		{
			this->Type = InSteal.Type;
			this->bIsConvex = InSteal.bIsConvex;
			this->bDoCollide = InSteal.bDoCollide;
			this->bHasBoundingBox = InSteal.bHasBoundingBox;

			MSegment = MoveTemp(InSteal.MSegment);
			SetRadius(InSteal.GetRadius());

			return *this;
		}

		~FCapsule() {}

		static constexpr EImplicitObjectType StaticType() { return ImplicitObjectType::Capsule; }

		static FCapsule NewFromOriginAndAxis(const FVec3& Origin, const FVec3& Axis, const FReal Height, const FReal Radius)
		{
			auto X1 = Origin + Axis * Radius;
			auto X2 = Origin + Axis * (Radius + Height);
			return FCapsule(X1, X2, Radius);
		}

		FReal GetRadius() const
		{
			return Margin;
		}

		/**
		 * Returns sample points centered about the origin.
		 *
		 * \p NumPoints specifies how many points to generate.
		 */
		TArray<FVec3> ComputeLocalSamplePoints(const int32 NumPoints) const;

		/** 
		 * Returns sample points centered about the origin. 
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 */
		TArray<FVec3> ComputeLocalSamplePoints(const FReal PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeLocalSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea())), MinPoints, MaxPoints)); }

		/**
		 * Returns sample points at the current location of the cylinder.
		 */
		TArray<FVec3> ComputeSamplePoints(const int32 NumPoints) const;

		/** 
		 * Returns sample points at the current location of the cylinder.
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 */
		TArray<FVec3> ComputeSamplePoints(const FReal PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea())), MinPoints, MaxPoints)); }

		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const override
		{
			auto Dot = FMath::Clamp(FVec3::DotProduct(x - GetX1(), GetAxis()), (FReal)0., GetHeight());
			FVec3 ProjectedPoint = Dot * GetAxis() + GetX1();
			Normal = x - ProjectedPoint;
			return Normal.SafeNormalize() - GetRadius();
		}

		virtual const FAABB3 BoundingBox() const override
		{
			FAABB3 Box = MSegment.BoundingBox();
			Box.Thicken(GetRadius());
			return Box;
		}

		static bool RaycastFast(FReal MRadius, FReal MHeight, const FVec3& MVector, const FVec3& X1, const FVec3& X2, const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex)
		{
			ensure(FMath::IsNearlyEqual(MVector.SizeSquared(), 1, KINDA_SMALL_NUMBER));
			ensure(FMath::IsNearlyEqual(Dir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
			ensure(Length > 0);

			const FReal R = MRadius + Thickness;
			const FReal R2 = R * R;
			OutFaceIndex = INDEX_NONE;

			//First check if we are initially overlapping
			//Find closest point to cylinder core and check if it's inside the inflated capsule
			const FVec3 X1ToStart = StartPoint - X1;
			const FReal MVectorDotX1ToStart = FVec3::DotProduct(X1ToStart, MVector);
			if (MVectorDotX1ToStart >= -R && MVectorDotX1ToStart <= MHeight + R)
			{
				//projection is somewhere in the capsule. Clamp to cylinder length and check if inside sphere
				const FReal ClampedProjection = FMath::Clamp(MVectorDotX1ToStart, (FReal)0, MHeight);
				const FVec3 ClampedProjectionPosition = MVector * ClampedProjection;
				const FReal Dist2 = (X1ToStart - ClampedProjectionPosition).SizeSquared();
				if (Dist2 <= R2)
				{
					OutTime = 0;
					return true;
				}
			}

			// Raycast against cylinder first

			//let <x,y> denote x \dot y
			//cylinder implicit representation: ||((X - x1) \cross MVector)||^2 - R^2 = 0, where X is any point on the cylinder surface (only true because MVector is unit)
			//Using Lagrange's identity we get ||X-x1||^2 ||MVector||^2 - <MVector, X-x1>^2 - R^2 = ||X-x1||^2 - <MVector, X-x1>^2 - R^2 = 0
			//Then plugging the ray into X we have: ||StartPoint + t Dir - x1||^2 - <MVector, Start + t Dir - x1>^2 - R^2
			// = ||StartPoint-x1||^2 + t^2 + 2t <StartPoint-x1, Dir> - <MVector, StartPoint-x1>^2 - t^2 <MVector,Dir>^2 - 2t<MVector, StartPoint -x1><MVector, Dir> - R^2 = 0
			//Solving for the quadratic formula we get:
			//a = 1 - <MVector,Dir>^2	Note a = 0 implies MVector and Dir are parallel
			//b = 2(<StartPoint-x1, Dir> - <MVector, StartPoint - x1><MVector, Dir>)
			//c = ||StartPoint-x1||^2 - <MVector, StartPoint-x1>^2 - R^2 Note this tells us if start point is inside (c < 0) or outside (c > 0) of cylinder

			const FReal MVectorDotX1ToStart2 = MVectorDotX1ToStart * MVectorDotX1ToStart;
			const FReal MVectorDotDir = FVec3::DotProduct(MVector, Dir);
			const FReal MVectorDotDir2 = MVectorDotDir * MVectorDotDir;
			const FReal X1ToStartDotDir = FVec3::DotProduct(X1ToStart, Dir);
			const FReal X1ToStart2 = X1ToStart.SizeSquared();
			const FReal A = 1 - MVectorDotDir2;
			const FReal C = X1ToStart2 - MVectorDotX1ToStart2 - R2;

			constexpr FReal Epsilon = (FReal)1e-4;
			bool bCheckCaps = false;

			if (C <= 0.f)
			{
				// Inside cylinder so check caps
				bCheckCaps = true;
			}
			else
			{
				const FReal HalfB = (X1ToStartDotDir - MVectorDotX1ToStart * MVectorDotDir);
				const FReal QuarterUnderRoot = HalfB * HalfB - A * C;

				if (QuarterUnderRoot < 0)
				{
					bCheckCaps = true;
				}
				else
				{
					FReal Time;
					const bool bSingleHit = QuarterUnderRoot < Epsilon;
					if (bSingleHit)
					{
						Time = (A == 0) ? 0 : (-HalfB / A);

					}
					else
					{
						Time = (A == 0) ? 0 : ((-HalfB - FMath::Sqrt(QuarterUnderRoot)) / A); //we already checked for initial overlap so just take smallest time
						if (Time < 0)	//we must have passed the cylinder
						{
							return false;
						}
					}

					const FVec3 SpherePosition = StartPoint + Time * Dir;
					const FVec3 CylinderToSpherePosition = SpherePosition - X1;
					const FReal PositionLengthOnCoreCylinder = FVec3::DotProduct(CylinderToSpherePosition, MVector);
					if (PositionLengthOnCoreCylinder >= 0 && PositionLengthOnCoreCylinder < MHeight)
					{
						OutTime = Time;
						OutNormal = (CylinderToSpherePosition - MVector * PositionLengthOnCoreCylinder) / R;
						OutPosition = SpherePosition - OutNormal * Thickness;
						return true;
					}
					else
					{
						//if we have a single hit the ray is tangent to the cylinder.
						//the caps are fully contained in the infinite cylinder, so no need to check them
						bCheckCaps = !bSingleHit;
					}
				}
			}

			if (bCheckCaps)
			{
				//can avoid some work here, but good enough for now
				TSphere<FReal, 3> X1Sphere(X1, MRadius);
				TSphere<FReal, 3> X2Sphere(X2, MRadius);

				FReal Time1, Time2;
				FVec3 Position1, Position2;
				FVec3 Normal1, Normal2;
				bool bHitX1 = X1Sphere.Raycast(StartPoint, Dir, Length, Thickness, Time1, Position1, Normal1, OutFaceIndex);
				bool bHitX2 = X2Sphere.Raycast(StartPoint, Dir, Length, Thickness, Time2, Position2, Normal2, OutFaceIndex);

				if (bHitX1 && bHitX2)
				{
					if (Time1 <= Time2)
					{
						OutTime = Time1;
						OutPosition = Position1;
						OutNormal = Normal1;
					}
					else
					{
						OutTime = Time2;
						OutPosition = Position2;
						OutNormal = Normal2;
					}

					return true;
				}
				else if (bHitX1)
				{
					OutTime = Time1;
					OutPosition = Position1;
					OutNormal = Normal1;
					return true;
				}
				else if (bHitX2)
				{
					OutTime = Time2;
					OutPosition = Position2;
					OutNormal = Normal2;
					return true;
				}
			}

			return false;
		}

		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override
		{
			return RaycastFast(GetRadius(), GetHeight(), GetAxis(), GetX1(), GetX2(), StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
		}

		FORCEINLINE FVec3 Support(const FVec3& Direction, const FReal Thickness) const
		{
			return MSegment.Support(Direction, GetRadius() + Thickness);
		}

		FORCEINLINE FVec3 SupportCore(const FVec3& Direction, FReal InMargin) const
		{
			// NOTE: Ignores InMargin, assumes Radius
			return MSegment.SupportCore(Direction);
		}

		FORCEINLINE FVec3 SupportCoreScaled(const FVec3& Direction, FReal InMargin, const FVec3& Scale) const
		{
			// NOTE: Ignores InMargin, assumes Radius
			return SupportCore(Scale * Direction, GetMargin()) * Scale;
		}

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			FImplicitObject::SerializeImp(Ar);
			MSegment.Serialize(Ar);

			// Radius is now stored in the base class Margin
			FReal ArRadius = GetRadius();
			Ar << ArRadius;
			SetRadius(ArRadius);
			
			if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::CapsulesNoUnionOrAABBs)
			{
				FAABB3 DummyBox;	//no longer store this, computed on demand
				TBox<FReal,3>::SerializeAsAABB(Ar,DummyBox);
			}
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			SerializeImp(Ar);

			if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::CapsulesNoUnionOrAABBs)
			{
				TUniquePtr<FImplicitObjectUnion> TmpUnion;
				Ar << TmpUnion;
			}
		}

		virtual TUniquePtr<FImplicitObject> Copy() const override
		{
			return TUniquePtr<FImplicitObject>(new FCapsule(*this));
		}

		FReal GetHeight() const { return MSegment.GetLength(); }
		/** Returns the bottommost point on the capsule. */
		const FVec3 GetOrigin() const { return GetX1() + GetAxis() * -GetRadius(); }
		/** Returns the topmost point on the capsule. */
		const FVec3 GetInsertion() const { return GetX1() + GetAxis() * (GetHeight() + GetRadius()); }
		FVec3 GetCenter() const { return MSegment.GetCenter(); }
		/** Returns the centroid (center of mass). */
		FVec3 GetCenterOfMass() const { return GetCenter(); }
		const FVec3& GetAxis() const { return MSegment.GetAxis(); }
		const FVec3& GetX1() const { return MSegment.GetX1(); }
		FVec3 GetX2() const { return MSegment.GetX2(); }
		TSegment<FReal> GetSegment() const { return TSegment<FReal>(GetX1(), GetX2()); }

		FReal GetArea() const { return GetArea(GetHeight(), GetRadius()); }
		static FReal GetArea(const FReal Height, const FReal Radius) { static const FReal PI2 = 2. * PI; return PI2 * Radius * (Height + 2.*Radius); }

		FReal GetVolume() const { return GetVolume(GetHeight(), GetRadius()); }
		static FReal GetVolume(const FReal Height, const FReal Radius) { static const FReal FourThirds = 4. / 3; return PI * Radius*Radius * (Height + FourThirds * Radius); }

		FMatrix33 GetInertiaTensor(const FReal Mass) const { return GetInertiaTensor(Mass, GetHeight(), GetRadius()); }
		static FMatrix33 GetInertiaTensor(const FReal Mass, const FReal Height, const FReal Radius)
		{
			// https://www.wolframalpha.com/input/?i=capsule&assumption=%7B%22C%22,+%22capsule%22%7D+-%3E+%7B%22Solid%22%7D
			const FReal R = FMath::Clamp(Radius, (FReal)0., TNumericLimits<FReal>::Max());
			const FReal H = FMath::Clamp(Height, (FReal)0., TNumericLimits<FReal>::Max());
			const FReal RR = R * R;
			const FReal HH = H * H;

			// (5H^3 + 20*H^2R + 45HR^2 + 32R^3) / (60H + 80R)
			const FReal Diag12 = Mass * (5.*HH*H + 20.*HH*R + 45.*H*RR + 32.*RR*R) / (60.*H + 80.*R);
			// (R^2 * (15H + 16R) / (30H +40R))
			const FReal Diag3 = Mass * (RR * (15.*H + 16.*R)) / (30.*H + 40.*R);

			return FMatrix33(Diag12, Diag12, Diag3);
		}

		FRotation3 GetRotationOfMass() const { return GetRotationOfMass(GetAxis()); }
		static FRotation3 GetRotationOfMass(const FVec3& Axis)
		{
			// since the capsule stores an axis and the InertiaTensor is assumed to be along the ZAxis
			// we need to make sure to return the rotation of the axis from Z
			return FRotation3::FromRotatedVector(FVec3(0, 0, 1), Axis);
		}

		virtual uint32 GetTypeHash() const override
		{
			return HashCombine(::GetTypeHash(GetX1()), ::GetTypeHash(GetAxis()));
		}

	private:
		void SetRadius(FReal InRadius) { SetMargin(InRadius); }

		TSegment<FReal> MSegment;
	};

	struct FCapsuleSpecializeSamplingHelper
	{
		static FORCEINLINE void ComputeSamplePoints(TArray<FVec3>& Points, const FCapsule& Capsule, const int32 NumPoints)
		{
			if (NumPoints <= 1 || Capsule.GetRadius() <= SMALL_NUMBER)
			{
				const int32 Offset = Points.Num();
				if (Capsule.GetHeight() <= SMALL_NUMBER)
				{
					Points.SetNumUninitialized(Offset + 1);
					Points[Offset] = Capsule.GetCenter();
				}
				else
				{
					Points.SetNumUninitialized(Offset + 3);
					Points[0] = Capsule.GetOrigin();
					Points[1] = Capsule.GetCenter();
					Points[2] = Capsule.GetInsertion();
				}
				return;
			}
			ComputeGoldenSpiralPoints(Points, Capsule, NumPoints);
		}

		static FORCEINLINE void ComputeGoldenSpiralPoints(TArray<FVec3>& Points, const FCapsule& Capsule, const int32 NumPoints)
		{ ComputeGoldenSpiralPoints(Points, Capsule.GetOrigin(), Capsule.GetAxis(), Capsule.GetHeight(), Capsule.GetRadius(), NumPoints); }

		static FORCEINLINE void ComputeGoldenSpiralPoints(
		    TArray<FVec3>& Points,
		    const FVec3& Origin,
		    const FVec3& Axis,
		    const FReal Height,
		    const FReal Radius,
		    const int32 NumPoints)
		{
			// Axis should be normalized.
			checkSlow(FMath::Abs(Axis.Size() - 1.0) < KINDA_SMALL_NUMBER);

			// Evenly distribute points between the capsule body and the end caps.
			int32 NumPointsEndCap;
			int32 NumPointsCylinder;
			const FReal CapArea = 4 * PI * Radius * Radius;
			const FReal CylArea = 2.0 * PI * Radius * Height;
			if (CylArea > KINDA_SMALL_NUMBER)
			{
				const FReal AllArea = CylArea + CapArea;
				NumPointsCylinder = static_cast<int32>(round(CylArea / AllArea * NumPoints));
				NumPointsCylinder += (NumPoints - NumPointsCylinder) % 2;
				NumPointsEndCap = (NumPoints - NumPointsCylinder) / 2;
			}
			else
			{
				NumPointsCylinder = 0;
				NumPointsEndCap = (NumPoints - (NumPoints % 2)) / 2;
			}
			const int32 NumPointsToAdd = NumPointsCylinder + NumPointsEndCap * 2;
			Points.Reserve(Points.Num() + NumPointsToAdd);

			const int32 Offset = Points.Num();
			const FReal HalfHeight = Height / 2;
			{
				// Points vary in Z: [-Radius-HalfHeight, -HalfHeight]
				TSphereSpecializeSamplingHelper<FReal, 3>::ComputeBottomHalfSemiSphere(
				    Points, TSphere<FReal, 3>(FVec3(0, 0, -HalfHeight), Radius), NumPointsEndCap, Points.Num());
#if 0
				{
					TSphere<FReal, 3> Sphere(FVec3(0, 0, -HalfHeight), Radius);
					for(int32 i=Offset; i < Points.Num(); i++)
					{
						const FVec3& Pt = Points[i];
						const FReal Phi = Sphere.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
						checkSlow(Pt[2] > -Radius - HalfHeight - KINDA_SMALL_NUMBER && Pt[2] < -HalfHeight + KINDA_SMALL_NUMBER);
					}
				}
#endif
				// Points vary in Z: [-HalfHeight, HalfHeight], about the Z axis.
				FCylinderSpecializeSamplingHelper::ComputeGoldenSpiralPointsUnoriented(
				    Points, Radius, Height, NumPointsCylinder, false, Points.Num());
#if 0
				{
					TCylinder<FReal> Cylinder(FVec3(0, 0, -HalfHeight), FVec3(0, 0, HalfHeight), Radius);
					for(int32 i=TmpOffset; i < Points.Num(); i++)
					{
						const FVec3& Pt = Points[i];
						const FReal Phi = Cylinder.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
						checkSlow(Pt[2] > -HalfHeight - KINDA_SMALL_NUMBER && Pt[2] < HalfHeight + KINDA_SMALL_NUMBER);
					}
				}
#endif
				// Points vary in Z: [HalfHeight, HalfHeight+Radius]
				TSphereSpecializeSamplingHelper<FReal, 3>::ComputeTopHalfSemiSphere(
				    Points, TSphere<FReal, 3>(FVec3(0, 0, HalfHeight), Radius), NumPointsEndCap, Points.Num());
#if 0
				{
					TSphere<FReal, 3> Sphere(FVec3(0, 0, HalfHeight), Radius);
					for(int32 i=TmpOffset; i < Points.Num(); i++)
					{
						const FVec3& Pt = Points[i];
						const FReal Phi = Sphere.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
						checkSlow(Pt[2] > HalfHeight - KINDA_SMALL_NUMBER && Pt[2] < HalfHeight + Radius + KINDA_SMALL_NUMBER);
					}
				}
#endif
#if 0
				{
					FCapsule(FVec3(0, 0, -HalfHeight), FVec3(0, 0, HalfHeight), Radius);
					for(int32 i=Offset; i < Points.Num(); i++)
					{
						const FVec3& Pt = Points[i];
						const FReal Phi = Cylinder.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
					}
				}
#endif
			}

			const FRotation3 Rotation = FRotation3::FromRotatedVector(FVec3(0, 0, 1), Axis);
			checkSlow(((Origin + Axis * (Height + Radius * 2)) - (Rotation.RotateVector(FVec3(0, 0, Height + Radius * 2)) + Origin)).Size() < KINDA_SMALL_NUMBER);
			for (int32 i = Offset; i < Points.Num(); i++)
			{
				FVec3& Point = Points[i];
				const FVec3 PointNew = Rotation.RotateVector(Point + FVec3(0, 0, HalfHeight + Radius)) + Origin;
				checkSlow(FMath::Abs(FCapsule::NewFromOriginAndAxis(Origin, Axis, Height, Radius).SignedDistance(PointNew)) < KINDA_SMALL_NUMBER);
				Point = PointNew;
			}
		}
	};

	FORCEINLINE TArray<FVec3> FCapsule::ComputeLocalSamplePoints(const int32 NumPoints) const
	{
		TArray<FVec3> Points;
		const FVec3 Mid = GetCenter();
		const FCapsule Capsule(GetX1() - Mid, GetX1() + (GetAxis() * GetHeight()) - Mid, GetRadius());
		FCapsuleSpecializeSamplingHelper::ComputeSamplePoints(Points, Capsule, NumPoints);
		return Points;
	}

	FORCEINLINE TArray<FVec3> FCapsule::ComputeSamplePoints(const int32 NumPoints) const
	{
		TArray<FVec3> Points;
		FCapsuleSpecializeSamplingHelper::ComputeSamplePoints(Points, *this, NumPoints);
		return Points;
	}

	template<class T>
	using TCapsule = FCapsule; // AABB<> is still using TCapsule<> so no deprecation message for now

	template<class T>
	using TCapsuleSpecializeSamplingHelper UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FCapsuleSpecializeSamplingHelper instead") = FCapsuleSpecializeSamplingHelper;

} // namespace Chaos
