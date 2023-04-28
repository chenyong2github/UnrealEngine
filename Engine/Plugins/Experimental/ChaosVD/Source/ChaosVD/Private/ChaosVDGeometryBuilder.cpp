// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDGeometryBuilder.h"

#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "UDynamicMesh.h"

#include "UObject/UObjectGlobals.h"

void FChaosVDGeometryBuilder::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddStableReferenceMap(MeshCacheMap);
	Collector.AddStableReferenceMap(StaticMeshCacheMap);
}

UDynamicMesh* FChaosVDGeometryBuilder::CreateAndCacheDynamicMesh(const uint32 GeometryCacheKey, UE::Geometry::FMeshShapeGenerator& MeshGenerator)
{
	//TODO: Make this return what is cached when the system is more robust
	// For now this should not happen and we want to catch it and make it visually noticeable
	if (MeshCacheMap.Contains(GeometryCacheKey))
	{
		ensureMsgf(false, TEXT("Tried to create a new mesh with an existing Cache key"));
		return nullptr;
	}

	UDynamicMesh* Mesh = NewObject<UDynamicMesh>();
	Mesh->SetMesh(&MeshGenerator.Generate());

	MeshCacheMap.Add(GeometryCacheKey, Mesh);

	return Mesh;
}

UStaticMesh* FChaosVDGeometryBuilder::CreateAndCacheStaticMesh(const uint32 GeometryCacheKey, UE::Geometry::FMeshShapeGenerator& MeshGenerator)
{
	//TODO: Make this return what is cached when the system is more robust
	// For now this should not happen and we want to catch it and make it visually noticeable
	if (StaticMeshCacheMap.Contains(GeometryCacheKey))
	{
		ensureMsgf(false, TEXT("Tried to create a new mesh with an existing Cache key"));
		return nullptr;
	}

	//TODO: Instead of generating a dynamic mesh and discard it, we should
	// Create a Mesh description directly.
	// We could create a base class for our mesh Generators and add a Generate method that generates these mesh descriptions
	UDynamicMesh* Mesh = NewObject<UDynamicMesh>();
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>();
	StaticMesh->GetStaticMaterials().Add(FStaticMaterial());

	Mesh->SetMesh(&MeshGenerator.Generate());

	FMeshDescription MeshDescription;
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();

	FDynamicMeshToMeshDescription Converter;
	Converter.Convert(Mesh->GetMeshPtr(), MeshDescription, true);

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bUseHashAsGuid = true;
	Params.bMarkPackageDirty = false;
	Params.bBuildSimpleCollision = false;
	Params.bCommitMeshDescription = false;
	Params.bFastBuild = true;

	StaticMesh->NaniteSettings.bEnabled = true;
	StaticMesh->BuildFromMeshDescriptions({&MeshDescription}, Params);

	StaticMeshCacheMap.Add(GeometryCacheKey, StaticMesh);

	return StaticMesh;
}

