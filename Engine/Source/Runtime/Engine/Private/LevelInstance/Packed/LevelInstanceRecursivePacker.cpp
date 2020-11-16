// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/Packed/LevelInstanceRecursivePacker.h"

#if WITH_EDITOR

#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"
#include "LevelInstance/Packed/PackedLevelInstanceActor.h"

#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

#include "Misc/Crc.h"

FLevelInstancePackerID FLevelInstanceRecursivePacker::PackerID = 'RECP';

FLevelInstancePackerID FLevelInstanceRecursivePacker::GetID() const
{
	return PackerID;
}

void FLevelInstanceRecursivePacker::GetPackClusters(FPackedLevelInstanceBuilderContext& InContext, AActor* InActor) const
{
	if (ALevelInstance* LevelInstance = Cast<ALevelInstance>(InActor))
	{
		FLevelInstancePackerClusterID ClusterID(MakeUnique<FLevelInstanceRecursivePackerCluster>(GetID(), LevelInstance));
		InContext.FindOrAddCluster(MoveTemp(ClusterID));

		// This Actor can be safely discarded without warning because it is a container
		InContext.DiscardActor(LevelInstance);

		ULevelInstanceSubsystem* LevelInstanceSubsystem = LevelInstance->GetLevelInstanceSubsystem();
		check(LevelInstanceSubsystem);
		if (ULevel* Level = LevelInstanceSubsystem->GetLevelInstanceLevel(LevelInstance))
		{
			for (AActor* LevelActor : Level->Actors)
			{
				if (LevelActor)
				{
					InContext.ClusterLevelActor(LevelActor);
				}
			}
		}
	}
}

void FLevelInstanceRecursivePacker::PackActors(FPackedLevelInstanceBuilderContext& InContext, APackedLevelInstance* InPackingActor, const FLevelInstancePackerClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const
{
	check(InClusterID.GetPackerID() == GetID());
	FLevelInstanceRecursivePackerCluster* LevelInstanceCluster = (FLevelInstanceRecursivePackerCluster*)InClusterID.GetData();
	check(LevelInstanceCluster);

	if (LevelInstanceCluster->LevelInstance->IsLevelInstancePathValid())
	{
		InPackingActor->PackDependencies.AddUnique(LevelInstanceCluster->LevelInstance->GetWorldAsset());
	}
}

FLevelInstanceRecursivePackerCluster::FLevelInstanceRecursivePackerCluster(FLevelInstancePackerID InPackerID, ALevelInstance* InLevelInstance)
	: FLevelInstancePackerCluster(InPackerID), LevelInstance(InLevelInstance)
{
}

bool FLevelInstanceRecursivePackerCluster::operator==(const FLevelInstancePackerCluster& InOther) const
{
	if (!FLevelInstancePackerCluster::operator==(InOther))
	{
		return false;
	}

	const FLevelInstanceRecursivePackerCluster& LevelInstanceCluster = (const FLevelInstanceRecursivePackerCluster&)InOther;
	return LevelInstance == LevelInstanceCluster.LevelInstance;
}
	
uint32 FLevelInstanceRecursivePackerCluster::ComputeHash() const
{
	return FCrc::TypeCrc32(LevelInstance->GetLevelInstanceID(), FLevelInstancePackerCluster::ComputeHash());
}

#endif
