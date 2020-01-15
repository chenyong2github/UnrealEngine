// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/MassProperties.h"
#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "GJK.h"
#include "ChaosCheck.h"

namespace Chaos
{
	class CHAOS_API FConvex final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;
		using TType = float;
		static constexpr unsigned D = 3;

		FConvex()
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Volume(0.f)
			, CenterOfMass(FVec3(0.f))
		{}
		FConvex(const FConvex&) = delete;
		FConvex(FConvex&& Other)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(Other.Planes))
		    , SurfaceParticles(MoveTemp(Other.SurfaceParticles))
		    , LocalBoundingBox(MoveTemp(Other.LocalBoundingBox))
			, Volume(MoveTemp(Other.Volume))
			, CenterOfMass(MoveTemp(Other.CenterOfMass))
		{}

		// NOTE: This constructor will result in approximate COM and volume calculations, since it does
		// not have face indices for surface particles.
		// TODO: Keep track of invalid state and ensure on volume or COM access?
		FConvex(TArray<TPlaneConcrete<FReal, 3>>&& InPlanes, TParticles<FReal, 3>&& InSurfaceParticles)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(InPlanes))
		    , SurfaceParticles(MoveTemp(InSurfaceParticles))
		    , LocalBoundingBox(TAABB<FReal, 3>::EmptyAABB())
		{
			for (uint32 ParticleIndex = 0; ParticleIndex < SurfaceParticles.Size(); ++ParticleIndex)
			{
				LocalBoundingBox.GrowToInclude(SurfaceParticles.X(ParticleIndex));
			}

			// For now we approximate COM and volume with the bounding box
			CenterOfMass = LocalBoundingBox.GetCenterOfMass();
			Volume = LocalBoundingBox.GetVolume();
		}

		FConvex(const TParticles<FReal, 3>& InParticles)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
		{
			const uint32 NumParticles = InParticles.Size();
			if (NumParticles == 0)
			{
				return;
			}

			TArray<TArray<int32>> FaceIndices;
			FConvexBuilder::Build(InParticles, Planes, FaceIndices, SurfaceParticles, LocalBoundingBox);
			CHAOS_ENSURE(Planes.Num() == FaceIndices.Num());
			CalculateVolumeAndCenterOfMass(SurfaceParticles, FaceIndices, Volume, CenterOfMass);
		}

		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::Convex;
		}

		virtual const TAABB<FReal, 3> BoundingBox() const override
		{
			return LocalBoundingBox;
		}

		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const override
		{
			const int32 NumPlanes = Planes.Num();
			if (NumPlanes == 0)
			{
				return FLT_MAX;
			}
			check(NumPlanes > 0);

			FReal MaxPhi = TNumericLimits<FReal>::Lowest();
			int32 MaxPlane = 0;

			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				const FReal Phi = Planes[Idx].SignedDistance(x);
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
		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override
		{
			OutFaceIndex = INDEX_NONE;	//finding face is expensive, should be called directly by user
			const FRigidTransform3 StartTM(StartPoint, FRotation3::FromIdentity());
			const TSphere<FReal, 3> Sphere(FVec3(0), Thickness);
			return GJKRaycast(*this, Sphere, StartTM, Dir, Length, OutTime, OutPosition, OutNormal);
		}

		virtual Pair<FVec3, bool> FindClosestIntersectionImp(const FVec3& StartPoint, const FVec3& EndPoint, const FReal Thickness) const override
		{
			const int32 NumPlanes = Planes.Num();
			TArray<Pair<FReal, FVec3>> Intersections;
			Intersections.Reserve(FMath::Min(static_cast<int32>(NumPlanes*.1), 16)); // Was NumPlanes, which seems excessive.
			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				auto PlaneIntersection = Planes[Idx].FindClosestIntersection(StartPoint, EndPoint, Thickness);
				if (PlaneIntersection.Second)
				{
					Intersections.Add(MakePair((PlaneIntersection.First - StartPoint).SizeSquared(), PlaneIntersection.First));
				}
			}
			Intersections.Sort([](const Pair<FReal, FVec3>& Elem1, const Pair<FReal, FVec3>& Elem2) { return Elem1.First < Elem2.First; });
			for (const auto& Elem : Intersections)
			{
				if (this->SignedDistance(Elem.Second) < (Thickness + 1e-4))
				{
					return MakePair(Elem.Second, true);
				}
			}
			return MakePair(FVec3(0), false);
		}

		virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const override;

		FVec3 FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const
		{
			// For convexes, this function must be called with a face index.
			// If this ensure is getting hit, fix the caller so that it
			// passes in a valid face index.
			if (CHAOS_ENSURE(FaceIndex != INDEX_NONE))
			{
				const TPlaneConcrete<FReal, 3>& OpposingFace = GetFaces()[FaceIndex];
				return OpposingFace.Normal();
			}
			return FVec3(0.f, 0.f, 1.f);
		}

	
		virtual int32 FindClosestFaceAndVertices(const FVec3& Position, TArray<FVec3>& FaceVertices, FReal SearchDist = 0.01) const override;

		FORCEINLINE FReal GetMargin() const { return 0; }

		FORCEINLINE FVec3 Support2(const FVec3& Direction) const { return Support(Direction, 0); }

		FVec3 Support(const FVec3& Direction, const FReal Thickness) const
		{
			FReal MaxDot = TNumericLimits<FReal>::Lowest();
			int32 MaxVIdx = 0;
			const int32 NumVertices = SurfaceParticles.Size();

			check(NumVertices > 0);
			for (int32 Idx = 0; Idx < NumVertices; ++Idx)
			{
				const FReal Dot = FVec3::DotProduct(SurfaceParticles.X(Idx), Direction);
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

		const TParticles<FReal, 3>& GetSurfaceParticles() const
		{
			return SurfaceParticles;
		}

		const TArray<TPlaneConcrete<FReal, 3>>& GetFaces() const
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

			for(const TPlaneConcrete<FReal, 3>& Plane : Planes)
			{
				Result = HashCombine(Result, Plane.GetTypeHash());
			}

			return Result;
		}

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			FImplicitObject::SerializeImp(Ar);

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::ConvexUsesTPlaneConcrete)
			{
				TArray<TPlane<FReal, 3>> TmpPlanes;
				Ar << TmpPlanes;

				Planes.SetNum(TmpPlanes.Num());
				for(int32 Idx = 0; Idx < Planes.Num(); ++Idx)
				{
					Planes[Idx] = TmpPlanes[Idx].PlaneConcrete();
				}
			}
			else
			{
				Ar << Planes;
			}

			Ar << SurfaceParticles;
			TBox<FReal,3>::SerializeAsAABB(Ar, LocalBoundingBox);

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
				TParticles<FReal, 3> TempSurfaceParticles;
				FConvexBuilder::Build(SurfaceParticles, Planes, FaceIndices, TempSurfaceParticles, LocalBoundingBox);
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
			return FConvexBuilder::IsPerformanceWarning(Planes.Num(), SurfaceParticles.Size());
		}

		virtual FString PerformanceWarningAndSimplifaction() override
		{

			FString PerformanceWarningString = FConvexBuilder::PerformanceWarningString(Planes.Num(), SurfaceParticles.Size());
			if (FConvexBuilder::IsGeometryReductionEnabled())
			{
				PerformanceWarningString += ", [Simplifying]";
				SimplifyGeometry();
			}

			return PerformanceWarningString;
		}

		void SimplifyGeometry()
		{
			TArray<TArray<int32>> FaceIndices;
			FConvexBuilder::Simplify(Planes, FaceIndices, SurfaceParticles, LocalBoundingBox);
		}

		FVec3 GetCenter() const
		{
			return FVec3(0);
		}

	private:
		TArray<TPlaneConcrete<FReal, 3>> Planes;
		TParticles<FReal, 3> SurfaceParticles;	//copy of the vertices that are just on the convex hull boundary
		TAABB<FReal, 3> LocalBoundingBox;
		float Volume;
		FVec3 CenterOfMass;
	};
}
