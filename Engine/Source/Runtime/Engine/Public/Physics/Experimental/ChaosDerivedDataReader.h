// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"
#include "PhysicsEngine/BodySetup.h"
namespace Chaos
{
	class FImplicitObject;

	template <typename T>
	class TTriangleMeshImplicitObject;

	template <typename T, int d>
	class TConvex;
}

struct FUntypedBulkData;

template<typename T, int d>
class FChaosDerivedDataReader
{
public:

	// Only valid use is to explicitly read chaos bulk data
	explicit FChaosDerivedDataReader(FUntypedBulkData* InBulkData);

	TArray<TUniquePtr<Chaos::FConvex>> ConvexImplicitObjects;
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
