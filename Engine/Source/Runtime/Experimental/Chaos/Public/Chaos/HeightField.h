// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/Particles.h"
#include "Chaos/SegmentMesh.h"
#include "ImplicitObject.h"
#include "Box.h"
#include "TriangleMeshImplicitObject.h"

namespace Chaos
{
	template<typename T>
	class CHAOS_API THeightField final : public TImplicitObject<T,3>
	{
	public:
		//IMPLICIT_OBJECT_SERIALIZER(THeightField)
		THeightField(TArray<T>&& Height, int32 InNumRows, int32 InNumCols, const TVector<T,3>& Scale);
		THeightField(const THeightField& Other) = delete;
		THeightField(THeightField&& Other) = default;
		virtual ~THeightField() {}

		virtual T PhiWithNormal(const TVector<T, 3>& x, TVector<T, 3>& Normal) const
		{
			check(false);	//not supported yet - might support it in the future or we may change the interface
			return (T)0;
		}

		virtual bool Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,3>& OutPosition, TVector<T,3>& OutNormal, int32& OutFaceIndex) const override;
		virtual bool Overlap(const TVector<T, 3>& Point, const T Thickness) const override;
		bool OverlapGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness) const;
		bool SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness = 0) const;
		virtual int32 FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex) const override;

		virtual const TBox<T, 3>& BoundingBox() const
		{
			return MTriangleMeshImplicitObject->BoundingBox();
		}

		virtual uint32 GetTypeHash() const override
		{
			return MTriangleMeshImplicitObject->GetTypeHash();
		}

		static ImplicitObjectType GetType()
		{
			return ImplicitObjectType::HeightField;
		}

	private:
		//For now just convert into a trimesh
		TUniquePtr<TTriangleMeshImplicitObject<T>> MTriangleMeshImplicitObject;
	};
}
