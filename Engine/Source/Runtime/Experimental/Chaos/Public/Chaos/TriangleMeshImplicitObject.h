// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/SegmentMesh.h"
#include "ImplicitObject.h"
#include "Box.h"
#include "BoundingVolumeHierarchy.h"
#include "BoundingVolume.h"

namespace Chaos
{
	template<typename T>
	class CHAOS_API TTriangleMeshImplicitObject final : public TImplicitObject<T,3>
	{
	public:
		IMPLICIT_OBJECT_SERIALIZER_DIM(TTriangleMeshImplicitObject, 3)
		TTriangleMeshImplicitObject(TParticles<T,3>&& Particles, TArray<TVector<int32, 3>>&& Elements);
		TTriangleMeshImplicitObject(const TTriangleMeshImplicitObject& Other) = delete;
		TTriangleMeshImplicitObject(TTriangleMeshImplicitObject&& Other) = default;
		virtual ~TTriangleMeshImplicitObject() {}

		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const
		{
			check(false);	//not supported yet - might support it in the future or we may change the interface
			return (T)0;
		}

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const TVector<T, 3>& Point, const T Thickness) const override;

		bool OverlapGeom(const TImplicitObject<T,3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, const TVector<T,3> Scale = TVector<T,3>(1)) const;
		bool SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T,3>& Dir, const T Length, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0, const TVector<T,3> Scale = TVector<T,3>(1)) const;
		virtual int32 FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex) const override;

		virtual const TBox<T, 3>& BoundingBox() const
		{
			return MLocalBoundingBox;
		}

		static ImplicitObjectType GetType()
		{
			return ImplicitObjectType::TriangleMesh;
		}

		void SerializeImp(FArchive& Ar)
		{
			TImplicitObject<T, 3>::SerializeImp(Ar);
			Ar << MParticles << MElements << MLocalBoundingBox;

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
				RebuildConvexHulls();
				RebuildBV();
			}
#endif
		}

		virtual void Serialize(FChaosArchive& Ar) override
		{
			SerializeImp(Ar);
		}

		virtual void Serialize(FArchive& Ar) override
		{
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

	private:

		void RebuildConvexHulls();
		void RebuildBV();

		TParticles<T, 3> MParticles;
		TArray<TVector<int32, 3>> MElements;
		TBox<T, 3> MLocalBoundingBox;

		TGeometryParticles<T, 3> ConvexHulls;
		//using BVHType = TBoundingVolumeHierarchy<TGeometryParticles<T, 3>, TArray<int32>, T, 3>;
		//using BVHType = TBoundingVolumeHierarchy<TGeometryParticles<T, 3>, TBoundingVolume<TGeometryParticles<T,3>, T, 3>, T, 3>;
		using BVHType = TBoundingVolume<TGeometryParticles<T, 3>, int32, T, 3>;
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
