// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/SegmentMesh.h"
#include "ImplicitObject.h"
#include "Box.h"
#include "BoundingVolumeHierarchy.h"
#include "BoundingVolume.h"
#include "ChaosArchive.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"

namespace Chaos
{

	template<typename T>
	class CHAOS_API TTriangleMeshImplicitObject final : public TImplicitObject<T,3>
	{
	public:
		using TImplicitObject<T, 3>::GetTypeName;

		TTriangleMeshImplicitObject(TParticles<T,3>&& Particles, TArray<TVector<int32, 3>>&& Elements);
		TTriangleMeshImplicitObject(const TTriangleMeshImplicitObject& Other) = delete;
		TTriangleMeshImplicitObject(TTriangleMeshImplicitObject&& Other) = default;
		virtual ~TTriangleMeshImplicitObject() {}

		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const
		{
			ensure(false);	//not supported yet - might support it in the future or we may change the interface
			return (T)0;
		}

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const TVector<T, 3>& Point, const T Thickness) const override;

		bool OverlapGeom(const TImplicitObject<T,3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T,3>& Dir, const T Length, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0) const;
		virtual int32 FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex, T SearchDistance) const override;
		virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, 3>& DenormDir, int32 FaceIndex, const TVector<T, 3>& OriginalNormal) const override;

		virtual const TBox<T, 3>& BoundingBox() const
		{
			return MLocalBoundingBox;
		}

		static ImplicitObjectType GetType()
		{
			return ImplicitObjectType::TriangleMesh;
		}

		void SerializeImp(FChaosArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

			TImplicitObject<T, 3>::SerializeImp(Ar);
			Ar << MParticles;
			Ar << MElements;
			Ar << MLocalBoundingBox;

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::RemovedConvexHullsFromTriangleMeshImplicitObject)
			{
				TUniquePtr<TGeometryParticles<T, 3>> ConvexHulls;
				Ar << ConvexHulls;
			}

			if(Ar.IsLoading())
			{
				RebuildBV();
			}
#if 0
#if 0
			// Disabled during 2-1 replacement. Replace when BV is actually working again
			Ar << ConvexHulls;
			Ar << BVH;

			if(Ar.IsLoading())
			{
				// Re-link the object array when we load this back in
				BVH.SetObjects(ConvexHulls);
			}
#else
			if(Ar.IsLoading())
			{
				RebuildBV();
			}
#endif
#endif
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

	private:

		void RebuildBV();

		TParticles<T, 3> MParticles;
		TArray<TVector<int32, 3>> MElements;
		TBox<T, 3> MLocalBoundingBox;

		using BVHType = TBoundingVolume<int32, T, 3>;

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

		template <typename R>
		friend struct TTriangleMeshSweepVisitor;

		// Required by implicit object serialization, disabled for general use.
		template<typename InnerT, int InnerD>
		friend class TImplicitObject;

		TTriangleMeshImplicitObject() 
			: TImplicitObject<T, 3>(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh)
		{};
	};
}

