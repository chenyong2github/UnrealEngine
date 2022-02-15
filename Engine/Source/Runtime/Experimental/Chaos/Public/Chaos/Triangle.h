// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Core.h"
#include "Chaos/GJK.h"
#include "ImplicitObject.h"
#include "Plane.h"
#include "Chaos/Plane.h"

namespace Chaos
{
	class FTriangle
	{
	public:
		FTriangle()
		{
		}

		FTriangle(const FVec3& InA, const FVec3& InB, const FVec3& InC)
			: ABC{ InA, InB, InC }
		{
		}

		FORCEINLINE FVec3& operator[](uint32 InIndex)
		{
			checkSlow(InIndex < 3);
			return ABC[InIndex];
		}

		FORCEINLINE const FVec3& operator[](uint32 InIndex) const
		{
			checkSlow(InIndex < 3);
			return ABC[InIndex];
		}

		FORCEINLINE const FVec3& GetVertex(const int32 InIndex) const
		{
			checkSlow(InIndex < 3);
			return ABC[InIndex];
		}

		FORCEINLINE FVec3 GetNormal() const
		{
			return FVec3::CrossProduct(ABC[1] - ABC[0], ABC[2] - ABC[0]).GetSafeNormal();
		}

		FORCEINLINE TPlane<FReal, 3> GetPlane() const
		{
			return TPlane<FReal, 3>(ABC[0], GetNormal());
		}

		// Face index is ignored since we only have one face
		// Used for manifold generation
		FORCEINLINE TPlaneConcrete<FReal, 3> GetPlane(int32 FaceIndex) const
		{
			return TPlaneConcrete < FReal, 3> (ABC[0], GetNormal());
		}

		FORCEINLINE void GetPlaneNX(const int32 FaceIndex, FVec3& OutN, FVec3& OutX) const
		{
			OutN = GetNormal();
			OutX = ABC[0];
		}

		// Get the nearest point on an edge and the edge vertices
		// Used for manifold generation
		FVec3 GetClosestEdge(int32 PlaneIndexHint, const FVec3& Position, FVec3& OutEdgePos0, FVec3& OutEdgePos1) const
		{
			FVec3 ClosestEdgePosition = FVec3(0);
			FReal ClosestDistanceSq = TNumericLimits<FReal>::Max();

			int32 PlaneVerticesNum = 3;
			
			FVec3 P0 = ABC[2];
			for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < PlaneVerticesNum; ++PlaneVertexIndex)
			{
				const TVector<FReal, 3>& P1 = GetVertex(PlaneVertexIndex);
				
				const FVec3 EdgePosition = FMath::ClosestPointOnLine(P0, P1, Position);
				const FReal EdgeDistanceSq = (EdgePosition - Position).SizeSquared();

				if (EdgeDistanceSq < ClosestDistanceSq)
				{
					ClosestDistanceSq = EdgeDistanceSq;
					ClosestEdgePosition = EdgePosition;
					OutEdgePos0 = P0;
					OutEdgePos1 = P1;
				}

				P0 = P1;
			}

			return ClosestEdgePosition;
		}

		// Get the nearest point on an edge
		// Used for manifold generation
		FVec3 GetClosestEdgePosition(int32 PlaneIndexHint, const FVec3& Position) const
		{
			FVec3 Unused0, Unused1;
			return GetClosestEdge(PlaneIndexHint, Position, Unused0, Unused1);
		}


		// The number of vertices that make up the corners of the specified face
		// Used for manifold generation
		int32 NumPlaneVertices(int32 PlaneIndex) const
		{
			return 3;
		}

		// Returns a winding order multiplier used in the manifold clipping and required when we have negative scales (See ImplicitObjectScaled)
		// Used for manifold generation
		FORCEINLINE FReal GetWindingOrder() const
		{
			return 1.0f;
		}

		// Get an array of all the plane indices that belong to a vertex (up to MaxVertexPlanes).
		// Returns the number of planes found.
		FORCEINLINE int32 FindVertexPlanes(int32 VertexIndex, int32* OutVertexPlanes, int32 MaxVertexPlanes) const
		{
			if(MaxVertexPlanes > 0)
			{
				OutVertexPlanes[0] = 0;
			}
			return 1; 
		}
		
		// Get up to the 3  plane indices that belong to a vertex
		// Returns the number of planes found.
		int32 GetVertexPlanes3(int32 VertexIndex, int32& PlaneIndex0, int32& PlaneIndex1, int32& PlaneIndex2) const
		{
			PlaneIndex0 = 0;
			return 1;
		}
		
		// Get the index of the plane that most opposes the normal
		int32 GetMostOpposingPlane(const FVec3& Normal) const
		{
			return 0; // Only have one plane
		}

		// Get the vertex index of one of the vertices making up the corners of the specified face
		// Used for manifold generation
		int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
		{
			return PlaneVertexIndex;
		}

		// Triangle is just one plane
		// Used for manifold generation
		int32 NumPlanes() const { return 1; }

		FORCEINLINE FReal PhiWithNormal(const FVec3& InSamplePoint, FVec3& OutNormal) const
		{
			OutNormal = GetNormal();
			FVec3 ClosestPoint = FindClosestPointOnTriangle(GetPlane(), ABC[0], ABC[1], ABC[2], InSamplePoint);
			return FVec3::DotProduct((InSamplePoint - ClosestPoint), OutNormal);
		}

		FORCEINLINE FVec3 Support(const FVec3& Direction, const FReal Thickness, int32& VertexIndex) const
		{
			const FReal DotA = FVec3::DotProduct(ABC[0], Direction);
			const FReal DotB = FVec3::DotProduct(ABC[1], Direction);
			const FReal DotC = FVec3::DotProduct(ABC[2], Direction);

			if(DotA >= DotB && DotA >= DotC)
			{
				VertexIndex = 0;
				if(Thickness != 0)
				{
					return ABC[0] + Direction.GetUnsafeNormal() * Thickness;
				}
				return ABC[0];
			}
			else if(DotB >= DotA && DotB >= DotC)
			{
				VertexIndex = 1;
				if(Thickness != 0)
				{
					return ABC[1] + Direction.GetUnsafeNormal() * Thickness;
				}
				return ABC[1];
			}
			VertexIndex = 2;
			if(Thickness != 0)
			{
				return ABC[2] + Direction.GetUnsafeNormal() * Thickness;
			}
			return ABC[2];
		}

		FORCEINLINE_DEBUGGABLE FVec3 SupportCore(const FVec3& Direction, const FReal InMargin, FReal* OutSupportDelta,int32& VertexIndex) const
		{
			// Note: assumes margin == 0
			const FReal DotA = FVec3::DotProduct(ABC[0], Direction);
			const FReal DotB = FVec3::DotProduct(ABC[1], Direction);
			const FReal DotC = FVec3::DotProduct(ABC[2], Direction);

			if (DotA >= DotB && DotA >= DotC)
			{
				VertexIndex = 0;
				return ABC[0];
			}
			else if (DotB >= DotA && DotB >= DotC)
			{
				VertexIndex = 1;
				return ABC[1];
			}
			VertexIndex = 2;
			return ABC[2];
		}

		FORCEINLINE FVec3 SupportCoreScaled(const FVec3& Direction, FReal InMargin, const FVec3& Scale, FReal* OutSupportDelta, int32& VertexIndex) const
		{
			// Note: ignores InMargin, assumed 0 (triangles cannot have a margin as they are zero thickness)
			return SupportCore(Direction * Scale, 0.0f, OutSupportDelta, VertexIndex) * Scale;
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
			const FVec3 ClosestPoint = FindClosestPointOnTriangle(GetPlane(), ABC[0], ABC[1], ABC[2], Point);
			const FReal AdjustedThickness = FMath::Max(Thickness, KINDA_SMALL_NUMBER);
			return (Point - ClosestPoint).SizeSquared() <= (AdjustedThickness * AdjustedThickness);
		}

		FORCEINLINE bool IsConvex() const
		{
			return true;
		}

	private:

		friend FChaosArchive& operator<<(FChaosArchive& Ar, FTriangle& Value);

		FVec3 ABC[3];
	};

	inline FChaosArchive& operator<<(FChaosArchive& Ar, FTriangle& Value)
	{
		Ar << Value.ABC[0] << Value.ABC[1] << Value.ABC[2];
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

		virtual TVec3<T> Support(const TVec3<T>& Direction, const T Thickness, int32& VertexIndex) const override
		{
			return Tri.Support(Direction, Thickness, VertexIndex);
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
			return HashCombine(UE::Math::GetTypeHash(Tri[0]), HashCombine(UE::Math::GetTypeHash(Tri[1]), UE::Math::GetTypeHash(Tri[2])));
		}

		virtual FName GetTypeName() const override
		{
			return FName("Triangle");
		}

	private:

		TTriangle<T> Tri;
	};
}