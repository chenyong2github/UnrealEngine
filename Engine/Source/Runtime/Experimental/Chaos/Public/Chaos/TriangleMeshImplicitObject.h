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
		//IMPLICIT_OBJECT_SERIALIZER(TTriangleMeshImplicitObject)
		TTriangleMeshImplicitObject(TParticles<T,3>&& Particles, TArray<TVector<int32, 3>>&& Elements);
		TTriangleMeshImplicitObject(const TTriangleMeshImplicitObject& Other) = delete;
		TTriangleMeshImplicitObject(TTriangleMeshImplicitObject&& Other) = default;
		virtual ~TTriangleMeshImplicitObject() {}

		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const
		{
			check(false);	//not supported yet - might support it in the future or we may change the interface
			return (T)0;
		}

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal) const override;
		virtual bool Overlap(const TVector<T, 3>& Point, const T Thickness) const override;

		bool OverlapGeom(const TImplicitObject<T,3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, const TVector<T,3> Scale = TVector<T,3>(1)) const;
		bool SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T,3>& Dir, const T Length, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, const T Thickness = 0, const TVector<T,3> Scale = TVector<T,3>(1)) const;

		virtual const TBox<T, 3>& BoundingBox() const
		{
			return MLocalBoundingBox;
		}

	private:
		TParticles<T, 3> MParticles;
		TArray<TVector<int32, 3>> MElements;
		TBox<T, 3> MLocalBoundingBox;

		TGeometryParticles<T, 3> ConvexHulls;
		//using BVHType = TBoundingVolumeHierarchy<TGeometryParticles<T, 3>, TArray<int32>, T, 3>;
		//using BVHType = TBoundingVolumeHierarchy<TGeometryParticles<T, 3>, TBoundingVolume<TGeometryParticles<T,3>, T, 3>, T, 3>;
		using BVHType = TBoundingVolume<TGeometryParticles<T, 3>, T, 3>;
		BVHType BVH;

		template <typename R>
		friend struct TTriangleMeshSweepVisitor;
	};
}
