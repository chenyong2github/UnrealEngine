// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace Chaos
{
	enum class EGeometryParticlesSimType
	{
		RigidBodySim,
		Other
	};

	template<class T, int d, EGeometryParticlesSimType SimType>
	class TGeometryParticlesImp;

	template <typename T, int d>
	using TGeometryParticles = TGeometryParticlesImp<T, d, EGeometryParticlesSimType::RigidBodySim>;

	template <typename T, int d>
	using TGeometryClothParticles = TGeometryParticlesImp<T, d, EGeometryParticlesSimType::Other>;


	struct FSpatialAccelerationIdx
	{
		uint16 Bucket : 3;
		uint16 InnerIdx : 13;

		static constexpr uint16 MaxBucketEntries = 1 << 13;

		bool operator==(const FSpatialAccelerationIdx& Rhs) const
		{
			return ((const uint16&)*this) == ((const uint16&)Rhs);
		}
	};

	inline uint32 GetTypeHash(const FSpatialAccelerationIdx& Idx)
	{
		return ::GetTypeHash((const uint16&) Idx);
	}

	inline FArchive& operator<<(FArchive& Ar, FSpatialAccelerationIdx& Idx)
	{
		return Ar << (uint16&)Idx;
	}
}