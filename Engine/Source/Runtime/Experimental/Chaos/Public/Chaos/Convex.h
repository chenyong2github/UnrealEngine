// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Box.h"
#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "GJK.h"

namespace Chaos
{
	template<class T, int d>
	class TConvex final : public TImplicitObject<T, d>
	{
	public:
		using TImplicitObject<T, d>::GetTypeName;

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

		/** Calls \c GJKRaycast(), which may return \c true but 0 for \c OutTime, 
		 * which means the bodies are touching, but not by enough to determine \c OutPosition 
		 * and \c OutNormal should be.  The burden for detecting this case is deferred to the
		 * caller. 
		 */
		virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
		{
			OutFaceIndex = INDEX_NONE;	//finding face is expensive, should be called directly by user
			const TRigidTransform<T, d> StartTM(StartPoint, TRotation<T,d>::FromIdentity());
			const TSphere<T, d> Sphere(TVector<T, d>(0), Thickness);
			return GJKRaycast(*this, Sphere, StartTM, Dir, Length, OutTime, OutPosition, OutNormal);
		}

		virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
		{
			const int32 NumPlanes = Planes.Num();
			TArray<Pair<T, TVector<T, 3>>> Intersections;
			Intersections.Reserve(FMath::Min(static_cast<int32>(NumPlanes*.1), 16)); // Was NumPlanes, which seems excessive.
			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				auto PlaneIntersection = Planes[Idx].FindClosestIntersection(StartPoint, EndPoint, Thickness);
				if (PlaneIntersection.Second)
				{
					Intersections.Add(MakePair((PlaneIntersection.First - StartPoint).SizeSquared(), PlaneIntersection.First));
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

		virtual int32 FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex, T SearchDist) const override
		{
			//todo: use hill climbing
			int32 MostOpposingIdx = INDEX_NONE;
			T MostOpposingDot = TNumericLimits<T>::Max();
			for(int32 Idx = 0; Idx < Planes.Num(); ++Idx)
			{
				const TPlane<T, d>& Plane = Planes[Idx];
				const T Distance = Plane.SignedDistance(Position);
				if (FMath::Abs(Distance) < SearchDist)
				{
					// TPlane has an override for Normal() that doesn't call PhiWithNormal().
					const T Dot = TVector<T, d>::DotProduct(Plane.Normal(), UnitDir);
					if (Dot < MostOpposingDot)
					{
						MostOpposingDot = Dot;
						MostOpposingIdx = Idx;
					}
				}
			}
			ensure(MostOpposingIdx != INDEX_NONE);
			return MostOpposingIdx;
		}

		TVector<T, d> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 HintFaceIndex, const TVector<T, d>& OriginalNormal) const
		{
			if (HintFaceIndex != INDEX_NONE)
			{
				const TPlane<float, 3>& OpposingFace = GetFaces()[HintFaceIndex];
				return OpposingFace.Normal();
			}
		
			// todo: make a way to call FindMostOpposingFace without a search dist
			int32 MostOpposingIdx = INDEX_NONE;
			T MostOpposingDot = TNumericLimits<T>::Max();
			for (int32 Idx = 0; Idx < Planes.Num(); ++Idx)
			{
				const T Dot = TVector<T, d>::DotProduct(Planes[Idx].Normal(), DenormDir);
				if (Dot < MostOpposingDot)
				{
					MostOpposingDot = Dot;
					MostOpposingIdx = Idx;
				}
			}
			ensure(MostOpposingIdx != INDEX_NONE);
			return Planes[MostOpposingIdx].Normal();
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

			if (Thickness)
			{
				return SurfaceParticles.X(MaxVIdx) + SurfaceParticles.X(MaxVIdx).GetSafeNormal()*Thickness;
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

		const TArray<TPlane<T, d>>& GetFaces() const
		{
			return Planes;
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
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			SerializeImp(Ar);
		}

		virtual void Serialize(FArchive& Ar) override
		{
			SerializeImp(Ar);
		}

		virtual bool IsValidGeometry() const override
		{
			return (SurfaceParticles.Size() > 0 && Planes.Num() > 0);
		}

		virtual bool IsPerformanceWarning() const override
		{
			return TConvexBuilder<T>::IsPerformanceWarning(Planes.Num(), SurfaceParticles.Size());
		}

		virtual FString PerformanceWarningAndSimplifaction() override
		{

			FString PerformanceWarningString = TConvexBuilder<T>::PerformanceWarningString(Planes.Num(), SurfaceParticles.Size());
			if (TConvexBuilder<T>::IsGeometryReductionEnabled())
			{
				PerformanceWarningString += ", [Simplifying]";
				SimplifyGeometry();
			}

			return PerformanceWarningString;
		}

		void SimplifyGeometry()
		{
			TConvexBuilder<T>::Simplify(Planes, SurfaceParticles, LocalBoundingBox);
		}

	private:
		TArray<TPlane<T, d>> Planes;
		TParticles<T, d> SurfaceParticles;	//copy of the vertices that are just on the convex hull boundary
		TBox<T, d> LocalBoundingBox;
	};
}
