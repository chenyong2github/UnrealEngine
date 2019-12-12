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

	class FImplicitObject;

	class FTriangleMeshImplicitObject;
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

	void BuildInternal(Chaos::FChaosArchive& Ar, FCookBodySetupInfo& InInfo);

	void BuildTriangleMeshes(TArray<TUniquePtr<Chaos::FTriangleMeshImplicitObject>>& OutTriangleMeshes, TArray<int32>& OutFaceRemap, const FCookBodySetupInfo& InParams);

	void BuildConvexMeshes(TArray<TUniquePtr<Chaos::FImplicitObject>>& OutTriangleMeshes, const FCookBodySetupInfo& InParams);

	UBodySetup* Setup;
	FName RequestedFormat;
};


#endif