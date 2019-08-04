// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/UnrealTemplate.h"

#include "Chaos/ImplicitObject.h"
#include "Chaos/Plane.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"

namespace Chaos
{
	template<typename T, int d>
	struct TBoxSpecializeSamplingHelper
	{
		static FORCEINLINE TArray<TVector<T, d>> ComputeLocalSamplePoints(const class TBox<T, d>& Box)
		{
			check(false);
			return TArray<TVector<T, d>>();
		}
	};

	template<class T, int d>
	class TBox final : public TImplicitObject<T, d>
	{
	public:
		IMPLICIT_OBJECT_SERIALIZER(TBox)

		using TImplicitObject<T, d>::SignedDistance;

		// This should never be used outside of creating a default for arrays
		FORCEINLINE TBox()
		    : TImplicitObject<T, d>(EImplicitObject::FiniteConvex, ImplicitObjectType::Box){};
		FORCEINLINE TBox(const TVector<T, d>& Min, const TVector<T, d>& Max)
		    : TImplicitObject<T, d>(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
		    , MMin(Min)
		    , MMax(Max)
		{
		}

		FORCEINLINE TBox(const TBox<T, d>& Other)
		    : TImplicitObject<T, d>(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
		    , MMin(Other.MMin)
		    , MMax(Other.MMax)
		{
		}

		FORCEINLINE TBox(TBox<T, d>&& Other)
		    : TImplicitObject<T, d>(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
		    , MMin(MoveTemp(Other.MMin))
		    , MMax(MoveTemp(Other.MMax))
		{
		}

		FORCEINLINE TBox<T, d>& operator=(const TBox<T, d>& Other)
		{
			this->Type = Other.Type;
			this->bIsConvex = Other.bIsConvex;
			this->bIgnoreAnalyticCollisions = Other.bIgnoreAnalyticCollisions;
			this->bHasBoundingBox = Other.bHasBoundingBox;

			MMin = Other.MMin;
			MMax = Other.MMax;
			return *this;
		}

		FORCEINLINE TBox<T, d>& operator=(TBox<T, d>&& Other)
		{
			this->Type = Other.Type;
			this->bIsConvex = Other.bIsConvex;
			this->bIgnoreAnalyticCollisions = Other.bIgnoreAnalyticCollisions;
			this->bHasBoundingBox = Other.bHasBoundingBox;

			MMin = MoveTemp(Other.MMin);
			MMax = MoveTemp(Other.MMax);
			return *this;
		}
		virtual ~TBox() {}

		/**
		 * Returns sample points centered about the origin.
		 */
		TArray<TVector<T, d>> ComputeLocalSamplePoints() const
		{
			const TVector<T, d> Mid = Center();
			return TBoxSpecializeSamplingHelper<T, d>::ComputeSamplePoints(TBox<T, d>(Min() - Mid, Max() - Mid));
		}

		/**
		 * Returns sample points at the current location of the box.
		 */
		TArray<TVector<T, d>> ComputeSamplePoints() const
		{
			return TBoxSpecializeSamplingHelper<T, d>::ComputeSamplePoints(*this);
		}

		template<class TTRANSFORM>
		TBox<T, d> TransformedBox(const TTRANSFORM& SpaceTransform) const
		{
			TVector<T, d> CurrentExtents = Extents();
			int32 Idx = 0;
			const TVector<T, d> MinToNewSpace = SpaceTransform.TransformPosition(MMin);
			TBox<T, d> NewBox(MinToNewSpace, MinToNewSpace);
			NewBox.GrowToInclude(SpaceTransform.TransformPosition(MMax));

			for (int32 j = 0; j < d; ++j)
			{
				NewBox.GrowToInclude(SpaceTransform.TransformPosition(MMin + TVector<T, d>::AxisVector(j) * CurrentExtents));
				NewBox.GrowToInclude(SpaceTransform.TransformPosition(MMax - TVector<T, d>::AxisVector(j) * CurrentExtents));
			}

			return NewBox;
		}

		FORCEINLINE bool Intersects(const TBox<T, d>& Other) const
		{
			for (int32 i = 0; i < d; ++i)
			{
				if (Other.MMax[i] < MMin[i] || Other.MMin[i] > MMax[i])
					return false;
			}
			return true;
		}

		TBox<T, d> GetIntersection(const TBox<T, d>& Other) const
		{
			TVector<float, 3> Tmp;
			return TBox<T, d>(MMin.ComponentwiseMax(Other.MMin), MMax.ComponentwiseMin(Other.MMax));
		}

		FORCEINLINE bool Contains(const TVector<T, d>& Point) const
		{
			for (int i = 0; i < d; i++)
			{
				if (Point[i] < MMin[i] || Point[i] > MMax[i])
				{
					return false;
				}
			}
			return true;
		}

		FORCEINLINE bool Contains(const TVector<T, d>& Point, const T Tolerance) const
		{
			for (int i = 0; i < d; i++)
			{
				if (Point[i] < MMin[i] - Tolerance || Point[i] > MMax[i] + Tolerance)
				{
					return false;
				}
			}
			return true;
		}

		FORCEINLINE static ImplicitObjectType GetType() { return ImplicitObjectType::Box; }

		const TBox<T, d>& BoundingBox() const { return *this; }

		virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
		{
			const TVector<T, d> MaxDists = x - MMax;
			const TVector<T, d> MinDists = MMin - x;
			if (x <= MMax && x >= MMin)
			{
				const Pair<T, int32> MaxAndAxis = TVector<T, d>::MaxAndAxis(MinDists, MaxDists);
				Normal = MaxDists[MaxAndAxis.Second] > MinDists[MaxAndAxis.Second] ? TVector<T, d>::AxisVector(MaxAndAxis.Second) : -TVector<T, d>::AxisVector(MaxAndAxis.Second);
				return MaxAndAxis.First;
			}
			else
			{
				for (int i = 0; i < d; ++i)
				{
					if (!ensure((MaxDists[i] <= 0 || MinDists[i] <= 0)))
					{
						return FLT_MAX;
					}
					if (MaxDists[i] > 0)
					{
						Normal[i] = MaxDists[i];
					}
					else if (MinDists[i] > 0)
					{
						Normal[i] = -MinDists[i];
					}
					else
					{
						Normal[i] = 0;
					}
				}
				T Phi = Normal.SafeNormalize();
				if (Phi < KINDA_SMALL_NUMBER)
				{
					for (int i = 0; i < d; ++i)
					{
						if (Normal[i] > 0)
						{
							Normal[i] = 1;
						}
						else if (Normal[i] < 0)
						{
							Normal[i] = -1;
						}
					}
					Normal.Normalize();
				}
				return Phi;
			}
		}

		virtual bool CHAOS_API Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override;

		bool RaycastFast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const TVector<T, d>& InvDir, const bool* bParallel, const T Length, const T InvLength, T& OutTime, TVector<T, d>& OutPosition) const
		{
			const TVector<T, d> StartToMin = MMin - StartPoint;
			const TVector<T, d> StartToMax = MMax - StartPoint;

			//For each axis record the start and end time when ray is in the box. If the intervals overlap the ray is inside the box
			T LatestStartTime = 0;
			T EarliestEndTime = FLT_MAX;

			for (int Axis = 0; Axis < d; ++Axis)
			{
				T Time1, Time2;
				if (bParallel[Axis])
				{
					if (StartToMin[Axis] > 0 || StartToMax[Axis] < 0)
					{
						return false;	//parallel and outside
					}
					else
					{
						Time1 = 0;
						Time2 = FLT_MAX;
					}
				}
				else
				{
					Time1 = StartToMin[Axis] * InvDir[Axis];
					Time2 = StartToMax[Axis] * InvDir[Axis];
				}

				if (Time1 > Time2)
				{
					//going from max to min direction
					Swap(Time1, Time2);
				}

				LatestStartTime = FMath::Max(LatestStartTime, Time1);
				EarliestEndTime = FMath::Min(EarliestEndTime, Time2);

				if (LatestStartTime > EarliestEndTime)
				{
					return false;	//Outside of slab before entering another
				}
			}

			//infinite ray intersects with inflated box
			if (LatestStartTime > Length || EarliestEndTime < 0)
			{
				//outside of line segment given
				return false;
			}

			OutTime = LatestStartTime;
			OutPosition = StartPoint + LatestStartTime * Dir;
			return true;
		}
		
		
		TVector<T, d> FindClosestPoint(const TVector<T, d>& StartPoint, const T Thickness = (T)0) const
		{
			TVector<T, d> Result(0);

			// clamp exterior to surface
			bool bIsExterior = false;
			for (int i = 0; i < 3; i++)
			{
				float v = StartPoint[i];
				if (v < MMin[i])
				{
					v = MMin[i];
					bIsExterior = true;
				}
				if (v > MMax[i])
				{
					v = MMax[i];
					bIsExterior = true;
				}
				Result[i] = v;
			}

			if (!bIsExterior)
			{
				TArray<Pair<T, TVector<T, d>>> Intersections;

				// sum interior direction to surface
				for (int32 i = 0; i < d; ++i)
				{
					auto PlaneIntersection = TPlane<T, d>(MMin - Thickness, -TVector<T, d>::AxisVector(i)).FindClosestPoint(Result, 0);
					Intersections.Add(MakePair((PlaneIntersection - Result).Size(), -TVector<T, d>::AxisVector(i)));
					PlaneIntersection = TPlane<T, d>(MMax + Thickness, TVector<T, d>::AxisVector(i)).FindClosestPoint(Result, 0);
					Intersections.Add(MakePair((PlaneIntersection - Result).Size(), TVector<T, d>::AxisVector(i)));
				}
				Intersections.Sort([](const Pair<T, TVector<T, d>>& Elem1, const Pair<T, TVector<T, d>>& Elem2) { return Elem1.First < Elem2.First; });

				if (!FMath::IsNearlyEqual(Intersections[0].First, 0.f))
				{
					T SmallestDistance = Intersections[0].First;
					Result += Intersections[0].Second * Intersections[0].First;
					for (int32 i = 1; i < 3 && FMath::IsNearlyEqual(SmallestDistance, Intersections[i].First); ++i)
					{
						Result += Intersections[i].Second * Intersections[i].First;
					}
				}
			}
			return Result;
		}

		virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
		{
			TArray<Pair<T, TVector<T, d>>> Intersections;
			for (int32 i = 0; i < d; ++i)
			{
				auto PlaneIntersection = TPlane<T, d>(MMin - Thickness, -TVector<T, d>::AxisVector(i)).FindClosestIntersection(StartPoint, EndPoint, 0);
				if (PlaneIntersection.Second)
					Intersections.Add(MakePair((PlaneIntersection.First - StartPoint).Size(), PlaneIntersection.First));
				PlaneIntersection = TPlane<T, d>(MMax + Thickness, TVector<T, d>::AxisVector(i)).FindClosestIntersection(StartPoint, EndPoint, 0);
				if (PlaneIntersection.Second)
					Intersections.Add(MakePair((PlaneIntersection.First - StartPoint).Size(), PlaneIntersection.First));
			}
			Intersections.Sort([](const Pair<T, TVector<T, d>>& Elem1, const Pair<T, TVector<T, d>>& Elem2) { return Elem1.First < Elem2.First; });
			for (const auto& Elem : Intersections)
			{
				if (SignedDistance(Elem.Second) < (Thickness + 1e-4))
				{
					return MakePair(Elem.Second, true);
				}
			}
			return MakePair(TVector<T, d>(0), false);
		}

		virtual TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const override
		{
			TVector<T, d> ChosenPt;
			for (int Axis = 0; Axis < d; ++Axis)
			{
				ChosenPt[Axis] = Direction[Axis] < 0 ? MMin[Axis] : MMax[Axis];
			}

			if (Thickness > 0)
			{
				//We want N / ||N|| and to avoid inf
				//So we want N / ||N|| < 1 / eps => N eps < ||N||, but this is clearly true for all eps < 1 and N > 0
				T SizeSqr = Direction.SizeSquared();
				if (SizeSqr <= TNumericLimits<T>::Min())
				{
					return ChosenPt;
				}
				const TVector<T, d> Normalized = Direction / sqrt(SizeSqr);

				const TVector<T, d> InflatedPt = ChosenPt + Normalized.GetSafeNormal() * Thickness;
				return InflatedPt;
			}

			return ChosenPt;
		}

		FORCEINLINE void GrowToInclude(const TVector<T, d>& V)
		{
			MMin = TVector<T, d>(FGenericPlatformMath::Min(MMin[0], V[0]), FGenericPlatformMath::Min(MMin[1], V[1]), FGenericPlatformMath::Min(MMin[2], V[2]));
			MMax = TVector<T, d>(FGenericPlatformMath::Max(MMax[0], V[0]), FGenericPlatformMath::Max(MMax[1], V[1]), FGenericPlatformMath::Max(MMax[2], V[2]));
		}

		FORCEINLINE void GrowToInclude(const TBox<T, d>& Other)
		{
			MMin = TVector<T, d>(FGenericPlatformMath::Min(MMin[0], Other.MMin[0]), FGenericPlatformMath::Min(MMin[1], Other.MMin[1]), FGenericPlatformMath::Min(MMin[2], Other.MMin[2]));
			MMax = TVector<T, d>(FGenericPlatformMath::Max(MMax[0], Other.MMax[0]), FGenericPlatformMath::Max(MMax[1], Other.MMax[1]), FGenericPlatformMath::Max(MMax[2], Other.MMax[2]));
		}

		FORCEINLINE void ShrinkToInclude(const TBox<T, d>& Other)
		{
			MMin = TVector<T, d>(FGenericPlatformMath::Max(MMin[0], Other.MMin[0]), FGenericPlatformMath::Max(MMin[1], Other.MMin[1]), FGenericPlatformMath::Max(MMin[2], Other.MMin[2]));
			MMax = TVector<T, d>(FGenericPlatformMath::Min(MMax[0], Other.MMax[0]), FGenericPlatformMath::Min(MMax[1], Other.MMax[1]), FGenericPlatformMath::Min(MMax[2], Other.MMax[2]));
		}

		FORCEINLINE void Thicken(const float Thickness)
		{
			MMin -= TVector<T, d>(Thickness);
			MMax += TVector<T, d>(Thickness);
		}

		//Grows (or shrinks) the box by this vector symmetrically - Changed name because previous Thicken had different semantics which caused several bugs
		FORCEINLINE void ThickenSymmetrically(const TVector<T,d>& Thickness)
		{
			MMin -= Thickness;
			MMax += Thickness;
		}

		FORCEINLINE TVector<T, d> Center() const { return (MMax - MMin) / (T)2 + MMin; }
		FORCEINLINE TVector<T, d> GetCenter() const { return Center(); }
		FORCEINLINE TVector<T, d> GetCenterOfMass() const { return GetCenter(); }
		FORCEINLINE TVector<T, d> Extents() const { return MMax - MMin; }

		int LargestAxis() const
		{
			const auto Extents = this->Extents();
			if (Extents[0] > Extents[1] && Extents[0] > Extents[2])
			{
				return 0;
			}
			else if (Extents[1] > Extents[2])
			{
				return 1;
			}
			else
			{
				return 2;
			}
		}

		FORCEINLINE const TVector<T, d>& Min() const { return MMin; }
		FORCEINLINE const TVector<T, d>& Max() const { return MMax; }

		T GetArea() const { return GetArea(Extents()); }
		static T GetArea(const TVector<T, d>& Dim) { return d == 2 ? Dim.Product() : 2. * (Dim[0] * Dim[1] + Dim[0] * Dim[2] + Dim[1] * Dim[2]); }

		T GetVolume() const { return GetVolume(Extents()); }
		static T GetVolume(const TVector<T, 3>& Dim) { return Dim.Product(); }

		PMatrix<T, d, d> GetInertiaTensor(const T Mass) const { return GetInertiaTensor(Mass, Extents()); }
		static PMatrix<T, 3, 3> GetInertiaTensor(const T Mass, const TVector<T, 3>& Dim)
		{
			// https://www.wolframalpha.com/input/?i=cuboid
			const T M = Mass / 12;
			const T WW = Dim[0] * Dim[0];
			const T HH = Dim[1] * Dim[1];
			const T DD = Dim[2] * Dim[2];
			return PMatrix<T, 3, 3>(M * (HH + DD), M * (WW + DD), M * (WW + HH));
		}
		static TRotation<T, d> GetRotationOfMass()
		{
			return TRotation<T, d>(TVector<T, d>(0), 1);
		}

		virtual FString ToString() const { return FString::Printf(TEXT("TBox Min:%s, Max:%s"), *Min().ToString(), *Max().ToString()); }

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			TImplicitObject<T, d>::SerializeImp(Ar);
			Ar << MMin << MMax;
		}
		virtual void Serialize(FChaosArchive& Ar) override { SerializeImp(Ar); }
		virtual void Serialize(FArchive& Ar) override { SerializeImp(Ar); }

		static TBox<T, d> EmptyBox() { return TBox<T, d>(TVector<T, d>(TNumericLimits<T>::Max()), TVector<T, d>(-TNumericLimits<T>::Max())); }
		static TBox<T, d> ZeroBox() { return TBox<T, d>(TVector<T, d>((T)0), TVector<T, d>((T)0)); }

		virtual uint32 GetTypeHash() const override 
		{
			return HashCombine(::GetTypeHash(MMin), ::GetTypeHash(MMax));
		}

	private:
		TVector<T, d> MMin, MMax;
	};

	template<typename T>
	struct TBoxSpecializeSamplingHelper<T, 2>
	{
		static FORCEINLINE TArray<TVector<T, 2>> ComputeSamplePoints(const TBox<T, 2>& Box)
		{
			const TVector<T, 2>& Min = Box.Min();
			const TVector<T, 2>& Max = Box.Max();
			const TVector<T, 2> Mid = Box.Center();

			TArray<TVector<T, 2>> SamplePoints;
			SamplePoints.SetNum(8);
			//top line (min y)
			SamplePoints[0] = TVector<T, 2>{Min.X, Min.Y};
			SamplePoints[1] = TVector<T, 2>{Mid.X, Min.Y};
			SamplePoints[2] = TVector<T, 2>{Max.X, Min.Y};

			//mid line (y=0) (mid point removed because internal)
			SamplePoints[3] = TVector<T, 2>{Min.X, Mid.Y};
			SamplePoints[4] = TVector<T, 2>{Max.X, Mid.Y};

			//bottom line (max y)
			SamplePoints[5] = TVector<T, 2>{Min.X, Max.Y};
			SamplePoints[6] = TVector<T, 2>{Mid.X, Max.Y};
			SamplePoints[7] = TVector<T, 2>{Max.X, Max.Y};

			return SamplePoints;
		}
	};

	template<typename T>
	struct TBoxSpecializeSamplingHelper<T, 3>
	{
		static FORCEINLINE TArray<TVector<T, 3>> ComputeSamplePoints(const TBox<T, 3>& Box)
		{
			const TVector<T, 3>& Min = Box.Min();
			const TVector<T, 3>& Max = Box.Max();
			const TVector<T, 3> Mid = Box.Center();

			//todo(ocohen): should order these for best levelset cache traversal
			TArray<TVector<T, 3>> SamplePoints;
			SamplePoints.SetNum(26);
			{
				//xy plane for Min Z
				SamplePoints[0] = TVector<T, 3>{Min.X, Min.Y, Min.Z};
				SamplePoints[1] = TVector<T, 3>{Mid.X, Min.Y, Min.Z};
				SamplePoints[2] = TVector<T, 3>{Max.X, Min.Y, Min.Z};

				SamplePoints[3] = TVector<T, 3>{Min.X, Mid.Y, Min.Z};
				SamplePoints[4] = TVector<T, 3>{Mid.X, Mid.Y, Min.Z};
				SamplePoints[5] = TVector<T, 3>{Max.X, Mid.Y, Min.Z};

				SamplePoints[6] = TVector<T, 3>{Min.X, Max.Y, Min.Z};
				SamplePoints[7] = TVector<T, 3>{Mid.X, Max.Y, Min.Z};
				SamplePoints[8] = TVector<T, 3>{Max.X, Max.Y, Min.Z};
			}

			{
				//xy plane for z = 0 (skip mid point since inside)
				SamplePoints[9] = TVector<T, 3>{Min.X, Min.Y, Mid.Z};
				SamplePoints[10] = TVector<T, 3>{Mid.X, Min.Y, Mid.Z};
				SamplePoints[11] = TVector<T, 3>{Max.X, Min.Y, Mid.Z};

				SamplePoints[12] = TVector<T, 3>{Min.X, Mid.Y, Mid.Z};
				SamplePoints[13] = TVector<T, 3>{Max.X, Mid.Y, Mid.Z};

				SamplePoints[14] = TVector<T, 3>{Min.X, Max.Y, Mid.Z};
				SamplePoints[15] = TVector<T, 3>{Mid.X, Max.Y, Mid.Z};
				SamplePoints[16] = TVector<T, 3>{Max.X, Max.Y, Mid.Z};
			}

			{
				//xy plane for Max Z
				SamplePoints[17] = TVector<T, 3>{Min.X, Min.Y, Max.Z};
				SamplePoints[18] = TVector<T, 3>{Mid.X, Min.Y, Max.Z};
				SamplePoints[19] = TVector<T, 3>{Max.X, Min.Y, Max.Z};

				SamplePoints[20] = TVector<T, 3>{Min.X, Mid.Y, Max.Z};
				SamplePoints[21] = TVector<T, 3>{Mid.X, Mid.Y, Max.Z};
				SamplePoints[22] = TVector<T, 3>{Max.X, Mid.Y, Max.Z};

				SamplePoints[23] = TVector<T, 3>{Min.X, Max.Y, Max.Z};
				SamplePoints[24] = TVector<T, 3>{Mid.X, Max.Y, Max.Z};
				SamplePoints[25] = TVector<T, 3>{Max.X, Max.Y, Max.Z};
			}

			return SamplePoints;
		}
	};
}
