// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "LevelInstance/Packed/PackedLevelInstanceTypes.h"
#include "LevelInstance/Packed/ILevelInstancePacker.h"

class AActor;
class APackedLevelInstance;
class UActorComponent;
class FPackedLevelInstanceBuilderContext;

class FLevelInstanceISMPacker : public ILevelInstancePacker
{
public:
	static FLevelInstancePackerID PackerID;

	virtual FLevelInstancePackerID GetID() const override;
	virtual void GetPackClusters(FPackedLevelInstanceBuilderContext& InContext, AActor* InActor) const override;
	virtual void PackActors(FPackedLevelInstanceBuilderContext& InContext, APackedLevelInstance* InPackingActor, const FLevelInstancePackerClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const override;
};

class UStaticMesh;
class UMaterialInterface;

class FLevelInstanceISMPackerCluster : public FLevelInstancePackerCluster
{
public:
	FLevelInstanceISMPackerCluster(FLevelInstancePackerID InPackerID, UStaticMeshComponent* InComponent);

	virtual bool operator==(const FLevelInstancePackerCluster& InOther) const override;
	virtual uint32 ComputeHash() const override;

	// UStaticMeshComponent that contribute to the hash. More can be added if needed.
	UStaticMesh* StaticMesh = nullptr;
	TArray<UMaterialInterface*> Materials;
	bool bReceivesDecals;
	bool bCastShadow;
	bool bVisibleInRayTracing;
};

#endif