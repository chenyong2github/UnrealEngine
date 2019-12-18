// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/UnrealTemplate.h"

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"

namespace Chaos
{
	template<class T, int d>
	class TBox final : public FImplicitObject
	{
	public:
		using FImplicitObject::SignedDistance;
		using FImplicitObject::GetTypeName;

		// This should never be used outside of creating a default for arrays
		FORCEINLINE TBox()
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box){};
		FORCEINLINE TBox(const TVector<T, d>& Min, const TVector<T, d>& Max)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
			, AABB(Min, Max)
		{
			//todo: turn back on
			/*for (int Axis = 0; Axis < d; ++Axis)
			{
				ensure(MMin[Axis] <= MMax[Axis]);
			}*/
		}

		FORCEINLINE TBox(const TBox<T, d>& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
			, AABB(Other.AABB)
		{
		}

		FORCEINLINE TBox(const TAABB<T, d>& AABB)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
			, AABB(AABB)
		{
		}

		FORCEINLINE TBox(TBox<T, d>&& Other)
		    : FImplicitObject(EImplicitObject::FiniteConvex, ImplicitObjectType::Box)
		    , AABB(MoveTemp(Other.AABB))
		{
		}

		FORCEINLINE TBox<T, d>& operator=(const TBox<T, d>& Other)
		{
			this->Type = Other.Type;
			this->bIsConvex = Other.bIsConvex;
			this->bIgnoreAnalyticCollisions = Other.bIgnoreAnalyticCollisions;
			this->bHasBoundingBox = Other.bHasBoundingBox;

			AABB = Other.AABB;
			return *this;
		}

		FORCEINLINE TBox<T, d>& operator=(TBox<T, d>&& Other)
		{
			this->Type = Other.Type;
			this->bIsConvex = Other.bIsConvex;
			this->bIgnoreAnalyticCollisions = Other.bIgnoreAnalyticCollisions;
			this->bHasBoundingBox = Other.bHasBoundingBox;

			AABB = MoveTemp(Other.AABB);
			return *this;
		}

		virtual ~TBox() {}

		virtual TUniquePtr<FImplicitObject> Copy() const override
		{
			return TUniquePtr<FImplicitObject>(new TBox<T,d>(*this));
		}

		/**
		 * Returns sample points centered about the origin.
		 */
		TArray<TVector<T, d>> ComputeLocalSamplePoints() const
		{
			return AABB.ComputeLocalSamplePoints();
		}

		/**
		 * Returns sample points at the current location of the box.
		 */
		TArray<TVector<T, d>> ComputeSamplePoints() const
		{
			return AABB.ComputeSamplePoints();
		}

		template<class TTRANSFORM>
		TBox<T, d> TransformedBox(const TTRANSFORM& SpaceTransform) const
		{
			return TBox<T, d>(AABB.TransformedAABB(SpaceTransform));
		}

		static FORCEINLINE bool Intersects(const TBox<T, d>& A, const TBox<T, d>& B)
		{
			return A.AABB.Intersects(B.AABB);
		}

		static FORCEINLINE TBox<T, d> Intersection(const TBox<T, d>& A, const TBox<T, d>& B)
		{
			return TBox<T, d>(A.AABB.GetIntersection(B.AABB));
		}

		FORCEINLINE bool Intersects(const TBox<T, d>& Other) const
		{
			return AABB.Intersects(Other.AABB);
		}

		TBox<T, d> GetIntersection(const TBox<T, d>& Other) const
		{
			return TBox<T, d>(AABB.GetIntersection(Other.AABB));
		}

		FORCEINLINE bool Contains(const TVector<T, d>& Point) const
		{
			return AABB.Contains(Point);
		}

		FORCEINLINE bool Contains(const TVector<T, d>& Point, const T Tolerance) const
		{
			return AABB.Contains(Point, Tolerance);
		}

		FORCEINLINE static constexpr EImplicitObjectType StaticType() { return ImplicitObjectType::Box; }

		const TBox<T, d>& BoundingBox() const { return *this; }

		virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
		{
			return AABB.PhiWithNormal(x, Normal);
		}


		static FORCEINLINE bool RaycastFast(const TVector<T,d>& Min, const TVector<T,d>& Max, const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const TVector<T, d>& InvDir, const bool* bParallel, const T Length, const T InvLength, T& OutTime, TVector<T, d>& OutPosition)
		{
			TAABB<T, d> AABB(Min, Max);
			return AABB.RaycastFast(StartPoint, Dir, InvDir, bParallel, Length, InvLength, OutTime, OutPosition);
		}

		virtual bool CHAOS_API Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
		{
			return AABB.Raycast(StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
		}

		TVector<T, d> FindClosestPoint(const TVector<T, d>& StartPoint, const T Thickness = (T)0) const
		{
			return AABB.FindClosestPoint(StartPoint, Thickness);
		}

		virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
		{
			return AABB.FindClosestIntersectionImp(StartPoint, EndPoint, Thickness);
		}

		virtual TVector<T, d> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 FaceIndex, const TVector<T, d>& OriginalNormal) const override
		{
			return AABB.FindGeometryOpposingNormal(DenormDir, FaceIndex, OriginalNormal);
		}

		FORCEINLINE T GetMargin() const { return 0; }

		FORCEINLINE TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const
		{
			return AABB.Support(Direction, Thickness);
		}

		FORCEINLINE TVector<T, d> Support2(const TVector<T, d>& Direction) const
		{
			return AABB.Support2(Direction);
		}

		FORCEINLINE void GrowToInclude(const TVector<T, d>& V)
		{
			AABB.GrowToInclude(V);
		}

		FORCEINLINE void GrowToInclude(const TBox<T, d>& Other)
		{
			AABB.GrowToInclude(Other.AABB);
		}

		FORCEINLINE void ShrinkToInclude(const TBox<T, d>& Other)
		{
			AABB.ShrinkToInclude(Other.AABB);
		}

		FORCEINLINE void Thicken(const float Thickness)
		{
			AABB.Thicken(Thickness);
		}

		//Grows (or shrinks) the box by this vector symmetrically - Changed name because previous Thicken had different semantics which caused several bugs
		FORCEINLINE void ThickenSymmetrically(const TVector<T,d>& Thickness)
		{
			AABB.ThickenSymmetrically(Thickness);
		}

		FORCEINLINE void Scale(const TVector<T, d>& InScale)
		{
			AABB.Scale(InScale);
		}

		FORCEINLINE TVector<T, d> Center() const { return AABB.Center(); }
		FORCEINLINE TVector<T, d> GetCenter() const { return AABB.GetCenter(); }
		FORCEINLINE TVector<T, d> GetCenterOfMass() const { return AABB.GetCenterOfMass(); }
		FORCEINLINE TVector<T, d> Extents() const { return AABB.Extents(); }

		int LargestAxis() const
		{
			return AABB.LargestAxis();
		}

		FORCEINLINE const TVector<T, d>& Min() const { return AABB.Min(); }
		FORCEINLINE const TVector<T, d>& Max() const { return AABB.Max(); }

		T GetArea() const { return AABB.GetArea(); }

		T GetVolume() const { return AABB.GetVolume(); }

		PMatrix<T, d, d> GetInertiaTensor(const T Mass) const { return GetInertiaTensor(Mass, Extents()); }
		FORCEINLINE static PMatrix<T, 3, 3> GetInertiaTensor(const T Mass, const TVector<T, 3>& Dim)
		{
			// https://www.wolframalpha.com/input/?i=cuboid
			const T M = Mass / 12;
			const T WW = Dim[0] * Dim[0];
			const T HH = Dim[1] * Dim[1];
			const T DD = Dim[2] * Dim[2];
			return PMatrix<T, 3, 3>(M * (HH + DD), M * (WW + DD), M * (WW + HH));
		}


		FORCEINLINE static TRotation<T, d> GetRotationOfMass()
		{
			return TRotation<T, d>::FromElements(TVector<T, d>(0), 1);
		}

		virtual FString ToString() const { return FString::Printf(TEXT("TBox Min:%s, Max:%s"), *Min().ToString(), *Max().ToString()); }

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			FImplicitObject::SerializeImp(Ar);
			AABB.Serialize(Ar);
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			SerializeImp(Ar);
		}

		virtual void Serialize(FArchive& Ar) override { SerializeImp(Ar); }

		static TBox<T, d> EmptyBox() { return TBox<T, d>(TVector<T, d>(TNumericLimits<T>::Max()), TVector<T, d>(-TNumericLimits<T>::Max())); }
		static TBox<T, d> ZeroBox() { return TBox<T, d>(TVector<T, d>((T)0), TVector<T, d>((T)0)); }

		virtual uint32 GetTypeHash() const override 
		{
			return AABB.GetTypeHash();
		}

		const TAABB<T, d>& GetAABB() const { return AABB; }

	private:
		TAABB<T, d> AABB;
	};
}
