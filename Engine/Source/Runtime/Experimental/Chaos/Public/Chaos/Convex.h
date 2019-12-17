// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Box.h"
#include "Chaos/MassProperties.h"
#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "GJK.h"

namespace Chaos
{
	template<class T, int d>
	class TConvex final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		TConvex()
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Volume(0.f)
			, CenterOfMass(FVec3(0.f))
		{}
		TConvex(const TConvex&) = delete;
		TConvex(TConvex&& Other)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(Other.Planes))
		    , SurfaceParticles(MoveTemp(Other.SurfaceParticles))
		    , LocalBoundingBox(MoveTemp(Other.LocalBoundingBox))
			, Volume(MoveTemp(Other.MoveTemp))
			, CenterOfMass(MoveTemp(Other.CenterOfMass))
		{}
		TConvex(TArray<TPlane<T, d>>&& InPlanes, TArray<TArray<int32>>&& InFaceIndices, TParticles<T, d>&& InSurfaceParticles)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(InPlanes))
		    , SurfaceParticles(MoveTemp(InSurfaceParticles))
		    , LocalBoundingBox(TBox<T, d>::EmptyBox())
		{
			for (uint32 ParticleIndex = 0; ParticleIndex < SurfaceParticles.Size(); ++ParticleIndex)
			{
				LocalBoundingBox.GrowToInclude(SurfaceParticles.X(ParticleIndex));
			}

			TArray<TArray<int32>> FaceIndices;
			CalculateVolumeAndCenterOfMass(SurfaceParticles, FaceIndices, Volume, CenterOfMass);
		}
		TConvex(const TParticles<T, 3>& InParticles)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
		{
			const uint32 NumParticles = InParticles.Size();
			if (NumParticles == 0)
			{
				return;
			}

			TArray<TArray<int32>> FaceIndices;
			TConvexBuilder<T>::Build(InParticles, Planes, FaceIndices, SurfaceParticles, LocalBoundingBox);
			CHAOS_ENSURE(Planes.Num() == FaceIndices.Num());
			CalculateVolumeAndCenterOfMass(SurfaceParticles, FaceIndices, Volume, CenterOfMass);
		}

		static constexpr EImplicitObjectType StaticType()
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

		TVector<T, d> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 FaceIndex, const TVector<T, d>& OriginalNormal) const
		{
			// For convexes, this function must be called with a face index.
			// If this ensure is getting hit, fix the caller so that it
			// passes in a valid face index.
			if (ensure(FaceIndex != INDEX_NONE))
			{
				const TPlane<float, 3>& OpposingFace = GetFaces()[FaceIndex];
				return OpposingFace.Normal();
			}
			return TVector<float, 3>(0.f, 0.f, 1.f);
		}

		FORCEINLINE T GetMargin() const { return 0; }

		FORCEINLINE TVector<T, d> Support2(const TVector<T, d>& Direction) const { return Support(Direction, 0); }

		TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const
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
				return SurfaceParticles.X(MaxVIdx) + Direction.GetUnsafeNormal() * Thickness;
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

		const FReal GetVolume() const
		{
			return Volume;
		}

		const FMatrix33 GetInertiaTensor(const FReal Mass) const
		{
			// TODO: More precise inertia!
			return LocalBoundingBox.GetInertiaTensor(Mass);
		}

		const FVec3 GetCenterOfMass() const
		{
			return CenterOfMass;
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
			FImplicitObject::SerializeImp(Ar);
			Ar << Planes << SurfaceParticles << LocalBoundingBox;

			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddConvexCenterOfMassAndVolume)
			{
				Ar << Volume;
				Ar << CenterOfMass;
			}
			else if (Ar.IsLoading())
			{
				// Rebuild convex in order to extract face indices.
				// TODO: Make it so it can take SurfaceParticles as both input and output without breaking...
				TArray<TArray<int32>> FaceIndices;
				TParticles<T, d> TempSurfaceParticles;
				TConvexBuilder<T>::Build(SurfaceParticles, Planes, FaceIndices, TempSurfaceParticles, LocalBoundingBox);
				CalculateVolumeAndCenterOfMass(SurfaceParticles, FaceIndices, Volume, CenterOfMass);
			}
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
			TArray<TArray<int32>> FaceIndices;
			TConvexBuilder<T>::Simplify(Planes, FaceIndices, SurfaceParticles, LocalBoundingBox);
		}

		TVector<T,d> GetCenter() const
		{
			return TVector<T, d>(0);
		}

	private:
		TArray<TPlane<T, d>> Planes;
		TParticles<T, d> SurfaceParticles;	//copy of the vertices that are just on the convex hull boundary
		TBox<T, d> LocalBoundingBox;
		float Volume;
		FVec3 CenterOfMass;
	};
}
