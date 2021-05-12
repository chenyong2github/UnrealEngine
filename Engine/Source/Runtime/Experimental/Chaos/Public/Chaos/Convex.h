// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/AABB.h"
#include "Chaos/ConvexStructureData.h"
#include "Chaos/MassProperties.h"

#include "CollisionConvexMesh.h"
#include "ChaosArchive.h"
#include "ChaosCheck.h"
#include "ChaosLog.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/PhysicsObjectVersion.h"
//#include "UObject/DownstreamObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

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
		using TType = FReal;
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
		UE_DEPRECATED(4.27, "Use the constructor version with the face indices.")
		FConvex(TArray<TPlaneConcrete<FReal, 3>>&& InPlanes, TArray<FVec3>&& InVertices)
		    : FImplicitObject(EImplicitObject::IsConvex | EImplicitObject::HasBoundingBox, ImplicitObjectType::Convex)
			, Planes(MoveTemp(InPlanes))
		    , Vertices(MoveTemp(InVertices))
		    , LocalBoundingBox(FAABB3::EmptyAABB())
		{
			for (int32 ParticleIndex = 0; ParticleIndex < Vertices.Num(); ++ParticleIndex)
			{
				LocalBoundingBox.GrowToInclude(Vertices[ParticleIndex]);
			}

			// For now we approximate COM and volume with the bounding box
			CenterOfMass = LocalBoundingBox.GetCenterOfMass();
			Volume = LocalBoundingBox.GetVolume();
		}

		FConvex(TArray<TPlaneConcrete<FReal, 3>>&& InPlanes, TArray<TArray<int32>>&& InFaceIndices, TArray<FVec3>&& InVertices)
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

			CreateStructureData(MoveTemp(InFaceIndices));
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

			// @todo(chaos): this only works with triangles. Fix that an we can run MergeFaces before calling this
			TParticles<FReal, 3> VertexParticles(CopyTemp(Vertices));
			CalculateVolumeAndCenterOfMass(VertexParticles, FaceIndices, Volume, CenterOfMass);

			// @todo(chaos): DistanceTolerance should be based on size, or passed in
			const FReal DistanceTolerance = 1.0f;
			FConvexBuilder::MergeFaces(Planes, FaceIndices, Vertices, DistanceTolerance);
			CHAOS_ENSURE(Planes.Num() == FaceIndices.Num());

			CreateStructureData(MoveTemp(FaceIndices));

			SetMargin(InMargin);
		}

		FConvex& operator=(const FConvex& Other) = delete;
		
		FConvex& operator=(FConvex&& Other)
		{
			// Base class assignment
			// @todo(chaos): Base class needs protected assignment
			Type = Other.Type;
			CollisionType = Other.CollisionType;
			Margin = Other.Margin;
			bIsConvex = Other.bIsConvex;
			bDoCollide = Other.bDoCollide;
			bHasBoundingBox = Other.bHasBoundingBox;
#if TRACK_CHAOS_GEOMETRY
			bIsTracked = Other.bIsTracked;
#endif
			// This class assignment
			Planes = MoveTemp(Other.Planes);
			Vertices = MoveTemp(Other.Vertices);
			LocalBoundingBox = MoveTemp(Other.LocalBoundingBox);
			StructureData = MoveTemp(Other.StructureData);
			Volume = MoveTemp(Other.Volume);
			CenterOfMass = MoveTemp(Other.CenterOfMass);

			return *this;
		}

		void MovePlanesAndRebuild(FReal InDelta);

	private:
		void CreateStructureData(TArray<TArray<int32>>&& FaceIndices);

	public:
		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::Convex;
		}

		FReal GetMargin() const
		{
			return Margin;
		}

		FReal GetRadius() const
		{
			return 0.0f;
		}

		virtual const FAABB3 BoundingBox() const override
		{
			return LocalBoundingBox;
		}

		// Return the distance to the surface
		virtual FReal PhiWithNormal(const FVec3& X, FVec3& Normal) const override
		{
			return PhiWithNormalInternal(X, Normal);
		}

		virtual FReal PhiWithNormalScaled(const FVec3& X, const FVec3& Scale, FVec3& Normal) const override
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
		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override;

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

		// Get the index of the plane that most opposes the normal
		int32 GetMostOpposingPlaneScaled(const FVec3& Normal, const FVec3& Scale) const;

		// Get the nearest point on an edge of the specified face
		FVec3 GetClosestEdgePosition(int32 PlaneIndex, const FVec3& Position) const;

		bool GetClosestEdgeVertices(int32 PlaneIndex, const FVec3& Position, int32& OutVertexIndex0, int32& OutVertexIndex1) const;

		// Get an array of all the plane indices that belong to a vertex (up to MaxVertexPlanes).
		// Returns the number of planes found.
		int32 FindVertexPlanes(int32 VertexIndex, int32* OutVertexPlanes, int32 MaxVertexPlanes) const;

		// The number of vertices that make up the corners of the specified face
		int32 NumPlaneVertices(int32 PlaneIndex) const;

		// Get the vertex index of one of the vertices making up the corners of the specified face
		int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const;

		int32 GetEdgeVertex(int32 EdgeIndex, int32 EdgeVertexIndex) const;

		int32 GetEdgePlane(int32 EdgeIndex, int32 EdgePlaneIndex) const;

		int32 NumPlanes() const
		{
			return Planes.Num();
		}

		int32 NumEdges() const;

		int32 NumVertices() const
		{
			return (int32)Vertices.Num();
		}

		// Get the plane at the specified index (e.g., indices from FindVertexPlanes)
		const TPlaneConcrete<FReal, 3>& GetPlane(int32 FaceIndex) const
		{
			return Planes[FaceIndex];
		}

		// Get the vertex at the specified index (e.g., indices from GetPlaneVertexs)
		const FVec3& GetVertex(int32 VertexIndex) const
		{
			return Vertices[VertexIndex];
		}


		virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const override;

		virtual int32 FindMostOpposingFaceScaled(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist, const FVec3& Scale) const override;

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
		FReal GetWindingOrder() const
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

	public:

		FVec3 GetMarginAdjustedVertex(int32 VertexIndex, FReal InMargin) const
		{
			// @chaos(todo): moving the vertices this way based on margin is only valid for small margins. If the margin
			// is large enough to cause a face to reduce to zero size, vertices should be merged and the path is non-linear.
			// This can be fixed with some extra data in the convex structure, but for now we accept the fact that large 
			// margins on convexes with small faces can cause non-convex core shapes.

			if (InMargin == 0.0f)
			{
				return GetVertex(VertexIndex);
			}

			// Get any 3 planes that contribute to this vertex
			int32 PlaneIndices[3];
			int32 NumVertexPlanes = FindVertexPlanes(VertexIndex, PlaneIndices, 3);

			// Move the planes by the margin and recalculate the interection
			// @todo(chaos): calculate dV/dm per vertex and store it in StructureData
			if (NumVertexPlanes >= 3)
			{
				const int32 PlaneIndex0 = PlaneIndices[0];
				const int32 PlaneIndex1 = PlaneIndices[1];
				const int32 PlaneIndex2 = PlaneIndices[2];

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

			// If we get here, the convex hull is malformed. Try to handle it anyway 
			// @todo(chaos): track down the invalid hull issue

			if (NumVertexPlanes == 2)
			{
				const int32 PlaneIndex0 = PlaneIndices[0];
				const int32 PlaneIndex1 = PlaneIndices[1];
				const FVec3 NewPlaneX = GetVertex(VertexIndex);
				const FVec3 NewPlaneN0 = Planes[PlaneIndex0].Normal();
				const FVec3 NewPlaneN1 = Planes[PlaneIndex1].Normal();
				const FVec3 NewPlaneN = (NewPlaneN0 + NewPlaneN1).GetSafeNormal();
				return NewPlaneX - (InMargin * NewPlaneN);
			}

			if (NumVertexPlanes == 1)
			{
				const int32 PlaneIndex0 = PlaneIndices[0];
				const FVec3 NewPlaneX = GetVertex(VertexIndex);
				const FVec3 NewPlaneN = Planes[PlaneIndex0].Normal();
				return NewPlaneX - (InMargin * NewPlaneN);
			}

			// Ok now we really are done...just return the outer vertex and duck
			return GetVertex(VertexIndex);
		}

		FVec3 GetMarginAdjustedVertexScaled(int32 VertexIndex, FReal InMargin, const FVec3& Scale) const
		{
			if (InMargin == 0.0f)
			{
				return GetVertex(VertexIndex) * Scale;
			}

			// Get any 3 planes that contribute to this vertex
			int32 PlaneIndices[3];
			int32 NumVertexPlanes = FindVertexPlanes(VertexIndex, PlaneIndices, 3);

			// Move the planes by the margin and recalculate the interection
			// @todo(chaos): calculate dV/dm per vertex and store it in StructureData (but see todo above)
			if (NumVertexPlanes >= 3)
			{
				const int32 PlaneIndex0 = PlaneIndices[0];
				const int32 PlaneIndex1 = PlaneIndices[1];
				const int32 PlaneIndex2 = PlaneIndices[2];

				const FVec3 NewPlaneX = Scale * GetVertex(VertexIndex);
				const FVec3 NewPlaneNs[3] = 
				{
					(Planes[PlaneIndex0].Normal() / Scale).GetUnsafeNormal(),
					(Planes[PlaneIndex1].Normal() / Scale).GetUnsafeNormal(),
					(Planes[PlaneIndex2].Normal() / Scale).GetUnsafeNormal(),
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

			// If we get here, the convex hull is malformed. Try to handle it anyway 
			// @todo(chaos): track down the invalid hull issue

			if (NumVertexPlanes == 2)
			{
				const int32 PlaneIndex0 = PlaneIndices[0];
				const int32 PlaneIndex1 = PlaneIndices[1];
				const FVec3 NewPlaneX = Scale * GetVertex(VertexIndex);
				const FVec3 NewPlaneN0 = (Planes[PlaneIndex0].Normal() / Scale).GetUnsafeNormal();
				const FVec3 NewPlaneN1 = (Planes[PlaneIndex1].Normal() / Scale).GetUnsafeNormal();
				const FVec3 NewPlaneN = (NewPlaneN0 + NewPlaneN1).GetSafeNormal();
				return NewPlaneX - (InMargin * NewPlaneN);
			}

			if (NumVertexPlanes == 1)
			{
				const int32 PlaneIndex0 = PlaneIndices[0];
				const FVec3 NewPlaneX = Scale * GetVertex(VertexIndex);
				const FVec3 NewPlaneN = (Planes[PlaneIndex0].Normal() / Scale).GetUnsafeNormal();
				return NewPlaneX - (InMargin * NewPlaneN);
			}

			// Ok now we really are done...just return the outer vertex and duck
			return GetVertex(VertexIndex) * Scale;
		}

	public:
		// Return support point on the core shape (the convex shape with all planes moved inwards by margin).
		FVec3 SupportCore(const FVec3& Direction, FReal InMargin) const
		{
			const int32 SupportVertexIndex = GetSupportVertex(Direction);
			if (SupportVertexIndex != INDEX_NONE)
			{
				if (InMargin > SMALL_NUMBER)
				{
					return GetMarginAdjustedVertex(SupportVertexIndex, InMargin);
				}
				return Vertices[SupportVertexIndex];
			}
			return FVec3(0);
		}

		// SupportCore with non-uniform scale support. This is required for the margin in scaled
		// space to by uniform. Note in this version all the inputs are in outer container's (scaled shape) space
		FVec3 SupportCoreScaled(const FVec3& Direction, FReal InMargin, const FVec3& Scale) const
		{
			// Find the supporting vertex index
			const FVec3 DirectionScaled = Scale * Direction;	// does not need to be normalized
			const int32 SupportVertexIndex = GetSupportVertex(DirectionScaled);

			// Adjust the vertex position based on margin
			FVec3 VertexPosition = FVec3(0);
			if (SupportVertexIndex != INDEX_NONE)
			{
				// Note: Shapes wrapped in a non-uniform scale should not have their own margin and we assume that here
				// @chaos(todo): apply an upper limit to the margin to prevent a non-convex or null shape (also see comments in GetMarginAdjustedVertex)
				if (InMargin > SMALL_NUMBER)
				{
					VertexPosition = GetMarginAdjustedVertexScaled(SupportVertexIndex, InMargin, Scale);
				}
				else
				{
					VertexPosition = Scale * Vertices[SupportVertexIndex];
				}
			}
			return VertexPosition;
		}

		// Return support point on the shape
		// @todo(chaos): do we need to support thickness?
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

		FORCEINLINE FVec3 SupportScaled(const FVec3& Direction, const FReal Thickness, const FVec3& Scale) const
		{
			FVec3 SupportPoint = Support(Direction * Scale, 0.0f) * Scale;
			if (Thickness > 0.0f)
			{
				SupportPoint += Thickness * Direction.GetSafeNormal();
			}
			return SupportPoint;
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

		FRotation3 GetRotationOfMass() const
		{
			return FRotation3::FromIdentity();
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
			//Ar.UsingCustomVersion(Downstream::GUID);
			Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
			Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
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

			// Do we use the old Particles array or the new Vertices array?
			// Note: This change was back-ported to UE4, so we need to check 
			// multiple object versions.
			//
			// @todo(chaos): when we hit a merge conflict here, replace bConvexVerticesNewFormatDownstream with the 
			// downstream version and remove this todo. Also uncomment the UsingCustomVersion at the start of this function
			// and the include at the top of the file.
			//
			// This is a mess because the change was back-integrated to 2 different streams. Be careful...
			bool bConvexVerticesNewFormatUE4 = (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::ConvexUsesVerticesArray);
			bool bConvexVerticesNewFormatDownstream = false;
			bool bConvexVerticesNewFormatFN = (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::ChaosConvexVariableStructureDataAndVerticesArray);
			bool bConvexVerticesNewFormat = bConvexVerticesNewFormatUE4 || bConvexVerticesNewFormatDownstream || bConvexVerticesNewFormatFN;

			if (!bConvexVerticesNewFormat)
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
				// @todo(chaos): Make it so it can take Vertices as both input and output without breaking...
				TArray<TArray<int32>> FaceIndices;
				TArray<FVec3> TempVertices;
				FConvexBuilder::Build(Vertices, Planes, FaceIndices, TempVertices, LocalBoundingBox);

				// Copy vertices and move into particles.
				// @todo(chaos): make CalculateVolumeAndCenterOfMass take array of positions rather than particles
				TArray<FVec3> VerticesCopy = Vertices;
				const FParticles SurfaceParticles(MoveTemp(VerticesCopy));
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

			// @todo(chaos): DistanceTolerance should be based on size, or passed in
			const FReal DistanceTolerance = 1.0f;
			FConvexBuilder::MergeFaces(Planes, FaceIndices, Vertices, DistanceTolerance);

			CreateStructureData(MoveTemp(FaceIndices));
		}

		FVec3 GetCenter() const
		{
			return FVec3(0);
		}

	private:
		TArray<TPlaneConcrete<FReal, 3>> Planes;
		TArray<FVec3> Vertices; //copy of the vertices that are just on the convex hull boundary
		FAABB3 LocalBoundingBox;
		FConvexStructureData StructureData;
		FReal Volume;
		FVec3 CenterOfMass;
	};
}
