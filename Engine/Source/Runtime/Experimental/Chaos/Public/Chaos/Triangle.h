// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Core.h"
#include "Chaos/GJK.h"
#include "ImplicitObject.h"
#include "Plane.h"

namespace Chaos
{
	class FTriangle
	{
	public:
		FTriangle(const FVec3& InA, const FVec3& InB, const FVec3& InC)
			: A(InA)
			, B(InB)
			, C(InC)
		{}

		FVec3& operator[](uint32 InIndex)
		{
			check(InIndex < 3);
			return (&A)[InIndex];
		}

		const FVec3& operator[](uint32 InIndex) const
		{
			check(InIndex < 3);
			return (&A)[InIndex];
		}

		FORCEINLINE FVec3 GetNormal() const
		{
			return FVec3::CrossProduct(B - A, C - A).GetSafeNormal();
		}

		FORCEINLINE TPlane<FReal, 3> GetPlane() const
		{
			return TPlane<FReal, 3>(A, GetNormal());
		}

		FORCEINLINE FReal PhiWithNormal(const FVec3& InSamplePoint, FVec3& OutNormal) const
		{
			OutNormal = GetNormal();
			FVec3 ClosestPoint = FindClosestPointOnTriangle(GetPlane(), A, B, C, InSamplePoint);
			return FVec3::DotProduct((InSamplePoint - ClosestPoint), OutNormal);
		}

		FORCEINLINE FVec3 Support(const FVec3& Direction, const FReal Thickness) const
		{
			const FReal DotA = FVec3::DotProduct(A, Direction);
			const FReal DotB = FVec3::DotProduct(B, Direction);
			const FReal DotC = FVec3::DotProduct(C, Direction);

			if(DotA >= DotB && DotA >= DotC)
			{
				if(Thickness != 0)
				{
					return A + Direction.GetUnsafeNormal() * Thickness;
				}
				return A;
			}
			else if(DotB >= DotA && DotB >= DotC)
			{
				if(Thickness != 0)
				{
					return B + Direction.GetUnsafeNormal() * Thickness;
				}
				return B;
			}

			if(Thickness != 0)
			{
				return C + Direction.GetUnsafeNormal() * Thickness;
			}
			return C;
		}

		FORCEINLINE_DEBUGGABLE FVec3 SupportCore(const FVec3& Direction, FReal InMargin) const
		{
			const FReal DotA = FVec3::DotProduct(A, Direction);
			const FReal DotB = FVec3::DotProduct(B, Direction);
			const FReal DotC = FVec3::DotProduct(C, Direction);

			if (DotA >= DotB && DotA >= DotC)
			{
				return A;
			}
			else if (DotB >= DotA && DotB >= DotC)
			{
				return B;
			}

			return C;
		}

		FORCEINLINE FVec3 SupportCoreScaled(const FVec3& Direction, FReal InMargin, const FVec3& Scale) const
		{
			// Note: ignores InMargin, assumed 0 (triangles cannot have a margin as they are zero thickness)
			return SupportCore(Direction * Scale, 0.0f) * Scale;
		}

		FORCEINLINE FReal GetMargin() const { return 0; }
		FORCEINLINE FReal GetRadius() const { return 0; }

		FORCEINLINE bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const
		{
			// No face as this is only one triangle
			OutFaceIndex = INDEX_NONE;

			// Pass through GJK #BGTODO Maybe specialise if it's possible to be faster
			const FRigidTransform3 StartTM(StartPoint, FRotation3::FromIdentity());
			const TSphere<FReal, 3> Sphere(FVec3(0), Thickness);
			return GJKRaycast(*this, Sphere, StartTM, Dir, Length, OutTime, OutPosition, OutNormal);
		}

		FORCEINLINE bool Overlap(const FVec3& Point, const FReal Thickness) const
		{
			const FVec3 ClosestPoint = FindClosestPointOnTriangle(GetPlane(), A, B, C, Point);
			const FReal AdjustedThickness = FMath::Max(Thickness, KINDA_SMALL_NUMBER);
			return (Point - ClosestPoint).SizeSquared() <= (AdjustedThickness * AdjustedThickness);
		}

		FORCEINLINE bool IsConvex() const
		{
			return true;
		}

	private:

		friend FChaosArchive& operator<<(FChaosArchive& Ar, FTriangle& Value);

		FVec3 A;
		FVec3 B;
		FVec3 C;
	};

	inline FChaosArchive& operator<<(FChaosArchive& Ar, FTriangle& Value)
	{
		Ar << Value.A << Value.B << Value.C;
		return Ar;
	}

	template<typename T> using TTriangle = FTriangle;
	
	template<typename T>
	class UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use other triangle based ImplicitObjects") TImplicitTriangle final : public FImplicitObject
	{
	public:

		TImplicitTriangle()
			: FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Triangle)
		{}

		TImplicitTriangle(const TImplicitTriangle&) = delete;

		TImplicitTriangle(TImplicitTriangle&& InToSteal)
			: FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Triangle)
		{}

		TImplicitTriangle(const TVec3<T>& InA, const TVec3<T>& InB, const TVec3<T>& InC)
			: FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Triangle)
			, Tri(InA, InB, InC)
		{
		}

		TVec3<T>& operator[](uint32 InIndex)
		{
			return Tri[InIndex];
		}

		const TVec3<T>& operator[](uint32 InIndex) const
		{
			return Tri[InIndex];
		}

		TVec3<T> GetNormal() const
		{
			return Tri.GetNormal();
		}

		TPlane<T, 3> GetPlane() const
		{
			return Tri.GetPlane();
		}

		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::Triangle;
		}

		virtual T PhiWithNormal(const TVec3<T>& InSamplePoint, TVec3<T>& OutNormal) const override
		{
			return Tri.PhiWithNormal(InSamplePoint, OutNormal);
		}

		virtual const class TAABB<T, 3> BoundingBox() const override
		{
			TAABB<T, 3> Bounds(Tri[0], Tri[0]);
			Bounds.GrowToInclude(Tri[1]);
			Bounds.GrowToInclude(Tri[2]);

			return Bounds;
		}

		virtual TVec3<T> Support(const TVec3<T>& Direction, const T Thickness) const override
		{
			return Tri.Support(Direction, Thickness);
		}

		virtual bool Raycast(const TVec3<T>& StartPoint, const TVec3<T>& Dir, const T Length, const T Thickness, T& OutTime, TVec3<T>& OutPosition, TVec3<T>& OutNormal, int32& OutFaceIndex) const override
		{
			return Tri.Raycast(StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
		}

		virtual TVec3<T> FindGeometryOpposingNormal(const TVec3<T>& DenormDir, int32 FaceIndex, const TVec3<T>& OriginalNormal) const override
		{
			return Tri.GetNormal();
		}

		virtual bool Overlap(const TVec3<T>& Point, const T Thickness) const override
		{
			return Tri.Overlap(Point, Thickness);
		}

		virtual FString ToString() const override
		{
			return FString::Printf(TEXT("Triangle: A: [%f, %f, %f], B: [%f, %f, %f], C: [%f, %f, %f]"), Tri[0].X, Tri[0].Y, Tri[0].Z, Tri[1].X, Tri[1].Y, Tri[1].Z, Tri[2].X, Tri[2].Y, Tri[2].Z);
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			Ar << Tri;
		}

		virtual uint32 GetTypeHash() const override
		{
			return HashCombine(::GetTypeHash(Tri[0]), HashCombine(::GetTypeHash(Tri[1]), ::GetTypeHash(Tri[2])));
		}

		virtual FName GetTypeName() const override
		{
			return FName("Triangle");
		}

	private:

		TTriangle<T> Tri;
	};
}