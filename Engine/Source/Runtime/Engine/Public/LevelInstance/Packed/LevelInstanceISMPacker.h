// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "LevelInstance/Packed/PackedLevelInstanceTypes.h"
#include "LevelInstance/Packed/ILevelInstancePacker.h"
#include "ISMPartition/ISMComponentDescriptor.h"

class AActor;
class APackedLevelInstance;
class UActorComponent;
class FPackedLevelInstanceBuilderContext;
class UStaticMeshComponent;

class FLevelInstanceISMPacker : public ILevelInstancePacker
{
public:
	static FLevelInstancePackerID PackerID;

	virtual FLevelInstancePackerID GetID() const override;
	virtual void GetPackClusters(FPackedLevelInstanceBuilderContext& InContext, AActor* InActor) const override;
	virtual void PackActors(FPackedLevelInstanceBuilderContext& InContext, APackedLevelInstance* InPackingActor, const FLevelInstancePackerClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const override;
};

class FLevelInstanceISMPackerCluster : public FLevelInstancePackerCluster
{
public:
	FLevelInstanceISMPackerCluster(FLevelInstancePackerID InPackerID, UStaticMeshComponent* InComponent);

	virtual bool operator==(const FLevelInstancePackerCluster& InOther) const override;
	virtual uint32 ComputeHash() const override;

	FISMComponentDescriptor ISMDescriptor;
};

#endif