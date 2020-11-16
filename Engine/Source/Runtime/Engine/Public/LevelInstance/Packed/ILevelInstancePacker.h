// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "LevelInstance/Packed/PackedLevelInstanceTypes.h"

class APackedLevelInstance;
class AActor;
class UActorComponent;
class FPackedLevelInstanceBuilder;
class FPackedLevelInstanceBuilderContext;

class ENGINE_API ILevelInstancePacker
{
public:
	ILevelInstancePacker() {}
	virtual ~ILevelInstancePacker() {}
	virtual FLevelInstancePackerID GetID() const = 0;
	virtual void GetPackClusters(FPackedLevelInstanceBuilderContext& InContext, AActor* InActor) const = 0;
	virtual void PackActors(FPackedLevelInstanceBuilderContext& InBuilder, APackedLevelInstance* InPackingActor, const FLevelInstancePackerClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const = 0;
private:
	ILevelInstancePacker(const ILevelInstancePacker&) = delete;
	ILevelInstancePacker& operator=(const ILevelInstancePacker&) = delete;
};

#endif