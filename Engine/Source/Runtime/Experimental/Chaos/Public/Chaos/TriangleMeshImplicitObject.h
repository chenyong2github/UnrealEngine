// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/SegmentMesh.h"

#include "AABBTree.h"
#include "BoundingVolume.h"
#include "BoundingVolumeHierarchy.h"
#include "Box.h"
#include "ChaosArchive.h"
#include "ImplicitObject.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"
#include "UObject/PhysicsObjectVersion.h"


namespace Chaos
{
	extern CHAOS_API bool TriMeshPerPolySupport;

	class FCapsule;
	class FTriangle;

	class FConvex;
	struct FMTDInfo;

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

		const auto& GetLargeIndexBuffer() const
		{
			check(bRequiresLargeIndices);
			return LargeIdxBuffer;
		}

		const auto& GetSmallIndexBuffer() const
		{
			check(!bRequiresLargeIndices);
			return SmallIdxBuffer;
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

	class CHAOS_API FTriangleMeshImplicitObject final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		template <typename IdxType>
		FTriangleMeshImplicitObject(FParticles&& Particles, TArray<TVec3<IdxType>>&& Elements, TArray<uint16>&& InMaterialIndices, TUniquePtr<TArray<int32>>&& InExternalFaceIndexMap = nullptr, TUniquePtr<TArray<int32>>&& InExternalVertexIndexMap = nullptr, const bool bInCullsBackFaceRaycast = false)
		: FImplicitObject(EImplicitObject::HasBoundingBox | EImplicitObject::DisableCollisions, ImplicitObjectType::TriangleMesh)
		, MParticles(MoveTemp(Particles))
		, MElements(MoveTemp(Elements))
		, MLocalBoundingBox(MParticles.X(0), MParticles.X(0))
		, MaterialIndices(MoveTemp(InMaterialIndices))
		, ExternalFaceIndexMap(MoveTemp(InExternalFaceIndexMap))
		, ExternalVertexIndexMap(MoveTemp(InExternalVertexIndexMap))
		, bCullsBackFaceRaycast(bInCullsBackFaceRaycast)
		{
			for (uint32 Idx = 1; Idx < MParticles.Size(); ++Idx)
			{
				MLocalBoundingBox.GrowToInclude(MParticles.X(Idx));
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

		bool SweepGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const FCapsule& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const FConvex& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;

		bool SweepGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false, FVec3 TriMeshScale = FVec3(1.0f)) const;

		bool GJKContactPoint(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;
		bool GJKContactPoint(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;
		bool GJKContactPoint(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;
		bool GJKContactPoint(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const;

		bool GJKContactPoint(const TImplicitObjectScaled < TSphere<FReal, 3> >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool GJKContactPoint(const TImplicitObjectScaled < TBox<FReal, 3> >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool GJKContactPoint(const TImplicitObjectScaled < FCapsule >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;
		bool GJKContactPoint(const TImplicitObjectScaled < FConvex >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration, FVec3 TriMeshScale = FVec3(1.0f)) const;

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

		const FParticles& Particles() const;
		const FTrimeshIndexBuffer& Elements() const;

		void UpdateVertices(const TArray<FVector>& Positions);

		void VisitTriangles(const FAABB3& InQueryBounds, const TFunction<void(const FTriangle& Triangle)>& Visitor) const;

	private:
		void RebuildBV();

		FParticles MParticles;
		FTrimeshIndexBuffer MElements;
		FAABB3 MLocalBoundingBox;
		TArray<uint16> MaterialIndices;
		TUniquePtr<TArray<int32>> ExternalFaceIndexMap;
		TUniquePtr<TArray<int32>> ExternalVertexIndexMap;
		bool bCullsBackFaceRaycast;

		using BVHType = TAABBTree<int32, TAABBTreeLeafArray<int32, /*bComputeBounds=*/false>, /*bMutable=*/false>;

		template<typename InStorageType, typename InRealType>
		friend struct FBvEntry;

		template<bool bRequiresLargeIndex>
		struct FBvEntry
		{
			FTriangleMeshImplicitObject* TmData;
			int32 Index;

			bool HasBoundingBox() const { return true; }

			FAABB3 BoundingBox() const
			{
				auto LambdaHelper = [&](const auto& Elements)
				{
					TAABB<FReal,3> Bounds(TmData->MParticles.X(Elements[Index][0]), TmData->MParticles.X(Elements[Index][0]));

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

		template<typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD = nullptr, FVec3 TriMeshScale = FVec3(1.0f)) const;

		template<typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD, FVec3 TriMeshScale = FVec3(1.0f)) const;

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
}
