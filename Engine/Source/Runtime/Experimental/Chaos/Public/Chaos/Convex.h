// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/MassProperties.h"
#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "GJK.h"
#include "ChaosCheck.h"
#include "ChaosLog.h"
#include "UObject/ReleaseObjectVersion.h"

namespace Chaos
{
	// Metadata for a convex shape used by the manifold generation system and anything
	// else that can benefit from knowing which vertices are associated with the faces.
	// @todo(chaos): support asset-dependent index size (8, 16, 32 bit). Make GetVertexPlanes and GetPlaneVertices return a converting array view.
	class CHAOS_API FConvexStructureData
	{
	public:
		bool IsValid() const
		{
			return PlaneVertices.Num() > 0;
		}

		TArrayView<const int32> GetVertexPlanes(int32 VertexIndex) const
		{
			return MakeArrayView(VertexPlanes[VertexIndex]);
		}
		
		TArrayView<const int32> GetPlaneVertices(int32 FaceIndex) const
		{
			return MakeArrayView(PlaneVertices[FaceIndex]);
		}

		void SetPlaneVertices(TArray<TArray<int32>>&& InPlaneVertices, int32 NumVerts)
		{
			// Steal the arrays of vertices per plane
			PlaneVertices = MoveTemp(InPlaneVertices);

			// Generate the arrays of planes per vertex
			VertexPlanes.SetNum(NumVerts);
			for (int32 PlaneIndex = 0; PlaneIndex < PlaneVertices.Num(); ++PlaneIndex)
			{
				for (int32 VertexIndex = 0; VertexIndex < PlaneVertices[PlaneIndex].Num(); ++VertexIndex)
				{
					const int32 PlaneVertexIndex = PlaneVertices[PlaneIndex][VertexIndex];
					VertexPlanes[PlaneVertexIndex].Add(PlaneIndex);
				}
			}
		}

		void Serialize(FArchive& Ar)
		{
			Ar << PlaneVertices;
			Ar << VertexPlanes;
		}

		friend FArchive& operator<<(FArchive& Ar, FConvexStructureData& Value)
		{
			Value.Serialize(Ar);
			return Ar;
		}

	private:
		// For each face: the set of vertex indices that form the corners of the face in counter-clockwise order
		TArray<TArray<int32>> PlaneVertices;

		// For each vertex: the set of face indices that use the vertex
		TArray<TArray<int32>> VertexPlanes;
	};


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
		    , Vertices(MoveTemp(Other.Vertices))
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
		FConvex(TArray<TPlaneConcrete<FReal, 3>>&& InPlanes, TArray<FVec3>&& InVertices)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(InPlanes))
		    , Vertices(MoveTemp(InVertices))
		    , LocalBoundingBox(TAABB<FReal, 3>::EmptyAABB())
		{
			for (int32 ParticleIndex = 0; ParticleIndex < Vertices.Num(); ++ParticleIndex)
			{
				LocalBoundingBox.GrowToInclude(Vertices[ParticleIndex]);
			}

			// For now we approximate COM and volume with the bounding box
			CenterOfMass = LocalBoundingBox.GetCenterOfMass();
			Volume = LocalBoundingBox.GetVolume();
		}

		FConvex(const TArray<FVec3>& InVertices, const FReal InMargin)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
		{
			const int32 NumVertices = InVertices.Num();
			if (NumVertices == 0)
			{
				return;
			}

			TArray<TArray<int32>> FaceIndices;
			FConvexBuilder::Build(InVertices, Planes, FaceIndices, Vertices, LocalBoundingBox);
			CHAOS_ENSURE(Planes.Num() == FaceIndices.Num());

			// @chaos(todo): this only works with triangles. Fix that an we can run MergeFaces before calling this
			TParticles<FReal, 3> VertexParticles(CopyTemp(Vertices));
			CalculateVolumeAndCenterOfMass(VertexParticles, FaceIndices, Volume, CenterOfMass);

			FConvexBuilder::MergeFaces(Planes, FaceIndices, Vertices);
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
		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const override
		{
			float Phi = PhiWithNormalInternal(x, Normal);
			if (Phi > 0)
			{
				//Outside convex, so test against bounding box - this is done to avoid
				//inaccurate results given by PhiWithNormalInternal when x is far outside

				FVec3 SnappedPosition, BoundingNormal;
				const float BoundingPhi = LocalBoundingBox.PhiWithNormal(x, BoundingNormal);
				if (BoundingPhi <= 0)
				{
					//Inside bounding box - snap to convex
					SnappedPosition = x - Phi * Normal;
				}
				else
				{
					//Snap to bounding box, and test convex
					SnappedPosition = x - BoundingPhi * BoundingNormal;
					Phi = PhiWithNormalInternal(SnappedPosition, Normal);
					SnappedPosition -= Phi * Normal;
				}

				//one final snap to ensure we're on the surface
				Phi = PhiWithNormalInternal(SnappedPosition, Normal);
				SnappedPosition -= Phi * Normal;

				//Return phi/normal based on distance from original position to snapped position
				const FVec3 Difference = x - SnappedPosition;
				Phi = Difference.Size();
				if (CHAOS_ENSURE(Phi > TNumericLimits<float>::Min())) //Phi shouldn't be 0 here since we only enter this block if x was outside the convex
				{
					Normal = (Difference) / Phi;
				}
				else
				{
					Normal = FVector::ForwardVector;
				}
			}
			return Phi;
		}

	private:
		// Distance to the surface
		FReal PhiWithNormalInternal(const FVec3& x, FVec3& Normal) const
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

		// Get the index of the plane that most opposes the normal
		int32 GetMostOpposingPlane(const FVec3& Normal) const;

		// Get the index of the plane that most opposes the normal, assuming it passes through the specified vertex
		int32 GetMostOpposingPlaneWithVertex(int32 VertexIndex, const FVec3& Normal) const;

		// Get the nearest point on an edge of the specified face
		FVec3 GetClosestEdgePosition(int32 PlaneIndex, const FVec3& Position) const;

		// Get the set of planes that pass through the specified vertex
		TArrayView<const int32> GetVertexPlanes(int32 VertexIndex) const;

		// Get the list of vertices that form the boundary of the specified face
		TArrayView<const int32> GetPlaneVertices(int32 FaceIndex) const;

		int32 NumPlanes() const
		{
			return Planes.Num();
		}

		int32 NumVertices() const
		{
			return (int32)Vertices.Num();
		}

		// Get the plane at the specified index (e.g., indices from GetVertexPlanes)
		const TPlaneConcrete<FReal, 3>& GetPlane(int32 FaceIndex) const
		{
			return Planes[FaceIndex];
		}

		// Get the vertex at the specified index (e.g., indices from GetPlaneVertices)
		const FVec3& GetVertex(int32 VertexIndex) const
		{
			return Vertices[VertexIndex];
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
			const int32 NumVertices = Vertices.Num();

			if (ensure(NumVertices > 0))
			{
				for (int32 Idx = 0; Idx < NumVertices; ++Idx)
				{
					const FReal Dot = FVec3::DotProduct(Vertices[Idx], Direction);
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

			// Get 3 planes that contribute to this vertex
			TArrayView<const int32> PlaneIndices = GetVertexPlanes(VertexIndex);
			if (PlaneIndices.Num() >= 3)
			{
				// Move the planes by the margin and recalculate the interection
				// @todo(chaos): calculate dV/dm per vertex and store it in StructureData
				FVec3 PlanesPos;
				FPlane NewPlanes[3] =
				{
					FPlane(Planes[PlaneIndices[0]].X() - InMargin * Planes[PlaneIndices[0]].Normal(), Planes[PlaneIndices[0]].Normal()),
					FPlane(Planes[PlaneIndices[1]].X() - InMargin * Planes[PlaneIndices[1]].Normal(), Planes[PlaneIndices[1]].Normal()),
					FPlane(Planes[PlaneIndices[2]].X() - InMargin * Planes[PlaneIndices[2]].Normal(), Planes[PlaneIndices[2]].Normal()),
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
			// Get 3 planes that contribute to this vertex
			TArrayView<const int32> PlaneIndices = GetVertexPlanes(VertexIndex);
			if (PlaneIndices.Num() >= 3)
			{
				// Move the planes by the margin and recalculate the interection
				// @todo(chaos): calculate dV/dm per vertex and store it in StructureData (but see todo above)
				const FVec3 NewPlaneX = Scale * GetVertex(VertexIndex);
				const FVec3 NewPlaneNs[3] = 
				{
					(Scale * Planes[PlaneIndices[0]].Normal()).GetUnsafeNormal(),
					(Scale * Planes[PlaneIndices[1]].Normal()).GetUnsafeNormal(),
					(Scale * Planes[PlaneIndices[2]].Normal()).GetUnsafeNormal(),
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
				return Vertices[SupportVertexIndex];
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
					VertexPosition = Scale * Vertices[SupportVertexIndex];
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
					return Vertices[MaxVIdx] + Direction.GetUnsafeNormal() * Thickness;
				}
				return Vertices[MaxVIdx];
			}
			return FVec3(0);
		}

		virtual FString ToString() const
		{
			return FString::Printf(TEXT("Convex"));
		}

		const TArray<FVec3>& GetVertices() const
		{
			return Vertices;
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

			for (const FVec3& Vertex: Vertices)
			{
				Result = HashCombine(Result, ::GetTypeHash(Vertex[0]));
				Result = HashCombine(Result, ::GetTypeHash(Vertex[1]));
				Result = HashCombine(Result, ::GetTypeHash(Vertex[2]));
			}

			for(const TPlaneConcrete<FReal, 3>& Plane : Planes)
			{
				Result = HashCombine(Result, Plane.GetTypeHash());
			}

			return Result;
		}

		FORCEINLINE void SerializeImp(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
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

			if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::ConvexUsesVerticesArray)
			{
				TParticles<FReal, 3> TmpSurfaceParticles;
				Ar << TmpSurfaceParticles;

				const int32 NumVertices = (int32)TmpSurfaceParticles.Size();
				Vertices.SetNum(NumVertices);
				for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
				{
					Vertices[VertexIndex] = TmpSurfaceParticles.X(VertexIndex);
				}
			}
			else
			{
				Ar << Vertices;
			}
			
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
				TArray<FVec3> TempVertices;
				FConvexBuilder::Build(Vertices, Planes, FaceIndices, TempVertices, LocalBoundingBox);

				// Copy vertices and move into particles.
				TArray<FVec3> VerticesCopy = Vertices;
				const TParticles<FReal, 3> SurfaceParticles(MoveTemp(VerticesCopy));
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
				FConvexBuilder::BuildPlaneVertexIndices(Planes, Vertices, FaceIndices);
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
			return (Vertices.Num() > 0 && Planes.Num() > 0);
		}

		virtual bool IsPerformanceWarning() const override
		{
			return FConvexBuilder::IsPerformanceWarning(Planes.Num(), Vertices.Num());
		}

		virtual FString PerformanceWarningAndSimplifaction() override
		{

			FString PerformanceWarningString = FConvexBuilder::PerformanceWarningString(Planes.Num(), Vertices.Num());
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
			FConvexBuilder::Simplify(Planes, FaceIndices, Vertices, LocalBoundingBox);
			FConvexBuilder::MergeFaces(Planes, FaceIndices, Vertices);
			CreateStructureData(MoveTemp(FaceIndices));
		}

		FVec3 GetCenter() const
		{
			return FVec3(0);
		}

	private:
		TArray<TPlaneConcrete<FReal, 3>> Planes;
		TArray<FVec3> Vertices; //copy of the vertices that are just on the convex hull boundary
		TAABB<FReal, 3> LocalBoundingBox;
		FConvexStructureData StructureData;
		float Volume;
		FVec3 CenterOfMass;
	};
}
