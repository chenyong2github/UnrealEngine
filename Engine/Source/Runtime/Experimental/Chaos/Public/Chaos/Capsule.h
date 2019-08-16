// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Cylinder.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Sphere.h"

namespace Chaos
{
	template<typename T>
	struct TCapsuleSpecializeSamplingHelper;

	template<class T>
	class TCapsule final : public TImplicitObject<T, 3>
	{
	public:
		using TImplicitObject<T, 3>::SignedDistance;
		TCapsule()
		    : TImplicitObject<T, 3>(EImplicitObject::FiniteConvex, ImplicitObjectType::Capsule)
		{}
		TCapsule(const TVector<T, 3>& x1, const TVector<T, 3>& x2, const T Radius)
		    : TImplicitObject<T, 3>(EImplicitObject::FiniteConvex, ImplicitObjectType::Capsule)
		    , MPoint(x1)
		    , MVector(x2 - x1)
		    , MHeight(MVector.SafeNormalize())
		    , MRadius(Radius)
		    , MLocalBoundingBox(x1, x1)
		    , MUnionedObjects(nullptr)
		{
			MLocalBoundingBox.GrowToInclude(x2);
			MLocalBoundingBox = TBox<T, 3>(MLocalBoundingBox.Min() - TVector<T, 3>(MRadius), MLocalBoundingBox.Max() + TVector<T, 3>(MRadius));
			InitUnionedObjects();
		}

		TCapsule(const TCapsule<T>& Other)
		    : TImplicitObject<T, 3>(EImplicitObject::FiniteConvex)
		    , MPoint(Other.MPoint)
		    , MVector(Other.MVector)
		    , MHeight(Other.MHeight)
		    , MRadius(Other.MRadius)
		    , MLocalBoundingBox(Other.MLocalBoundingBox)
		    , MUnionedObjects(nullptr)
		{
			InitUnionedObjects();
		}

		TCapsule(TCapsule<T>&& Other)
		    : TImplicitObject<T, 3>(EImplicitObject::FiniteConvex)
		    , MPoint(MoveTemp(Other.MPoint))
		    , MVector(MoveTemp(Other.MVector))
		    , MHeight(Other.MHeight)
		    , MRadius(Other.MRadius)
		    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
		    , MUnionedObjects(MoveTemp(Other.MUnionedObjects))
		{
			InitUnionedObjects();
		}

		TCapsule& operator=(TCapsule<T>&& InSteal)
		{
			this->Type = InSteal.Type;
			this->bIsConvex = InSteal.bIsConvex;
			this->bIgnoreAnalyticCollisions = InSteal.bIgnoreAnalyticCollisions;
			this->bHasBoundingBox = InSteal.bHasBoundingBox;

			MPoint = MoveTemp(InSteal.MPoint);
			MVector = MoveTemp(InSteal.MVector);
			MHeight = InSteal.MHeight;
			MRadius = InSteal.MRadius;
			MLocalBoundingBox = MoveTemp(InSteal.MLocalBoundingBox);
			MUnionedObjects = MoveTemp(InSteal.MUnionedObjects);

			InitUnionedObjects();

			return *this;
		}

		~TCapsule() {}

		static ImplicitObjectType GetType() { return ImplicitObjectType::Capsule; }

		static TCapsule<T> NewFromOriginAndAxis(const TVector<T, 3>& Origin, const TVector<T, 3>& Axis, const T Height, const T Radius)
		{
			auto X1 = Origin + Axis * Radius;
			auto X2 = Origin + Axis * (Radius + Height);
			return TCapsule<T>(X1, X2, Radius);
		}

		/**
		 * Returns sample points centered about the origin.
		 *
		 * \p NumPoints specifies how many points to generate.
		 */
		TArray<TVector<T, 3>> ComputeLocalSamplePoints(const int32 NumPoints) const
		{
			TArray<TVector<T, 3>> Points;
			const TVector<T, 3> Mid = GetCenter();
			const TCapsule<T> Capsule(MPoint - Mid, MPoint + (MVector * MHeight) - Mid, GetRadius());
			TCapsuleSpecializeSamplingHelper<T>::ComputeSamplePoints(Points, Capsule, NumPoints);
			return Points;
		}
		/** 
		 * Returns sample points centered about the origin. 
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 */
		TArray<TVector<T, 3>> ComputeLocalSamplePoints(const T PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeLocalSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea())), MinPoints, MaxPoints)); }

		/**
		 * Returns sample points at the current location of the cylinder.
		 */
		TArray<TVector<T, 3>> ComputeSamplePoints(const int32 NumPoints) const
		{
			TArray<TVector<T, 3>> Points;
			TCapsuleSpecializeSamplingHelper<T>::ComputeSamplePoints(Points, *this, NumPoints);
			return Points;
		}
		/** 
		 * Returns sample points at the current location of the cylinder.
		 *
		 * \p PointsPerUnitArea specifies how many points to generate per square 
		 *    unit (cm). 0.5 would generate 1 point per 2 square cm.
		 */
		TArray<TVector<T, 3>> ComputeSamplePoints(const T PointsPerUnitArea, const int32 MinPoints = 0, const int32 MaxPoints = 1000) const
		{ return ComputeSamplePoints(FMath::Clamp(static_cast<int32>(ceil(PointsPerUnitArea * GetArea())), MinPoints, MaxPoints)); }

		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const override
		{
			auto Dot = FMath::Clamp(TVector<T, 3>::DotProduct(x - MPoint, MVector), (T)0., MHeight);
			TVector<T, 3> ProjectedPoint = Dot * MVector + MPoint;
			Normal = x - ProjectedPoint;
			return Normal.SafeNormalize() - MRadius;
		}

		virtual const TBox<T, 3>& BoundingBox() const override { return MLocalBoundingBox; }

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex) const override
		{
			ensure(FMath::IsNearlyEqual(MVector.SizeSquared(), 1, KINDA_SMALL_NUMBER));
			ensure(FMath::IsNearlyEqual(Dir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
			ensure(Length > 0);

			const T R = MRadius + Thickness;
			const T R2 = R * R;
			OutFaceIndex = INDEX_NONE;

			//First check if we are initially overlapping
			//Find closest point to cylinder core and check if it's inside the inflated capsule
			const TVector<T, 3> X1 = GetX1();
			const TVector<T, 3> X1ToStart = StartPoint - X1;
			const T MVectorDotX1ToStart = TVector<T, 3>::DotProduct(X1ToStart, MVector);
			if (MVectorDotX1ToStart >= -R && MVectorDotX1ToStart <= MHeight + R)
			{
				//projection is somewhere in the capsule. Clamp to cylinder length and check if inside sphere
				const T ClampedProjection = FMath::Clamp(MVectorDotX1ToStart, (T)0, MHeight);
				const TVector<T, 3> ClampedProjectionPosition = MVector * ClampedProjection;
				const T Dist2 = (X1ToStart - ClampedProjectionPosition).SizeSquared();
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

			const T MVectorDotX1ToStart2 = MVectorDotX1ToStart * MVectorDotX1ToStart;
			const T MVectorDotDir = TVector<T, 3>::DotProduct(MVector, Dir);
			const T MVectorDotDir2 = MVectorDotDir * MVectorDotDir;
			const T X1ToStartDotDir = TVector<T, 3>::DotProduct(X1ToStart, Dir);
			const T X1ToStart2 = X1ToStart.SizeSquared();
			const T A = 1 - MVectorDotDir2;
			const T C = X1ToStart2 - MVectorDotX1ToStart2 - R2;

			constexpr T Epsilon = 1e-4;
			bool bCheckCaps = false;

			if (A < Epsilon)
			{
				//Parallel and inside cylinder so check caps
				bCheckCaps = C <= 0;
			}
			else
			{
				const T HalfB = (X1ToStartDotDir - MVectorDotX1ToStart * MVectorDotDir);
				const T QuarterUnderRoot = HalfB * HalfB - A * C;

				if (QuarterUnderRoot < 0)
				{
					bCheckCaps = true;
				}
				else 
				{
					T Time;
					const bool bSingleHit = QuarterUnderRoot < Epsilon;
					if (bSingleHit)
					{
						Time = -HalfB / A;
						
					}
					else
					{
						Time = (-HalfB - FMath::Sqrt(QuarterUnderRoot)) / A; //we already checked for initial overlap so just take smallest time
						if (Time < 0)	//we must have passed the cylinder
						{
							return false;
						}
					}

					const TVector<T, 3> SpherePosition = StartPoint + Time * Dir;
					const TVector<T, 3> CylinderToSpherePosition = SpherePosition - X1;
					const T PositionLengthOnCoreCylinder = TVector<T, 3>::DotProduct(CylinderToSpherePosition, MVector);
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
				TSphere<T,3> X1Sphere(X1, MRadius);
				TSphere<T, 3> X2Sphere(GetX2(), MRadius);

				T Time1, Time2;
				TVector<T, 3> Position1, Position2;
				TVector<T, 3> Normal1, Normal2;
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

		TVector<T,3> Support(const TVector<T, 3>& Direction, const T Thickness) const override
		{
			const T Dot = TVector<T, 3>::DotProduct(Direction, MVector);
			const TVector<T, 3> FarthestCap = Dot >= 0 ? GetX2() : GetX1();	//orthogonal we choose either
			//We want N / ||N|| and to avoid inf
			//So we want N / ||N|| < 1 / eps => N eps < ||N||, but this is clearly true for all eps < 1 and N > 0
			T SizeSqr = Direction.SizeSquared();
			if (SizeSqr <= TNumericLimits<T>::Min())
			{
				return FarthestCap;
			}
			const TVector<T, 3> Normalized = Direction / sqrt(SizeSqr);
			return FarthestCap + Normalized * (MRadius + Thickness);
		}

		const T& GetRadius() const { return MRadius; }
		const T& GetHeight() const { return MHeight; }
		/** Returns the bottommost point on the capsule. */
		const TVector<T, 3> GetOrigin() const { return MPoint + MVector * -MRadius; }
		/** Returns the topmost point on the capsule. */
		const TVector<T, 3> GetInsertion() const { return MPoint + MVector * (MHeight + MRadius); }
		TVector<T, 3> GetCenter() const { return MPoint + MVector * (MHeight / 2); }
		/** Returns the centroid (center of mass). */
		TVector<T, 3> GetCenterOfMass() const { return GetCenter(); }
		const TVector<T, 3>& GetAxis() const { return MVector; }
		const TVector<T, 3>& GetX1() const { return MPoint; }
		TVector<T, 3> GetX2() const { return MPoint + MVector * MHeight; }

		T GetArea() const { return GetArea(MHeight, MRadius); }
		static T GetArea(const T Height, const T Radius) { static const T PI2 = 2. * PI; return PI2*Radius * (Height + 2.*Radius); }

		T GetVolume() const { return GetVolume(MHeight, MRadius); }
		static T GetVolume(const T Height, const T Radius) { static const T FourThirds = 4./3; return PI*Radius*Radius * (Height + FourThirds*Radius); }

		PMatrix<T, 3, 3> GetInertiaTensor(const T Mass) const { return GetInertiaTensor(Mass, MHeight, MRadius); }
		static PMatrix<T, 3, 3> GetInertiaTensor(const T Mass, const T Height, const T Radius)
		{
			// https://www.wolframalpha.com/input/?i=capsule&assumption=%7B%22C%22,+%22capsule%22%7D+-%3E+%7B%22Solid%22%7D
			const T R = FMath::Clamp(Radius, (T)0., TNumericLimits<T>::Max());
			const T H = FMath::Clamp(Height, (T)0., TNumericLimits<T>::Max());
			const T RR = R * R;
			const T HH = H * H;

			// (5H^3 + 20*H^2R + 45HR^2 + 32R^3) / (60H + 80R)
			const T Diag12 = Mass * (5.*HH*H + 20.*HH*R + 45.*H*RR + 32.*RR*R) / (60.*H + 80.*R);
			// (R^2 * (15H + 16R) / (30H +40R))
			const T Diag3 = Mass * (RR * (15.*H + 16.*R)) / (30.*H + 40.*R);

			return PMatrix<T, 3, 3>(Diag12, Diag12, Diag3);
		}

		static TRotation<T, 3> GetRotationOfMass()
		{
			return TRotation<T, 3>(TVector<T, 3>(0), 1); 
		}

		virtual uint32 GetTypeHash() const override
		{
			return HashCombine(::GetTypeHash(MPoint), ::GetTypeHash(MVector));
		}

	private:
		void InitUnionedObjects()
		{
			TArray<TUniquePtr<TImplicitObject<float, 3>>> Objects;

			Objects.Add(MakeUnique<Chaos::TCylinder<float>>(MPoint, MPoint + MVector * MHeight, MRadius));
			Objects.Add(MakeUnique<Chaos::TSphere<float, 3>>(MPoint, MRadius));
			Objects.Add(MakeUnique<Chaos::TSphere<float, 3>>(MPoint + MVector * MHeight, MRadius));

			MUnionedObjects.Reset(new Chaos::TImplicitObjectUnion<float, 3>(std::move(Objects)));
		}

		virtual Pair<TVector<T, 3>, bool> FindClosestIntersectionImp(const TVector<T, 3>& StartPoint, const TVector<T, 3>& EndPoint, const T Thickness) const override
		{
			return MUnionedObjects->FindClosestIntersection(StartPoint, EndPoint, Thickness);
		}

		TVector<T, 3> MPoint, MVector;
		T MHeight, MRadius;
		TBox<T, 3> MLocalBoundingBox;
		TUniquePtr<TImplicitObjectUnion<T, 3>> MUnionedObjects;
	};

	template<typename T>
	struct TCapsuleSpecializeSamplingHelper
	{
		static FORCEINLINE void ComputeSamplePoints(TArray<TVector<T, 3>>& Points, const TCapsule<T>& Capsule, const int32 NumPoints)
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

		static FORCEINLINE void ComputeGoldenSpiralPoints(TArray<TVector<T, 3>>& Points, const TCapsule<T>& Capsule, const int32 NumPoints)
		{ ComputeGoldenSpiralPoints(Points, Capsule.GetOrigin(), Capsule.GetAxis(), Capsule.GetHeight(), Capsule.GetRadius(), NumPoints); }

		static FORCEINLINE void ComputeGoldenSpiralPoints(
		    TArray<TVector<T, 3>>& Points,
		    const TVector<T, 3>& Origin,
		    const TVector<T, 3>& Axis,
		    const T Height,
		    const T Radius,
		    const int32 NumPoints)
		{
			// Axis should be normalized.
			checkSlow(FMath::Abs(Axis.Size() - 1.0) < KINDA_SMALL_NUMBER);

			// Evenly distribute points between the capsule body and the end caps.
			int32 NumPointsEndCap;
			int32 NumPointsCylinder;
			const T CapArea = 4 * PI * Radius * Radius;
			const T CylArea = 2.0 * PI * Radius * Height;
			if (CylArea > KINDA_SMALL_NUMBER)
			{
				const T AllArea = CylArea + CapArea;
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
			const T HalfHeight = Height / 2;
			{
				// Points vary in Z: [-Radius-HalfHeight, -HalfHeight]
				TSphereSpecializeSamplingHelper<T, 3>::ComputeBottomHalfSemiSphere(
				    Points, TSphere<T, 3>(TVector<T, 3>(0, 0, -HalfHeight), Radius), NumPointsEndCap, Points.Num());
#if 0
				{
					TSphere<T, 3> Sphere(TVector<T, 3>(0, 0, -HalfHeight), Radius);
					for(int32 i=Offset; i < Points.Num(); i++)
					{
						const TVector<T, 3>& Pt = Points[i];
						const T Phi = Sphere.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
						checkSlow(Pt[2] > -Radius - HalfHeight - KINDA_SMALL_NUMBER && Pt[2] < -HalfHeight + KINDA_SMALL_NUMBER);
					}
				}
#endif
				// Points vary in Z: [-HalfHeight, HalfHeight], about the Z axis.
				TCylinderSpecializeSamplingHelper<T>::ComputeGoldenSpiralPointsUnoriented(
				    Points, Radius, Height, NumPointsCylinder, false, Points.Num());
#if 0
				{
					TCylinder<T> Cylinder(TVector<T, 3>(0, 0, -HalfHeight), TVector<T, 3>(0, 0, HalfHeight), Radius);
					for(int32 i=TmpOffset; i < Points.Num(); i++)
					{
						const TVector<T, 3>& Pt = Points[i];
						const T Phi = Cylinder.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
						checkSlow(Pt[2] > -HalfHeight - KINDA_SMALL_NUMBER && Pt[2] < HalfHeight + KINDA_SMALL_NUMBER);
					}
				}
#endif
				// Points vary in Z: [HalfHeight, HalfHeight+Radius]
				TSphereSpecializeSamplingHelper<T, 3>::ComputeTopHalfSemiSphere(
				    Points, TSphere<T, 3>(TVector<T, 3>(0, 0, HalfHeight), Radius), NumPointsEndCap, Points.Num());
#if 0
				{
					TSphere<T, 3> Sphere(TVector<T, 3>(0, 0, HalfHeight), Radius);
					for(int32 i=TmpOffset; i < Points.Num(); i++)
					{
						const TVector<T, 3>& Pt = Points[i];
						const T Phi = Sphere.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
						checkSlow(Pt[2] > HalfHeight - KINDA_SMALL_NUMBER && Pt[2] < HalfHeight + Radius + KINDA_SMALL_NUMBER);
					}
				}
#endif
#if 0
				{
					TCapsule<T>(TVector<T, 3>(0, 0, -HalfHeight), TVector<T, 3>(0, 0, HalfHeight), Radius);
					for(int32 i=Offset; i < Points.Num(); i++)
					{
						const TVector<T, 3>& Pt = Points[i];
						const T Phi = Cylinder.SignedDistance(Pt);
						checkSlow(FMath::Abs(Phi) < KINDA_SMALL_NUMBER);
					}
				}
#endif
			}

			const TRotation<float, 3> Rotation = TRotation<float, 3>::FromRotatedVector(TVector<float, 3>(0, 0, 1), Axis);
			checkSlow(((Origin + Axis * (Height + Radius * 2)) - (Rotation.RotateVector(TVector<T, 3>(0, 0, Height + Radius * 2)) + Origin)).Size() < KINDA_SMALL_NUMBER);
			for (int32 i = Offset; i < Points.Num(); i++)
			{
				TVector<T, 3>& Point = Points[i];
				const TVector<T, 3> PointNew = Rotation.RotateVector(Point + TVector<T, 3>(0, 0, HalfHeight + Radius)) + Origin;
				checkSlow(FMath::Abs(TCapsule<T>::NewFromOriginAndAxis(Origin, Axis, Height, Radius).SignedDistance(PointNew)) < KINDA_SMALL_NUMBER);
				Point = PointNew;
			}
		}
	};
} // namespace Chaos
