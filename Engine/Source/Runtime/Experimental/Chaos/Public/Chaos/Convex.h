// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Box.h"
#include "TriangleMesh.h"
#include "CollisionConvexMesh.h"

namespace Chaos
{
	template<class T, int d>
	class TConvex final : public TImplicitObject<T, d>
	{
	public:
		IMPLICIT_OBJECT_SERIALIZER(TConvex);
		TConvex()
		    : TImplicitObject<T,3>(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
		{}
		TConvex(const TConvex&) = delete;
		TConvex(TConvex&& Other)
		    : TImplicitObject<T,3>(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(Other.Planes))
		    , SurfaceParticles(MoveTemp(Other.SurfaceParticles))
		    , LocalBoundingBox(MoveTemp(Other.LocalBoundingBox))
		{}
		TConvex(const TParticles<T, 3>& InParticles)
		    : TImplicitObject<T, d>(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
		{
			const uint32 NumParticles = InParticles.Size();
			if (NumParticles == 0)
			{
				return;
			}

			TConvexBuilder<T>::Build(InParticles, Planes, SurfaceParticles, LocalBoundingBox);
		}

		static ImplicitObjectType GetType()
		{
			return ImplicitObjectType::Convex;
		}

		virtual const TBox<T, d>& BoundingBox() const override
		{
			return LocalBoundingBox;
		}

		virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
		{
			const int32 NumPlanes = Planes.Num();
			if (NumPlanes == 0)
			{
				return FLT_MAX;
			}
			check(NumPlanes > 0);

			T MaxPhi = TNumericLimits<T>::Lowest();
			int32 MaxPlane = 0;

			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				const T Phi = Planes[Idx].SignedDistance(x);
				if (Phi > MaxPhi)
				{
					MaxPhi = Phi;
					MaxPlane = Idx;
				}
			}

			return Planes[MaxPlane].PhiWithNormal(x, Normal);
		}

		virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
		{
			const int32 NumPlanes = Planes.Num();
			TArray<Pair<T, TVector<T, 3>>> Intersections;
			Intersections.Reserve(NumPlanes);
			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				auto PlaneIntersection = Planes[Idx].FindClosestIntersection(StartPoint, EndPoint, Thickness);
				if (PlaneIntersection.Second)
				{
					Intersections.Add(MakePair((PlaneIntersection.First - StartPoint).Size(), PlaneIntersection.First));
				}
			}
			Intersections.Sort([](const Pair<T, TVector<T, 3>>& Elem1, const Pair<T, TVector<T, 3>>& Elem2) { return Elem1.First < Elem2.First; });
			for (const auto& Elem : Intersections)
			{
				if (this->SignedDistance(Elem.Second) < (Thickness + 1e-4))
				{
					return MakePair(Elem.Second, true);
				}
			}
			return MakePair(TVector<T, 3>(0), false);
		}

		virtual TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const override
		{
			T MaxDot = TNumericLimits<T>::Lowest();
			int32 MaxVIdx = 0;
			const int32 NumVertices = SurfaceParticles.Size();

			check(NumVertices > 0);
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				const T Dot = TVector<T, d>::DotProduct(SurfaceParticles.X(Idx), Direction);
				if (Dot > MaxDot)
				{
					MaxDot = Dot;
					MaxVIdx = Idx;
				}
			}

			return SurfaceParticles.X(MaxVIdx);
		}

		virtual FString ToString() const
		{
			return FString::Printf(TEXT("Convex"));
		}

		const TParticles<T, d>& GetSurfaceParticles() const
		{
			return SurfaceParticles;
		}

		virtual uint32 GetTypeHash() const override
		{
			uint32 Result = LocalBoundingBox.GetTypeHash();

			Result = HashCombine(Result, SurfaceParticles.GetTypeHash());

			for(const TPlane<T, d>& Plane : Planes)
			{
				Result = HashCombine(Result, Plane.GetTypeHash());
			}

			return Result;
		}

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			TImplicitObject<T, 3>::SerializeImp(Ar);
			Ar << Planes << SurfaceParticles << LocalBoundingBox;
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			SerializeImp(Ar);
		}

		virtual void Serialize(FArchive& Ar) override
		{
			SerializeImp(Ar);
		}

	private:
		TArray<TPlane<T, d>> Planes;
		TParticles<T, d> SurfaceParticles;	//copy of the vertices that are just on the convex hull boundary
		TBox<T, d> LocalBoundingBox;
	};
}
