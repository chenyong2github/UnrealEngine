// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "LevelInstance/Packed/PackedLevelInstanceTypes.h"
#include "LevelInstance/Packed/ILevelInstancePacker.h"

class AActor;
class ALevelInstance;
class APackedLevelInstance;
class UActorComponent;
class FPackedLevelInstanceBuilderContext;

class FLevelInstanceRecursivePacker : public ILevelInstancePacker
{
public:
	static FLevelInstancePackerID PackerID;

	virtual FLevelInstancePackerID GetID() const override;
	virtual void GetPackClusters(FPackedLevelInstanceBuilderContext& InContext, AActor* InActor) const override;
	virtual void PackActors(FPackedLevelInstanceBuilderContext& InContext, APackedLevelInstance* InPackingActor, const FLevelInstancePackerClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const override;
};

class FLevelInstanceRecursivePackerCluster : public FLevelInstancePackerCluster
{
public:
	FLevelInstanceRecursivePackerCluster(FLevelInstancePackerID InPackerID, ALevelInstance* InLevelInstance);

	virtual bool operator==(const FLevelInstancePackerCluster& InOther) const override;
	virtual uint32 ComputeHash() const override;

	ALevelInstance* LevelInstance = nullptr;
};

#endif