// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

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
	};
}