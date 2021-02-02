// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/ConvexStructureData.h"
#include "Chaos/MassProperties.h"
#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "GJK.h"
#include "ChaosCheck.h"
#include "ChaosLog.h"
#include "UObject/ReleaseObjectVersion.h"

namespace Chaos
{
	//
	// Note: While Convex technically supports a margin, the margin is typically a property of the
	// instance wrapper (ImplicitScaled, ImplicitTransformed, or ImplicitInstanced). Usually the
	// margin on the convex itself is zero.
	//
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
			, StructureData(MoveTemp(Other.StructureData))
			, Volume(MoveTemp(Other.Volume))
			, CenterOfMass(MoveTemp(Other.CenterOfMass))
		{}

		// NOTE: This constructor will result in approximate COM and volume calculations, since it does
		// not have face indices for surface particles.
		// NOTE: Convex constructed this way will not contain any structure data
		// @todo(chaos): Keep track of invalid state and ensure on volume or COM access?
		// @todo(chaos): Add plane vertex indices in the constructor and call CreateStructureData
		// @todo(chaos): Merge planes? Or assume the input is a good convex hull?
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

		FConvex(const TParticles<FReal, 3>& InParticles, const FReal InMargin)
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

			// @todo(chaos): this only works with triangles. Fix that an we can run MergeFaces before calling this
			CalculateVolumeAndCenterOfMass(SurfaceParticles, FaceIndices, Volume, CenterOfMass);

			// @todo(chaos): DistanceTolerance should be based on size, or passed in
			const FReal DistanceTolerance = 1.0f;
			FConvexBuilder::MergeFaces(Planes, FaceIndices, SurfaceParticles, DistanceTolerance);
			CHAOS_ENSURE(Planes.Num() == FaceIndices.Num());

			CreateStructureData(MoveTemp(FaceIndices));

			SetMargin(InMargin);
		}

	private:
		void MovePlanesAndRebuild(FReal InDelta);

		void CreateStructureData(TArray<TArray<int32>>&& FaceIndices);

	public:
		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::Convex;
		}

		virtual const TAABB<FReal, 3> BoundingBox() const override
		{
			return LocalBoundingBox;
		}

		// Return the distance to the surface
		virtual FReal PhiWithNormal(const FVec3& X, FVec3& Normal) const override
		{
			return PhiWithNormalInternal(X, Normal);
		}

		FReal PhiWithNormalScaled(const FVec3& X, const FVec3& Scale, const FVec3& InvScale, FVec3& Normal) const
		{
			return PhiWithNormalScaledInternal(X, Scale, Normal);
		}


	private:
		// Distance to the surface
		FReal PhiWithNormalInternal(const FVec3& X, FVec3& Normal) const
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
				const FReal Phi = Planes[Idx].SignedDistance(X);
				if (Phi > MaxPhi)
				{
					MaxPhi = Phi;
					MaxPlane = Idx;
				}
			}

			FReal Phi = Planes[MaxPlane].PhiWithNormal(X, Normal);
			if (Phi <= 0)
			{
				return Phi;
			}

			// If x is outside the convex mesh, we should find for the nearest point to triangles on the plane
			const int32 PlaneVerticesNum = NumPlaneVertices(MaxPlane);
			const FVec3 XOnPlane = X - Phi * Normal;
			FReal ClosestDistance = TNumericLimits<FReal>::Max();
			FVec3 ClosestPoint;
			for (int32 Index = 0; Index < PlaneVerticesNum - 2; Index++)
			{
				const FVec3 A(GetVertex(GetPlaneVertex(MaxPlane, 0)));
				const FVec3 B(GetVertex(GetPlaneVertex(MaxPlane, Index + 1)));
				const FVec3 C(GetVertex(GetPlaneVertex(MaxPlane, Index + 2)));

				const FVec3 Point = FindClosestPointOnTriangle(XOnPlane, A, B, C, X);
				if (XOnPlane == X)
				{
					return Phi;
				}

				const FReal Distance = (Point - XOnPlane).Size();
				if (Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					ClosestPoint = Point;
				}
			}

			const TVector<FReal, 3> Difference = X - ClosestPoint;
			Phi = Difference.Size();
			if (Phi > SMALL_NUMBER)
			{
				Normal = (Difference) / Phi;
			}
			return Phi;
		}

		// Distance from a point to the surface for use in the scaled version. When the convex
		// is scaled, we need to bias the depth calculation to take into account the world-space scale
		FReal PhiWithNormalScaledInternal(const FVec3& X, const FVec3& Scale, FVec3& Normal) const
		{
			const int32 NumPlanes = Planes.Num();
			if (NumPlanes == 0)
			{
				return FLT_MAX;
			}
			check(NumPlanes > 0);

			FReal MaxPhi = TNumericLimits<FReal>::Lowest();
			FVec3 MaxNormal = FVec3(0,0,1);
			int32 MaxPlane = 0;
			for (int32 Idx = 0; Idx < NumPlanes; ++Idx)
			{
				FVec3 PlaneNormal = (Planes[Idx].Normal() * Scale).GetUnsafeNormal();
				FVec3 PlanePos = Planes[Idx].X() * Scale;
				FReal PlaneDistance = FVec3::DotProduct(X - PlanePos, PlaneNormal);
				if (PlaneDistance > MaxPhi)
				{
					MaxPhi = PlaneDistance;
					MaxNormal = PlaneNormal;
					MaxPlane = Idx;
				}
			}

			Normal = MaxNormal;

			if (MaxPhi < 0)
			{
				return MaxPhi;
			}

			// If X is outside the convex mesh, we should find for the nearest point to triangles on the plane
			const int32 PlaneVerticesNum = NumPlaneVertices(MaxPlane);
			const FVec3 XOnPlane = X - MaxPhi * Normal;
			FReal ClosestDistance = TNumericLimits<FReal>::Max();
			FVec3 ClosestPoint;
			for (int32 Index = 0; Index < PlaneVerticesNum - 2; Index++)
			{
				const FVec3 A(Scale * GetVertex(GetPlaneVertex(MaxPlane, 0)));
				const FVec3 B(Scale * GetVertex(GetPlaneVertex(MaxPlane, Index + 1)));
				const FVec3 C(Scale * GetVertex(GetPlaneVertex(MaxPlane, Index + 2)));

				const FVec3 Point = FindClosestPointOnTriangle(XOnPlane, A, B, C, X);
				if (XOnPlane == X)
				{
					return MaxPhi;
				}

				const FReal Distance = (Point - XOnPlane).Size();
				if (Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					ClosestPoint = Point;
				}
			}

			const FVec3 Difference = X - ClosestPoint;
			const FReal DifferenceLen = Difference.Size();
			if (DifferenceLen > SMALL_NUMBER)
			{
				Normal = Difference / DifferenceLen;
				MaxPhi = DifferenceLen;
			}
			return MaxPhi;
		}


	public:
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

		// Whether the structure data has been created for this convex (will eventually always be true)
		bool HasStructureData() const { return StructureData.IsValid(); }

		// The convex structure data (mainly exposed for testing)
		const FConvexStructureData& GetStructureData() const { return StructureData; }

		// Get the index of the plane that most opposes the normal
		int32 GetMostOpposingPlane(const FVec3& Normal) const;

		// Get the index of the plane that most opposes the normal, assuming it passes through the specified vertex
		int32 GetMostOpposingPlaneWithVertex(int32 VertexIndex, const FVec3& Normal) const;

		// Get the nearest point on an edge of the specified face
		FVec3 GetClosestEdgePosition(int32 PlaneIndex, const FVec3& Position) const;


		// The number of planes that use the specified vertex
		int32 NumVertexPlanes(int32 VertexIndex) const;

		// Get the plane index of one of the planes that uses the specified vertex
		int32 GetVertexPlane(int32 VertexIndex, int32 VertexPlaneIndex) const;

		// The number of vertices that make up the corners of the specified face
		int32 NumPlaneVertices(int32 PlaneIndex) const;

		// Get the vertex index of one of the vertices making up the corners of the specified face
		int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const;

		int32 NumPlanes() const
		{
			return Planes.Num();
		}

		int32 NumVertices() const
		{
			return (int32)SurfaceParticles.Size();
		}

		// Get the plane at the specified index (e.g., indices from GetVertexPlane)
		const TPlaneConcrete<FReal, 3>& GetPlane(int32 FaceIndex) const
		{
			return Planes[FaceIndex];
		}

		// Get the vertex at the specified index (e.g., indices from GetPlaneVertex)
		const FVec3& GetVertex(int32 VertexIndex) const
		{
			return SurfaceParticles.X(VertexIndex);
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

		// Returns a winding order multiplier used in the manifold clipping and required when we have negative scales (See ImplicitObjectScaled)
		float GetWindingOrder() const
		{
			return 1.0f;
		}

	private:
		int32 GetSupportVertex(const FVec3& Direction) const
		{
			FReal MaxDot = TNumericLimits<FReal>::Lowest();
			int32 MaxVIdx = INDEX_NONE;
			const int32 NumVertices = SurfaceParticles.Size();

			if (ensure(NumVertices > 0))
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					const FReal Dot = FVec3::DotProduct(SurfaceParticles.X(Idx), Direction);
					if (Dot > MaxDot)
					{
						MaxDot = Dot;
						MaxVIdx = Idx;
					}
				}
			}

			return MaxVIdx;
		}

		FVec3 GetMarginAdjustedVertex(int32 VertexIndex, float InMargin) const
		{
			// @chaos(todo): moving the vertices this way based on margin is only valid for small margins. If the margin
			// is large enough to cause a face to reduce to zero size, vertices should be merged and the path is non-linear.
			// This can be fixed with some extra data in the convex structure, but for now we accept the fact that large 
			// margins on convexes with small faces can cause non-convex core shapes.

			// Get any 3 planes that contribute to this vertex
			if (NumVertexPlanes(VertexIndex) >= 3)
			{
				const int32 PlaneIndex0 = GetVertexPlane(VertexIndex, 0);
				const int32 PlaneIndex1 = GetVertexPlane(VertexIndex, 1);
				const int32 PlaneIndex2 = GetVertexPlane(VertexIndex, 2);

				// Move the planes by the margin and recalculate the interection
				// @todo(chaos): calculate dV/dm per vertex and store it in StructureData
				FVec3 PlanesPos;
				FPlane NewPlanes[3] =
				{
					FPlane(Planes[PlaneIndex0].X() - InMargin * Planes[PlaneIndex0].Normal(), Planes[PlaneIndex0].Normal()),
					FPlane(Planes[PlaneIndex1].X() - InMargin * Planes[PlaneIndex1].Normal(), Planes[PlaneIndex1].Normal()),
					FPlane(Planes[PlaneIndex2].X() - InMargin * Planes[PlaneIndex2].Normal(), Planes[PlaneIndex2].Normal()),
				};
				if (FMath::IntersectPlanes3(PlanesPos, NewPlanes[0], NewPlanes[1], NewPlanes[2]))
				{
					return PlanesPos;
				}
			}

			return FVec3(0);
		}

		FVec3 GetMarginAdjustedVertexScaled(int32 VertexIndex, float InMargin, const FVec3& Scale) const
		{
			// Get any 3 planes that contribute to this vertex
			if (NumVertexPlanes(VertexIndex) >= 3)
			{
				const int32 PlaneIndex0 = GetVertexPlane(VertexIndex, 0);
				const int32 PlaneIndex1 = GetVertexPlane(VertexIndex, 1);
				const int32 PlaneIndex2 = GetVertexPlane(VertexIndex, 2);

				// Move the planes by the margin and recalculate the interection
				// @todo(chaos): calculate dV/dm per vertex and store it in StructureData (but see todo above)
				const FVec3 NewPlaneX = Scale * GetVertex(VertexIndex);
				const FVec3 NewPlaneNs[3] = 
				{
					(Scale * Planes[PlaneIndex0].Normal()).GetUnsafeNormal(),
					(Scale * Planes[PlaneIndex1].Normal()).GetUnsafeNormal(),
					(Scale * Planes[PlaneIndex2].Normal()).GetUnsafeNormal(),
				};
				FReal NewPlaneDs[3] = 
				{
					FVec3::DotProduct(NewPlaneX, NewPlaneNs[0]) - InMargin,
					FVec3::DotProduct(NewPlaneX, NewPlaneNs[1]) - InMargin,
					FVec3::DotProduct(NewPlaneX, NewPlaneNs[2]) - InMargin,
				};
				FPlane NewPlanes[3] =
				{
					FPlane(NewPlaneNs[0], NewPlaneDs[0]),
					FPlane(NewPlaneNs[1], NewPlaneDs[1]),
					FPlane(NewPlaneNs[2], NewPlaneDs[2]),
				};

				FVec3 AdjustedVertexPos;
				if (FMath::IntersectPlanes3(AdjustedVertexPos, NewPlanes[0], NewPlanes[1], NewPlanes[2]))
				{
					return AdjustedVertexPos;
				}
			}

			return FVec3(0);
		}

	public:
		// Return support point on the core shape (the convex shape with all planes moved inwards by margin).
		FVec3 SupportCore(const FVec3& Direction, float InMargin) const
		{
			const int32 SupportVertexIndex = GetSupportVertex(Direction);
			if (SupportVertexIndex != INDEX_NONE)
			{
				// @chaos(todo): apply an upper limit to the margin to prevent an inverted shape
				const float NetMargin = InMargin + GetMargin();
				if (NetMargin > SMALL_NUMBER)
				{
					return GetMarginAdjustedVertex(SupportVertexIndex, NetMargin);
				}
				return SurfaceParticles.X(SupportVertexIndex);
			}
			return FVec3(0);
		}

		// SupportCore with non-uniform scale support. This is required for the margin in scaled
		// space to by uniform. Note in this version all the inputs are in outer container's (scaled shape) space
		FVec3 SupportCoreScaled(const FVec3& Direction, float InMargin, const FVec3& Scale) const
		{
			// Find the supporting vertex index
			const FVec3 DirectionScaled = Scale * Direction;	// Not normalized
			const int32 SupportVertexIndex = GetSupportVertex(DirectionScaled);

			// Adjust the vertex position based on margin
			FVec3 VertexPosition = FVec3(0);
			if (SupportVertexIndex != INDEX_NONE)
			{
				// Note: Shapes wrapped in a non-uniform scale should not have their own margin
				// because we do not support non-uniformly scaled margins on convex (to do so would 
				// require that we dupe the convex data for each scale).
				const FReal InverseScale = 1.0f / Scale[0];
				const float NetMargin = InMargin + InverseScale * GetMargin();

				// @chaos(todo): apply an upper limit to the margin to prevent a non-convex or null shape (also see comments in GetMarginAdjustedVertex)
				if (NetMargin > SMALL_NUMBER)
				{
					VertexPosition = GetMarginAdjustedVertexScaled(SupportVertexIndex, NetMargin, Scale);
				}
				else
				{
					VertexPosition = Scale * SurfaceParticles.X(SupportVertexIndex);
				}
			}
			return VertexPosition;
		}

		// Return support point on the shape
		FORCEINLINE FVec3 Support(const FVec3& Direction, const FReal Thickness) const
		{
			const int32 MaxVIdx = GetSupportVertex(Direction);
			if (MaxVIdx != INDEX_NONE)
			{
				if (Thickness != 0.0f)
				{
					return SurfaceParticles.X(MaxVIdx) + Direction.GetUnsafeNormal() * Thickness;
				}
				return SurfaceParticles.X(MaxVIdx);
			}
			return FVec3(0);
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

			Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
			if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::MarginAddedToConvexAndBox)
			{
				Ar << FImplicitObject::Margin;
			}

			if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::StructureDataAddedToConvex)
			{
				Ar << StructureData;
			}
			else if (Ar.IsLoading())
			{
				// Generate the structure data from the planes and vertices
				TArray<TArray<int32>> FaceIndices;
				FConvexBuilder::BuildPlaneVertexIndices(Planes, SurfaceParticles, FaceIndices);
				CreateStructureData(MoveTemp(FaceIndices));
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

			// @todo(chaos): DistanceTolerance should be based on size, or passed in
			const FReal DistanceTolerance = 1.0f;
			FConvexBuilder::MergeFaces(Planes, FaceIndices, SurfaceParticles, DistanceTolerance);

			CreateStructureData(MoveTemp(FaceIndices));
		}

		FVec3 GetCenter() const
		{
			return FVec3(0);
		}

	private:
		TArray<TPlaneConcrete<FReal, 3>> Planes;
		TParticles<FReal, 3> SurfaceParticles;	//copy of the vertices that are just on the convex hull boundary
		TAABB<FReal, 3> LocalBoundingBox;
		FConvexStructureData StructureData;
		float Volume;
		FVec3 CenterOfMass;
	};
}
