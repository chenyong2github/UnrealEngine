// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "Templates/UniquePtr.h"
#include "PhysicsEngine/BodySetup.h"
namespace Chaos
{
	template<typename T, int d>
	class TImplicitObject;

	template <typename T>
	class TTriangleMeshImplicitObject;
}

struct FUntypedBulkData;

template<typename T, int d>
class FChaosDerivedDataReader
{
public:

	// Only valid use is to explicitly read chaos bulk data
	explicit FChaosDerivedDataReader(FUntypedBulkData* InBulkData);

	TArray<TUniquePtr<Chaos::TImplicitObject<T, d>>> ConvexImplicitObjects;
	TArray<TUniquePtr<Chaos::TTriangleMeshImplicitObject<T>>> TrimeshImplicitObjects;
	FBodySetupUVInfo UVInfo;

private:
	FChaosDerivedDataReader() = delete;
	FChaosDerivedDataReader(const FChaosDerivedDataReader& Other) = delete;
	FChaosDerivedDataReader(FChaosDerivedDataReader&& Other) = delete;
	FChaosDerivedDataReader& operator =(const FChaosDerivedDataReader& Other) = delete;
	FChaosDerivedDataReader& operator =(FChaosDerivedDataReader&& Other) = delete;

	bool bReadSuccessful;

};

extern template class FChaosDerivedDataReader<float, 3>;

#endif