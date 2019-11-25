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

	template<typename T>
	class CHAOS_API TTriangleMeshImplicitObject final : public FImplicitObject
	{
	public:
		using FImplicitObject::GetTypeName;

		TTriangleMeshImplicitObject(TParticles<T,3>&& Particles, TArray<TVector<int32, 3>>&& Elements, TArray<uint16>&& InMaterialIndices);
		TTriangleMeshImplicitObject(const TTriangleMeshImplicitObject& Other) = delete;
		TTriangleMeshImplicitObject(TTriangleMeshImplicitObject&& Other) = default;
		virtual ~TTriangleMeshImplicitObject() {}

		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const
		{
			ensure(false);	//not supported yet - might support it in the future or we may change the interface
			return TNumericLimits<T>::Max();
		}

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const TVector<T, 3>& Point, const T Thickness) const override;

		bool OverlapGeom(const TSphere<T,3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const FConvex& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool OverlapGeom(const TImplicitObjectScaled<TImplicitObjectScaled<FConvex>>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;

		bool SweepGeom(const TSphere<T,3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T,3>& Dir, const T Length, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TBox<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TCapsule<T>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const FConvex& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TSphere<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TBox<T, 3>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<TCapsule<T>>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;
		bool SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const bool bComputeMTD = false) const;

		virtual int32 FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex, T SearchDistance) const override;
		virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, 3>& DenormDir, int32 FaceIndex, const TVector<T, 3>& OriginalNormal) const override;

		virtual const TBox<T, 3>& BoundingBox() const
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
			Ar << MLocalBoundingBox;

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::RemovedConvexHullsFromTriangleMeshImplicitObject)
			{
				TUniquePtr<TGeometryParticles<T, 3>> ConvexHulls;
				Ar << ConvexHulls;
			}

			if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TrimeshSerializesBV)
			{
				// Should now only hit when loading older trimeshes
				RebuildBV();
			}
			else if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::TrimeshSerializesAABBTree)
			{
				TBoundingVolume<int32, T, 3> Dummy;
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

		virtual void Serialize(FChaosArchive& Ar) override
		{
			FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
			SerializeImp(Ar);
		}

		virtual uint32 GetTypeHash() const override
		{
			uint32 Result = MParticles.GetTypeHash();
			Result = HashCombine(Result, MLocalBoundingBox.GetTypeHash());

			for(TVector<int32, 3> Tri : MElements)
			{
				uint32 TriHash = HashCombine(::GetTypeHash(Tri[0]), HashCombine(::GetTypeHash(Tri[1]), ::GetTypeHash(Tri[2])));
				Result = HashCombine(Result, TriHash);
			}

			return Result;
		}

		TVector<T, 3> GetFaceNormal(const int32 FaceIdx) const;

		virtual uint16 GetMaterialIndex(uint32 HintIndex) const override
		{
			if (MaterialIndices.IsValidIndex(HintIndex))
			{
				return MaterialIndices[HintIndex];
			}

			// 0 should always be the default material for a shape
			return 0;
		}

	private:

		void RebuildBV();

		TParticles<T, 3> MParticles;
		TArray<TVector<int32, 3>> MElements;
		TBox<T, 3> MLocalBoundingBox;
		TArray<uint16> MaterialIndices;

		//using BVHType = TBoundingVolume<int32, T, 3>;
		using BVHType = TAABBTree<int32, TAABBTreeLeafArray<int32, T>, T>;

		template<typename InStorageType, typename InRealType>
		friend struct FBvEntry;

		struct FBvEntry
		{
			TTriangleMeshImplicitObject* TmData;
			int32 Index;

			bool HasBoundingBox() const { return true; }

			TBox<T, 3> BoundingBox() const
			{
				TBox<T, 3> Bounds(TmData->MParticles.X(TmData->MElements[Index][0]), TmData->MParticles.X(TmData->MElements[Index][0]));

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

		template <typename Geom, typename R>
		friend struct TTriangleMeshSweepVisitor;

		// Required by implicit object serialization, disabled for general use.
		friend class FImplicitObject;

		TTriangleMeshImplicitObject() 
			: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh)
		{};

		template <typename QueryGeomType>
		bool OverlapGeomImp(const QueryGeomType& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;

		template <typename QueryGeomType>
		bool SweepGeomImp(const QueryGeomType& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, const bool bComputeMTD) const;
	};
}

