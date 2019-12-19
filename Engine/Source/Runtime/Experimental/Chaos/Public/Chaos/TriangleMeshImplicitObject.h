// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/SegmentMesh.h"
#include "Chaos/ImplicitObjectScaled.h"

#include "ImplicitObject.h"
#include "Box.h"
#include "BoundingVolumeHierarchy.h"
#include "BoundingVolume.h"
#include "ChaosArchive.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"
#include "AABBTree.h"

namespace Chaos
{

	template <typename T>
	class TCapsule;

	class FConvex;

	class CHAOS_API FTriangleMeshImplicitObject final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		FTriangleMeshImplicitObject(TParticles<FReal,3>&& Particles, TArray<TVector<int32, 3>>&& Elements, TArray<uint16>&& InMaterialIndices);
		FTriangleMeshImplicitObject(const FTriangleMeshImplicitObject& Other) = delete;
		FTriangleMeshImplicitObject(FTriangleMeshImplicitObject&& Other) = default;
		virtual ~FTriangleMeshImplicitObject() {}

		virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const;
		
		virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const FVec3& Point, const FReal Thickness) const override;

		bool OverlapGeom(const TSphere<FReal,3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TCapsule<FReal>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TCapsule<FReal>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TImplicitObjectScaled<FConvex>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;

		bool SweepGeom(const TSphere<FReal,3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness = 0, const bool bComputeMTD = false) const;
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

			if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TrimeshSerializesBV)
			{
				// Should now only hit when loading older trimeshes
				RebuildBV();
			}
			else if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TrimeshSerializesAABBTree)
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

			if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddTrimeshMaterialIndices)
			{
				Ar << MaterialIndices;
			}
		}

		virtual void Serialize(FChaosArchive& Ar) override;

		virtual uint32 GetTypeHash() const override;

		FVec3 GetFaceNormal(const int32 FaceIdx) const;

		virtual uint16 GetMaterialIndex(uint32 HintIndex) const override;

	private:

		void RebuildBV();

		TParticles<FReal, 3> MParticles;
		TArray<TVector<int32, 3>> MElements;
		TAABB<FReal, 3> MLocalBoundingBox;
		TArray<uint16> MaterialIndices;

		//using BVHType = TBoundingVolume<int32, T, 3>;
		using BVHType = TAABBTree<int32, TAABBTreeLeafArray<int32, FReal>, FReal>;

		template<typename InStorageType, typename InRealType>
		friend struct FBvEntry;

		struct FBvEntry
		{
			FTriangleMeshImplicitObject* TmData;
			int32 Index;

			bool HasBoundingBox() const { return true; }

			TAABB<FReal, 3> BoundingBox() const
			{
				TAABB<FReal, 3> Bounds(TmData->MParticles.X(TmData->MElements[Index][0]), TmData->MParticles.X(TmData->MElements[Index][0]));

				Bounds.GrowToInclude(TmData->MParticles.X(TmData->MElements[Index][1]));
				Bounds.GrowToInclude(TmData->MParticles.X(TmData->MElements[Index][2]));

				return Bounds;
			}

			template <typename TPayloadType>
			int32 GetPayload(int32 Idx) const
			{
				return Idx;
			}
		};
		TArray<FBvEntry> BVEntries;

		BVHType BVH;

		template <typename Geom>
		friend struct FTriangleMeshSweepVisitor;

		// Required by implicit object serialization, disabled for general use.
		friend class FImplicitObject;

		FTriangleMeshImplicitObject() 
			: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh)
		{};

		template <typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness) const;

		template <typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD) const;
	};
}

