// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"
#include "PhysicsEngine/BodySetup.h"
namespace Chaos
{
	class FImplicitObject;

	template <typename T>
	class TTriangleMeshImplicitObject;

	class FConvex;
}

struct FUntypedBulkData;

template<typename T, int d>
class FChaosDerivedDataReader
{
public:

	// Only valid use is to explicitly read chaos bulk data
	explicit FChaosDerivedDataReader(FUntypedBulkData* InBulkData);

	TArray<TUniquePtr<Chaos::FConvex>> ConvexImplicitObjects;
	TArray<TSharedPtr<Chaos::TTriangleMeshImplicitObject<T>, ESPMode::ThreadSafe>> TrimeshImplicitObjects;
	FBodySetupUVInfo UVInfo;
	TArray<int32> FaceRemap;

private:
	FChaosDerivedDataReader() = delete;
	FChaosDerivedDataReader(const FChaosDerivedDataReader& Other) = delete;
	FChaosDerivedDataReader(FChaosDerivedDataReader&& Other) = delete;
	FChaosDerivedDataReader& operator =(const FChaosDerivedDataReader& Other) = delete;
	FChaosDerivedDataReader& operator =(FChaosDerivedDataReader&& Other) = delete;

	bool bReadSuccessful;

};

extern template class FChaosDerivedDataReader<float, 3>;
