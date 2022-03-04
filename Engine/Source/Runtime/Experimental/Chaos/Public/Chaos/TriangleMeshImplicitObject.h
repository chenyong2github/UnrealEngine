// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/SegmentMesh.h"
#include "Chaos/Triangle.h"

#include "AABBTree.h"
#include "BoundingVolume.h"
#include "BoundingVolumeHierarchy.h"
#include "Box.h"
#include "ChaosArchive.h"
#include "ImplicitObject.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"
#include "UObject/PhysicsObjectVersion.h"

#include <type_traits>

namespace Chaos
{
	extern CHAOS_API bool TriMeshPerPolySupport;

	class FCapsule;
	class FTriangle;
	class FConvex;
	class FTriangle;
	struct FMTDInfo;
	class FContactPoint;

	class CHAOS_API FTrimeshIndexBuffer
	{
	public:
		using LargeIdxType = int32;
		using SmallIdxType = uint16;

		FTrimeshIndexBuffer() = default;
		FTrimeshIndexBuffer(TArray<TVec3<LargeIdxType>>&& Elements)
		    : LargeIdxBuffer(MoveTemp(Elements))
		    , bRequiresLargeIndices(true)
		{
		}

		FTrimeshIndexBuffer(TArray<TVec3<SmallIdxType>>&& Elements)
		    : SmallIdxBuffer(MoveTemp(Elements))
		    , bRequiresLargeIndices(false)
		{
		}

		FTrimeshIndexBuffer(const FTrimeshIndexBuffer& Other) = delete;
		FTrimeshIndexBuffer& operator=(const FTrimeshIndexBuffer& Other) = delete;

		void Serialize(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TrimeshCanUseSmallIndices)
			{
				Ar << LargeIdxBuffer;
				bRequiresLargeIndices = true;
			}
			else
			{
				Ar << bRequiresLargeIndices;
				if (bRequiresLargeIndices)
				{
					Ar << LargeIdxBuffer;
				}
				else
				{
					Ar << SmallIdxBuffer;
				}
			}
		}

		bool RequiresLargeIndices() const
		{
			return bRequiresLargeIndices;
		}

		const TArray<TVec3<LargeIdxType>>& GetLargeIndexBuffer() const
		{
			check(bRequiresLargeIndices);
			return LargeIdxBuffer;
		}

		const TArray<TVec3<SmallIdxType>>& GetSmallIndexBuffer() const
		{
			check(!bRequiresLargeIndices);
			return SmallIdxBuffer;
		}

		int32 GetNumTriangles() const
		{
			if(bRequiresLargeIndices)
			{
				return LargeIdxBuffer.Num();
			}

			return SmallIdxBuffer.Num();
		}

		template<typename ExpectedType>
		const TArray<TVec3<ExpectedType>>& GetIndexBuffer() const
		{
			if constexpr(std::is_same_v<ExpectedType, LargeIdxType>)
			{
				check(bRequiresLargeIndices);
				return LargeIdxBuffer;
			}
			else if constexpr(std::is_same_v<ExpectedType, SmallIdxType>)
			{
				check(!bRequiresLargeIndices);
				return SmallIdxBuffer;
			}
			else
			{
				static_assert(sizeof(ExpectedType) == 0, "Unsupported index buffer type");
			}
		}

	private:
		TArray<TVec3<LargeIdxType>> LargeIdxBuffer;
		TArray<TVec3<SmallIdxType>> SmallIdxBuffer;
		bool bRequiresLargeIndices;
	};

	FORCEINLINE FArchive& operator<<(FArchive& Ar, FTrimeshIndexBuffer& Buffer)
	{
		Buffer.Serialize(Ar);
		return Ar;
	}

	template <typename IdxType, typename ParticlesType>
	inline void TriangleMeshTransformVertsHelper(const FVec3& TriMeshScale, int32 TriIdx, const ParticlesType& Particles,
		const TArray<TVector<IdxType, 3>>& Elements, FVec3& OutA, FVec3& OutB, FVec3& OutC)
	{
		OutA = Particles.X(Elements[TriIdx][0]) * TriMeshScale;
		OutB = Particles.X(Elements[TriIdx][1]) * TriMeshScale;
		OutC = Particles.X(Elements[TriIdx][2]) * TriMeshScale;
	}

	template <typename IdxType, typename ParticlesType>
	inline void TriangleMeshTransformVertsHelper(const FRigidTransform3& Transform, int32 TriIdx, const ParticlesType& Particles,
		const TArray<TVector<IdxType, 3>>& Elements, FVec3& OutA, FVec3& OutB, FVec3& OutC)
	{
		// Note: deliberately using scaled transform
		OutA = Transform.TransformPosition(FVector(Particles.X(Elements[TriIdx][0])));
		OutB = Transform.TransformPosition(FVector(Particles.X(Elements[TriIdx][1])));
		OutC = Transform.TransformPosition(FVector(Particles.X(Elements[TriIdx][2])));
	}


	class CHAOS_API FTriangleMeshImplicitObject final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		using ParticlesType = TParticles<FRealSingle, 3>;
		using ParticleVecType = TVec3<FRealSingle>;

		template <typename IdxType>
		FTriangleMeshImplicitObject(ParticlesType&& Particles, TArray<TVec3<IdxType>>&& Elements, TArray<uint16>&& InMaterialIndices, TUniquePtr<TArray<int32>>&& InExternalFaceIndexMap = nullptr, TUniquePtr<TArray<int32>>&& InExternalVertexIndexMap = nullptr, const bool bInCullsBackFaceRaycast = false)
		: FImplicitObject(EImplicitObject::HasBoundingBox | EImplicitObject::DisableCollisions, ImplicitObjectType::TriangleMesh)
		, MParticles(MoveTemp(Particles))
		, MElements(MoveTemp(Elements))
		, MLocalBoundingBox(MParticles.X(0), MParticles.X(0))
		, MaterialIndices(MoveTemp(InMaterialIndices))
		, ExternalFaceIndexMap(MoveTemp(InExternalFaceIndexMap))
		, ExternalVertexIndexMap(MoveTemp(InExternalVertexIndexMap))
		, bCullsBackFaceRaycast(bInCullsBackFaceRaycast)
		{
			const int32 NumTriangles = MElements.GetNumTriangles();
			if(NumTriangles > 0)
			{

				const TArray<TVec3<IdxType>>& Tris = MElements.GetIndexBuffer<IdxType>();
				const TVec3<IdxType>& FirstTri = Tris[0];

				MLocalBoundingBox = FAABB3(MParticles.X(FirstTri[0]), MParticles.X(FirstTri[0]));
				MLocalBoundingBox.GrowToInclude(MParticles.X(FirstTri[1]));
				MLocalBoundingBox.GrowToInclude(MParticles.X(FirstTri[2]));

				for(int32 TriangleIndex = 1; TriangleIndex < NumTriangles; ++TriangleIndex)
				{
					const TVec3<IdxType>& Tri = Tris[TriangleIndex];
					MLocalBoundingBox.GrowToInclude(MParticles.X(Tri[0]));
					MLocalBoundingBox.GrowToInclude(MParticles.X(Tri[1]));
					MLocalBoundingBox.GrowToInclude(MParticles.X(Tri[2]));
				}
			}
			
			RebuildBV();
		}

		FTriangleMeshImplicitObject(const FTriangleMeshImplicitObject& Other) = delete;
		FTriangleMeshImplicitObject(FTriangleMeshImplicitObject&& Other) = delete;
		virtual ~FTriangleMeshImplicitObject();

		FReal GetRadius() const
		{
			return 0.0f;
		}

		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const;

		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const FVec3& Point, const FReal Thickness) const override;

		bool OverlapGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;
		bool OverlapGeom(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr) const;

		bool OverlapGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool OverlapGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool OverlapGeom(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr, FVec3 TriMeshScale = FVec3(1.0f)) const;

		bool SweepGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const FCapsule& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const FConvex& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;

		bool SweepGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;

		bool GJKContactPoint(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;
		bool GJKContactPoint(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;
		bool GJKContactPoint(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;
		bool GJKContactPoint(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;

		bool GJKContactPoint(const TImplicitObjectScaled < TSphere<FReal, 3> >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool GJKContactPoint(const TImplicitObjectScaled < TBox<FReal, 3> >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool GJKContactPoint(const TImplicitObjectScaled < FCapsule >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool GJKContactPoint(const TImplicitObjectScaled < FConvex >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;


		bool ContactManifold(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, TArray<FContactPoint>& ContactPoints) const;
		bool ContactManifold(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, TArray<FContactPoint>& ContactPoints) const;
		bool ContactManifold(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, TArray<FContactPoint>& ContactPoints) const;
		bool ContactManifold(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, TArray<FContactPoint>& ContactPoints, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool ContactManifold(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, TArray<FContactPoint>& ContactPoints, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool ContactManifold(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, TArray<FContactPoint>& ContactPoints, FVec3 TriMeshScale = FVec3(1.0f)) const;

		// Returns -1 if InternalFaceIndex is not in map, or map is invalid.
		int32 GetExternalFaceIndexFromInternal(int32 InternalFaceIndex) const;

		// Does Trimesh cull backfaces in raycast.
		bool GetCullsBackFaceRaycast() const;
		void SetCullsBackFaceRaycast(const bool bInCullsBackFace);


		virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDistance) const override;
		virtual FVec3 FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const override;

		virtual const FAABB3 BoundingBox() const
		{
			return MLocalBoundingBox;
		}

		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::TriangleMesh;
		}

		TUniquePtr<FTriangleMeshImplicitObject> CopySlow() const;

		void SerializeImp(FChaosArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

			FImplicitObject::SerializeImp(Ar);
			Ar << MParticles;
			Ar << MElements;
			TBox<FReal, 3>::SerializeAsAABB(Ar, MLocalBoundingBox);

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::RemovedConvexHullsFromTriangleMeshImplicitObject)
			{
				TUniquePtr<TGeometryParticles<FReal, 3>> ConvexHulls;
				Ar << ConvexHulls;
			}

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TrimeshSerializesBV)
			{
				// Should now only hit when loading older trimeshes
				RebuildBV();
			}
			else if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TrimeshSerializesAABBTree)
			{
				TBoundingVolume<int32> Dummy;
				Ar << Dummy;
				RebuildBV();
			}
			else
			{
				// Serialize acceleration
				Ar << BVH;
			}

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddTrimeshMaterialIndices)
			{
				Ar << MaterialIndices;
			}

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::TriangleMeshHasFaceIndexMap)
			{
				// TODO: This data is only needed in editor unless project configuration requests this for gameplay. We should not serialize this when cooking
				// unless it is required for gameplay, as we are wasting disk space.
				if (Ar.IsLoading())
				{
					ExternalFaceIndexMap = MakeUnique<TArray<int32>>(TArray<int32>());
					Ar << *ExternalFaceIndexMap;
				}
				else
				{
					if (ExternalFaceIndexMap == nullptr)
					{
						TArray<int32> EmptyArray;
						Ar << EmptyArray;
					}
					else
					{
						Ar << *ExternalFaceIndexMap;
					}
				}
			}

			Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
			if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::TriangleMeshHasVertexIndexMap)
			{
				if (Ar.IsLoading())
				{
					ExternalVertexIndexMap = MakeUnique<TArray<int32>>(TArray<int32>());
					Ar << *ExternalVertexIndexMap;
				}
				else
				{
					if (ExternalVertexIndexMap == nullptr)
					{
						TArray<int32> EmptyArray;
						Ar << EmptyArray;
					}
					else
					{
						Ar << *ExternalVertexIndexMap;
					}
				}
			}
		}

		virtual void Serialize(FChaosArchive& Ar) override;

		virtual FString ToString() const
		{
			return FString::Printf(TEXT("TriangleMesh"));
		}

		virtual uint32 GetTypeHash() const override;

		FVec3 GetFaceNormal(const int32 FaceIdx) const;

		virtual uint16 GetMaterialIndex(uint32 HintIndex) const override;

		const ParticlesType& Particles() const;
		const FTrimeshIndexBuffer& Elements() const;

		void UpdateVertices(const TArray<FVector>& Positions);

		void VisitTriangles(const FAABB3& InQueryBounds, const TFunction<void(const FTriangle& Triangle)>& Visitor) const;

		void VisitTriangle(const int32 TriangleIndex, const TFunction<void(const FTriangle& Triangle)>& Visitor) const;

		/**
		 * @brief Generate the triangle at the specified index with the specified transform (including scale)
		*/
		void GetTransformedTriangle(const int32 TriangleIndex, const FRigidTransform3& Transform, FTriangle& OutTriangle) const
		{
			if (MElements.RequiresLargeIndices())
			{
				TriangleMeshTransformVertsHelper(Transform, TriangleIndex, MParticles, MElements.GetLargeIndexBuffer(), OutTriangle[0], OutTriangle[1], OutTriangle[2]);
			}
			else
			{
				TriangleMeshTransformVertsHelper(Transform, TriangleIndex, MParticles, MElements.GetSmallIndexBuffer(), OutTriangle[0], OutTriangle[1], OutTriangle[2]);
			}
		}

		/**
		 * @brief Get a list of triangle indices that overlap the query bounds
		 * @param QueryBounds query bounds in trimesh space
		*/
		void FindOverlappingTriangles(const FAABB3& QueryBounds, TArray<int32>& OutTriangleIndices) const
		{
			OutTriangleIndices = BVH.FindAllIntersections(QueryBounds);
		}

	private:

		void RebuildBV();

		ParticlesType MParticles;
		FTrimeshIndexBuffer MElements;
		FAABB3 MLocalBoundingBox;
		TArray<uint16> MaterialIndices;
		TUniquePtr<TArray<int32>> ExternalFaceIndexMap;
		TUniquePtr<TArray<int32>> ExternalVertexIndexMap;
		bool bCullsBackFaceRaycast;

		using BVHType = TAABBTree<int32, TAABBTreeLeafArray<int32, /*bComputeBounds=*/false, FRealSingle>, /*bMutable=*/false, FRealSingle>;

		// Initialising constructor privately declared for use in CopySlow to copy the underlying BVH
		template <typename IdxType>
		FTriangleMeshImplicitObject(ParticlesType&& Particles, TArray<TVec3<IdxType>>&& Elements, TArray<uint16>&& InMaterialIndices, const BVHType& InBvhToCopy, TUniquePtr<TArray<int32>>&& InExternalFaceIndexMap = nullptr, TUniquePtr<TArray<int32>>&& InExternalVertexIndexMap = nullptr, const bool bInCullsBackFaceRaycast = false)
			: FImplicitObject(EImplicitObject::HasBoundingBox | EImplicitObject::DisableCollisions, ImplicitObjectType::TriangleMesh)
			, MParticles(MoveTemp(Particles))
			, MElements(MoveTemp(Elements))
			, MLocalBoundingBox(MParticles.X(0), MParticles.X(0))
			, MaterialIndices(MoveTemp(InMaterialIndices))
			, ExternalFaceIndexMap(MoveTemp(InExternalFaceIndexMap))
			, ExternalVertexIndexMap(MoveTemp(InExternalVertexIndexMap))
			, bCullsBackFaceRaycast(bInCullsBackFaceRaycast)
		{
			const int32 NumTriangles = MElements.GetNumTriangles();
			if(NumTriangles > 0)
			{
				const TArray<TVec3<IdxType>>& Tris = MElements.GetIndexBuffer<IdxType>();
				const TVec3<IdxType>& FirstTri = Tris[0];

				MLocalBoundingBox = FAABB3(MParticles.X(FirstTri[0]), MParticles.X(FirstTri[0]));
				MLocalBoundingBox.GrowToInclude(MParticles.X(FirstTri[1]));
				MLocalBoundingBox.GrowToInclude(MParticles.X(FirstTri[2]));

				for(int32 TriangleIndex = 1; TriangleIndex < NumTriangles; ++TriangleIndex)
				{
					const TVec3<IdxType>& Tri = Tris[TriangleIndex];
					MLocalBoundingBox.GrowToInclude(MParticles.X(Tri[0]));
					MLocalBoundingBox.GrowToInclude(MParticles.X(Tri[1]));
					MLocalBoundingBox.GrowToInclude(MParticles.X(Tri[2]));
				}
			}
			
			BVH.CopyFrom(InBvhToCopy);
		}

		template<typename InStorageType, typename InRealType>
		friend struct FBvEntry;

		template<bool bRequiresLargeIndex>
		struct FBvEntry
		{
			FTriangleMeshImplicitObject* TmData;
			int32 Index;

			bool HasBoundingBox() const { return true; }

			TAABB<FRealSingle, 3> BoundingBox() const
			{
				auto LambdaHelper = [&](const auto& Elements)
				{
					TAABB<FRealSingle,3> Bounds(TmData->MParticles.X(Elements[Index][0]), TmData->MParticles.X(Elements[Index][0]));

					Bounds.GrowToInclude(TmData->MParticles.X(Elements[Index][1]));
					Bounds.GrowToInclude(TmData->MParticles.X(Elements[Index][2]));

					return Bounds;
				};

				if(bRequiresLargeIndex)
				{
					return LambdaHelper(TmData->MElements.GetLargeIndexBuffer());
				}
				else
				{
					return LambdaHelper(TmData->MElements.GetSmallIndexBuffer());
				}
			}

			template<typename TPayloadType>
			int32 GetPayload(int32 Idx) const
			{
				return Idx;
			}

			FUniqueIdx UniqueIdx() const
			{
				return FUniqueIdx(Index);
			}
		};

		BVHType BVH;

		template<typename Geom, typename IdxType>
		friend struct FTriangleMeshSweepVisitor;

		// Required by implicit object serialization, disabled for general use.
		friend class FImplicitObject;

		FTriangleMeshImplicitObject()
		    : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh){};

		template <typename QueryGeomType>
		bool GJKContactPointImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0)) const;

		template <typename GeomType>
		bool ContactManifoldImp(const GeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, TArray<FContactPoint>& ContactPoints, FVec3 TriMeshScale) const;

		template<typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr, FVec3 TriMeshScale = FVec3(1.0f)) const;

		template<typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, FVec3& OutFaceNormal, const FReal Thickness, const bool bComputeMTD, FVec3 TriMeshScale = FVec3(1.0f)) const;

		template <typename IdxType>
		bool RaycastImp(const TArray<TVec3<IdxType>>& Elements, const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const;

		template <typename IdxType>
		bool OverlapImp(const TArray<TVec3<IdxType>>& Elements, const FVec3& Point, const FReal Thickness) const;

		template<typename IdxType>
		int32 FindMostOpposingFace(const TArray<TVec3<IdxType>>& Elements, const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const;

		template <typename IdxType>
		void RebuildBVImp(const TArray<TVec3<IdxType>>& Elements);

		template <typename IdxType>
		TUniquePtr<FTriangleMeshImplicitObject> CopySlowImpl(const TArray < TVector<IdxType, 3>>& InElements) const;
	};


	/**
	 * @brief A helper for iterating over FTriangleMeshImplicitObject triangles in a bounding box
	*/
	class FTriangleMeshTriangleProducer
	{
	public:
		FTriangleMeshTriangleProducer()
			: NextIndex(0)
			, TriangleIndices()
		{
		}

		/**
		 * @brief Find the set of triangle indices that overlap the query bounds
		 * @param InQueryBounds The query bounds in triangle mesh space
		*/
		inline void Reset(const FTriangleMeshImplicitObject& InTriMesh, const FAABB3& InQueryBounds)
		{
			InTriMesh.FindOverlappingTriangles(InQueryBounds, TriangleIndices);
			NextIndex = 0;
		}

		/**
		 * @brief Whether we are done producing triangles
		 * @return
		*/
		inline const bool IsDone() const
		{
			return (NextIndex >= TriangleIndices.Num());
		}

		/**
		 * @brief Get the next triangle with its index and transform the vertices
		 * @return true if we produced a triangle, false if we are done with the triangle list
		*/
		inline bool NextTriangle(const FTriangleMeshImplicitObject& InTriMesh, const FRigidTransform3& Transform, FTriangle& OutTriangle, int32& OutTriangleIndex)
		{
			if (IsDone())
			{
				return false;
			}

			const int32 TriIndex = TriangleIndices[NextIndex++];

			InTriMesh.GetTransformedTriangle(TriIndex, Transform, OutTriangle);
			OutTriangleIndex = TriIndex;

			return true;
		}

	private:
		int32 NextIndex;
		TArray<int32> TriangleIndices;
	};

}
