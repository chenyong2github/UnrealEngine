// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS

#include "CoreMinimal.h"
#include "DerivedDataPluginInterface.h"
#include "UObject/GCObject.h"
#include "Interface_CollisionDataProviderCore.h"

struct FBodySetupUVInfo;
struct FCookBodySetupInfo;

class UBodySetup;

namespace Chaos
{
	class FChaosArchive;

	template<typename T, int d>
	class TImplicitObject;

	template<typename T>
	class TTriangleMeshImplicitObject;
}

struct FChaosTriMeshCollisionBuildParameters
{
	bool bCollapseVerts;

	FTriMeshCollisionData MeshDesc;
	FBodySetupUVInfo* UVInfo;
};

struct FChaosConvexMeshCollisionBuildParameters
{
	bool bCollapseVerts;
	bool bMirror;
};

class FChaosDerivedDataCooker : public FDerivedDataPluginInterface, public FGCObject
{
public:

	using BuildPrecision = float;

	// FDerivedDataPluginInterface Interface
	virtual const TCHAR* GetPluginName() const override;
	virtual const TCHAR* GetVersionString() const override;
	virtual FString GetPluginSpecificCacheKeySuffix() const override;
	virtual bool IsBuildThreadsafe() const override;
	virtual bool Build(TArray<uint8>& OutData) override;
	//End FDerivedDataPluginInterface Interface

	// FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End FGCObject Interface

	FChaosDerivedDataCooker(UBodySetup* InSetup, FName InFormat);

	bool CanBuild()
	{
		return Setup != nullptr;
	}

private:

	template<typename Precision>
	void BuildInternal(Chaos::FChaosArchive& Ar, FCookBodySetupInfo& InInfo);

	template<typename Precision>
	void BuildTriangleMeshes(TArray<TUniquePtr<Chaos::TTriangleMeshImplicitObject<Precision>>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);

	template<typename Precision>
	void BuildConvexMeshes(TArray<TUniquePtr<Chaos::TImplicitObject<Precision, 3>>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);

	UBodySetup* Setup;
	FName RequestedFormat;
};

extern template void FChaosDerivedDataCooker::BuildTriangleMeshes(TArray<TUniquePtr<Chaos::TTriangleMeshImplicitObject<float>>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);
// #BGTODO When it's possible to build with doubles, re-enable this. (Currently at least TRigidTransform cannot build with double precision because we don't have a base transform implementation using them)
//extern template void FChaosDerivedDataCooker::BuildTriangleMeshes(TArray<Chaos::TCollisionTriangleMesh<double>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);

extern template void FChaosDerivedDataCooker::BuildConvexMeshes(TArray<TUniquePtr<Chaos::TImplicitObject<float, 3>>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);
// #BGTODO When it's possible to build with doubles, re-enable this. (Currently at least TRigidTransform cannot build with double precision because we don't have a base transform implementation using them)
//extern template void FChaosDerivedDataCooker::BuildConvexMeshes(TArray<Chaos::TCollisionConvexMesh<double>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);

extern template void FChaosDerivedDataCooker::BuildInternal<float>(Chaos::FChaosArchive& Ar, FCookBodySetupInfo& InInfo);
// #BGTODO When it's possible to build with doubles, re-enable this. (Currently at least TRigidTransform cannot build with double precision because we don't have a base transform implementation using them)
//extern template void FChaosDerivedDataCooker::BuildInternal<float>(FArchive& Ar, FCookBodySetupInfo& InInfo);

#endif