// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImplicitObject.h"
#include "Plane.h"

namespace Chaos
{
	template<typename T>
	class TTriangle
	{
	public:

		TTriangle(const TVector<T, 3>& InA, const TVector<T, 3>& InB, const TVector<T, 3>& InC)
			: A(InA)
			, B(InB)
			, C(InC)
		{}

		TVector<T, 3>& operator[](uint32 InIndex)
		{
			check(InIndex < 3);
			return (&A)[InIndex];
		}

		const TVector<T, 3>& operator[](uint32 InIndex) const
		{
			check(InIndex < 3);
			return (&A)[InIndex];
		}

		FORCEINLINE TVector<T, 3> GetNormal() const
		{
			return TVector<T, 3>::CrossProduct(B - A, C - A).GetSafeNormal();
		}

		FORCEINLINE TPlane<T, 3> GetPlane() const
		{
			return TPlane<T, 3>(A, GetNormal());
		}

		FORCEINLINE T PhiWithNormal(const TVector<T, 3>& InSamplePoint, TVector<T, 3>& OutNormal) const
		{
			OutNormal = GetNormal();
			TVector<T, 3> ClosestPoint = FindClosestPointOnTriangle(GetPlane(), A, B, C, InSamplePoint);
			return TVector<T, 3>::DotProduct((InSamplePoint - ClosestPoint), OutNormal);
		}

		FORCEINLINE TVector<T, 3> Support(const TVector<T, 3>& Direction, const T Thickness) const
		{
			const float DotA = TVector<T, 3>::DotProduct(A, Direction);
			const float DotB = TVector<T, 3>::DotProduct(B, Direction);
			const float DotC = TVector<T, 3>::DotProduct(C, Direction);

			if(DotA > DotB && DotA > DotC)
			{
				if(Thickness != 0)
				{
					return A + Direction.GetUnsafeNormal() * Thickness;
				}
				return A;
			}
			else if(DotB > DotA && DotB > DotC)
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

		FORCEINLINE_DEBUGGABLE TVector<T, 3> Support2(const TVector<T, 3>& Direction) const
		{
			const float DotA = TVector<T, 3>::DotProduct(A, Direction);
			const float DotB = TVector<T, 3>::DotProduct(B, Direction);
			const float DotC = TVector<T, 3>::DotProduct(C, Direction);

			if (DotA > DotB && DotA > DotC)
			{
				return A;
			}
			else if (DotB > DotA && DotB > DotC)
			{
				return B;
			}

			return C;
		}

		FORCEINLINE T GetMargin() const { return 0; }

		FORCEINLINE bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex) const
		{
			// No face as this is only one triangle
			OutFaceIndex = INDEX_NONE;

			// Pass through GJK #BGTODO Maybe specialise if it's possible to be faster
			const TRigidTransform<T, 3> StartTM(StartPoint, TRotation<T, 3>::FromIdentity());
			const TSphere<T, 3> Sphere(TVector<T, 3>(0), Thickness);
			return GJKRaycast(*this, Sphere, StartTM, Dir, Length, OutTime, OutPosition, OutNormal);
		}

		FORCEINLINE bool Overlap(const TVector<T, 3>& Point, const T Thickness) const 
		{
			const TVector<T, 3> ClosestPoint = FindClosestPointOnTriangle(GetPlane(), A, B, C, Point);
			const float AdjustedThickness = FMath::Max(Thickness, KINDA_SMALL_NUMBER);
			return (Point - ClosestPoint).SizeSquared() <= (AdjustedThickness * AdjustedThickness);
		}

		FORCEINLINE bool IsConvex() const
		{
			return true;
		}

	private:

		friend FChaosArchive& operator<<(FChaosArchive& Ar, TTriangle<T>& Value);

		TVector<T, 3> A;
		TVector<T, 3> B;
		TVector<T, 3> C;
	};

	template<typename T>
	FChaosArchive& operator<<(FChaosArchive& Ar, TTriangle<T>& Value)
	{
		Ar << Value.A << Value.B << Value.C;
		return Ar;
	}

	template<typename T>
	class TImplicitTriangle final : public FImplicitObject
	{
	public:

		TImplicitTriangle()
			: FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Triangle)
		{}

		TImplicitTriangle(const TImplicitTriangle&) = delete;

		TImplicitTriangle(TImplicitTriangle&& InToSteal)
			: FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Triangle)
		{}

		TImplicitTriangle(const TVector<T, 3>& InA, const TVector<T, 3>& InB, const TVector<T, 3>& InC)
			: FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Triangle)
			, Tri(InA, InB, InC)
		{
		}

		TVector<T, 3>& operator[](uint32 InIndex)
		{
			return Tri[InIndex];
		}

		const TVector<T, 3>& operator[](uint32 InIndex) const
		{
			return Tri[InIndex];
		}

		TVector<T, 3> GetNormal() const
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

		virtual T PhiWithNormal(const TVector<T, 3>& InSamplePoint, TVector<T, 3>& OutNormal) const override
		{
			return Tri.PhiWithNormal(InSamplePoint, OutNormal);
		}

		virtual const class TAABB<T, 3> BoundingBox() const override
		{
			TAABB<T,3> Bounds(Tri[0],Tri[0]);
			Bounds.GrowToInclude(Tri[1]);
			Bounds.GrowToInclude(Tri[2]);

			return Bounds;
		}

		virtual TVector<T, 3> Support(const TVector<T, 3>& Direction, const T Thickness) const override
		{
			return Tri.Support(Direction, Thickness);
		}

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex) const override
		{
			return Tri.Raycast(StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
		}

		virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, 3>& DenormDir, int32 FaceIndex, const TVector<T, 3>& OriginalNormal) const override
		{
			return Tri.GetNormal();
		}

		virtual bool Overlap(const TVector<T, 3>& Point, const T Thickness) const override
		{
			return Tri.Overlap(Point, Thickness);
		}

		virtual FString ToString() const override
		{
			return FString::Printf(TEXT("Triangle: A: %s, B: %s, C: %s"), LexToString(Tri[0]), LexToString(Tri[1]), LexToString(Tri[2]));
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