// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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

namespace Chaos
{
	template<typename T>
	class TCapsule;

	class FConvex;

	class CHAOS_API FTrimeshIndexBuffer
	{
	public:
		using LargeIdxType = int32;
		using SmallIdxType = uint16;

		FTrimeshIndexBuffer() = default;
		FTrimeshIndexBuffer(TArray<TVector<LargeIdxType, 3>>&& Elements)
		    : LargeIdxBuffer(MoveTemp(Elements))
		    , bRequiresLargeIndices(true)
		{
		}

		FTrimeshIndexBuffer(TArray<TVector<SmallIdxType, 3>>&& Elements)
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
		TArray<TVector<LargeIdxType, 3>> LargeIdxBuffer;
		TArray<TVector<SmallIdxType, 3>> SmallIdxBuffer;
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
		FTriangleMeshImplicitObject(TParticles<FReal, 3>&& Particles, TArray<TVector<IdxType, 3>>&& Elements, TArray<uint16>&& InMaterialIndices)
		: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh)
		, MParticles(MoveTemp(Particles))
		, MElements(MoveTemp(Elements))
		, MLocalBoundingBox(MParticles.X(0), MParticles.X(0))
		, MaterialIndices(MoveTemp(InMaterialIndices))
		{
			for (uint32 Idx = 1; Idx < MParticles.Size(); ++Idx)
			{
				MLocalBoundingBox.GrowToInclude(MParticles.X(Idx));
			}
			RebuildBV();
		}

		FTriangleMeshImplicitObject(const FTriangleMeshImplicitObject& Other) = delete;
		FTriangleMeshImplicitObject(FTriangleMeshImplicitObject&& Other) = delete;
		virtual ~FTriangleMeshImplicitObject() {}

		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const;

		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const FVec3& Point, const FReal Thickness) const override;

		bool OverlapGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TImplicitObjectScaled<FConvex>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;

		bool SweepGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const FConvex& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;

		virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDistance) const override;
		virtual FVec3 FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const override;

		virtual const TAABB<FReal, 3>& BoundingBox() const
		{
			return MLocalBoundingBox;
		}

		static constexpr EImplicitObjectType StaticType()
		{
			return ImplicitObjectType::TriangleMesh;
		}

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
				TBoundingVolume<int32, FReal, 3> Dummy;
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
		}

		virtual void Serialize(FChaosArchive& Ar) override;

		virtual uint32 GetTypeHash() const override;

		FVec3 GetFaceNormal(const int32 FaceIdx) const;

		virtual uint16 GetMaterialIndex(uint32 HintIndex) const override;

		const TParticles<FReal, 3>& Particles() const;
		const FTrimeshIndexBuffer& Elements() const;

	private:
		void RebuildBV();

		TParticles<FReal, 3> MParticles;
		FTrimeshIndexBuffer MElements;
		TAABB<FReal, 3> MLocalBoundingBox;
		TArray<uint16> MaterialIndices;

		//using BVHType = TBoundingVolume<int32, T, 3>;
		using BVHType = TAABBTree<int32, TAABBTreeLeafArray<int32, FReal>, FReal, /*bMutable=*/false>;

		template<typename InStorageType, typename InRealType>
		friend struct FBvEntry;

		template<bool bRequiresLargeIndex>
		struct FBvEntry
		{
			FTriangleMeshImplicitObject* TmData;
			int32 Index;

			bool HasBoundingBox() const { return true; }

			TAABB<FReal, 3> BoundingBox() const
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
		};

		BVHType BVH;

		template<typename Geom, typename IdxType>
		friend struct FTriangleMeshSweepVisitor;

		// Required by implicit object serialization, disabled for general use.
		friend class FImplicitObject;

		FTriangleMeshImplicitObject()
		    : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh){};

		template<typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;

		template<typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD) const;

		template <typename IdxType>
		bool RaycastImp(const TArray<TVector<IdxType, 3>>& Elements, const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const;

		template <typename IdxType>
		bool OverlapImp(const TArray<TVec3<IdxType>>& Elements, const FVec3& Point, const FReal Thickness) const;

		template<typename IdxType>
		int32 FindMostOpposingFace(const TArray<TVec3<IdxType>>& Elements, const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const;

		template <typename IdxType>
		void RebuildBVImp(const TArray<TVec3<IdxType>>& Elements);
	};
}
