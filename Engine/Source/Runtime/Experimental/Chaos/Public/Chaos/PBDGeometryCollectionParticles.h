// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/PBDRigidParticles.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDGeometryCollectionParticles : public TPBDRigidParticles<T, d>
	{
	public:
		TPBDGeometryCollectionParticles()
		    : TPBDRigidParticles<T, d>()
		{
			InitHelper();
		}
		TPBDGeometryCollectionParticles(const TPBDRigidParticles<T, d>& Other) = delete;

		TPBDGeometryCollectionParticles(TPBDRigidParticles<T, d>&& Other)
		    : TPBDRigidParticles<T, d>(MoveTemp(Other))
		{
			InitHelper();
		}
		~TPBDGeometryCollectionParticles()
		{}

		typedef TPBDGeometryCollectionParticleHandle<T, d> THandleType;
		const THandleType* Handle(int32 Index) const { return static_cast<const THandleType*>(TGeometryParticles<T, d>::Handle(Index)); }
		THandleType* Handle(int32 Index) { return static_cast<THandleType*>(TGeometryParticles<T, d>::Handle(Index)); }

	private:
		void InitHelper() { this->MParticleType = EParticleType::GeometryCollection; }
	};
} // namespace Chaos