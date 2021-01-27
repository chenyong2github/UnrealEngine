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

		TTriangle(const TVec3<T>& InA, const TVec3<T>& InB, const TVec3<T>& InC)
			: A(InA)
			, B(InB)
			, C(InC)
		{}

		TVec3<T>& operator[](uint32 InIndex)
		{
			check(InIndex < 3);
			return (&A)[InIndex];
		}

		const TVec3<T>& operator[](uint32 InIndex) const
		{
			check(InIndex < 3);
			return (&A)[InIndex];
		}

		FORCEINLINE TVec3<T> GetNormal() const
		{
			return TVec3<T>::CrossProduct(B - A, C - A).GetSafeNormal();
		}

		FORCEINLINE TPlane<T, 3> GetPlane() const
		{
			return TPlane<T, 3>(A, GetNormal());
		}

		FORCEINLINE T PhiWithNormal(const TVec3<T>& InSamplePoint, TVec3<T>& OutNormal) const
		{
			OutNormal = GetNormal();
			TVec3<T> ClosestPoint = FindClosestPointOnTriangle(GetPlane(), A, B, C, InSamplePoint);
			return TVec3<T>::DotProduct((InSamplePoint - ClosestPoint), OutNormal);
		}

		FORCEINLINE TVec3<T> Support(const TVec3<T>& Direction, const T Thickness) const
		{
			const float DotA = TVec3<T>::DotProduct(A, Direction);
			const float DotB = TVec3<T>::DotProduct(B, Direction);
			const float DotC = TVec3<T>::DotProduct(C, Direction);

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

		FORCEINLINE_DEBUGGABLE TVec3<T> SupportCore(const TVec3<T>& Direction, float InMargin) const
		{
			const float DotA = TVec3<T>::DotProduct(A, Direction);
			const float DotB = TVec3<T>::DotProduct(B, Direction);
			const float DotC = TVec3<T>::DotProduct(C, Direction);

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

		FORCEINLINE TVector<T, 3> SupportCoreScaled(const TVector<T, 3>& Direction, float InMargin, const TVector<T, 3>& Scale) const
		{
			// No margin support in triangles (they are zero thickness so cannot have an internal margin)
			return SupportCore(Direction * Scale, 0.0f) * Scale;
		}

		FORCEINLINE T GetMargin() const { return 0; }

		FORCEINLINE bool Raycast(const TVec3<T>& StartPoint, const TVec3<T>& Dir, const T Length, const T Thickness, T& OutTime, TVec3<T>& OutPosition, TVec3<T>& OutNormal, int32& OutFaceIndex) const
		{
			// No face as this is only one triangle
			OutFaceIndex = INDEX_NONE;

			// Pass through GJK #BGTODO Maybe specialise if it's possible to be faster
			const TRigidTransform<T, 3> StartTM(StartPoint, TRotation<T, 3>::FromIdentity());
			const TSphere<T, 3> Sphere(TVec3<T>(0), Thickness);
			return GJKRaycast(*this, Sphere, StartTM, Dir, Length, OutTime, OutPosition, OutNormal);
		}

		FORCEINLINE bool Overlap(const TVec3<T>& Point, const T Thickness) const 
		{
			const TVec3<T> ClosestPoint = FindClosestPointOnTriangle(GetPlane(), A, B, C, Point);
			const float AdjustedThickness = FMath::Max(Thickness, KINDA_SMALL_NUMBER);
			return (Point - ClosestPoint).SizeSquared() <= (AdjustedThickness * AdjustedThickness);
		}

		FORCEINLINE bool IsConvex() const
		{
			return true;
		}

	private:

		friend FChaosArchive& operator<<(FChaosArchive& Ar, TTriangle<T>& Value);

		TVec3<T> A;
		TVec3<T> B;
		TVec3<T> C;
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
			TAABB<T,3> Bounds(Tri[0],Tri[0]);
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