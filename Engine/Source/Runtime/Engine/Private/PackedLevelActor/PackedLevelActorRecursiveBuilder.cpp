// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackedLevelActor/PackedLevelActorRecursiveBuilder.h"

#if WITH_EDITOR

#include "LevelInstance/LevelInstanceInterface.h"
#include "PackedLevelActor/PackedLevelActorBuilder.h"
#include "PackedLevelActor/PackedLevelActor.h"

#include "LevelInstance/LevelInstanceSubsystem.h"

#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/Brush.h"
#include "GameFramework/WorldSettings.h"

FPackedLevelActorBuilderID FPackedLevelActorRecursiveBuilder::BuilderID = 'RECP';

FPackedLevelActorBuilderID FPackedLevelActorRecursiveBuilder::GetID() const
{
	return BuilderID;
}

void FPackedLevelActorRecursiveBuilder::GetPackClusters(FPackedLevelActorBuilderContext& InContext, AActor* InActor) const
{
	if (ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(InActor))
	{
		FPackedLevelActorBuilderClusterID ClusterID(MakeUnique<FPackedLevelActorRecursiveBuilderCluster>(GetID(), LevelInstance));
		InContext.FindOrAddCluster(MoveTemp(ClusterID));

		// This Actor can be safely discarded without warning because it is a container
		InContext.DiscardActor(InActor);

		ULevelInstanceSubsystem* LevelInstanceSubsystem = LevelInstance->GetLevelInstanceSubsystem();
		check(LevelInstanceSubsystem);
		if (ULevel* Level = LevelInstanceSubsystem->GetLevelInstanceLevel(LevelInstance))
		{
			for (AActor* LevelActor : Level->Actors)
			{
				if (LevelActor)
				{
					if (LevelActor == Level->GetDefaultBrush())
					{
						InContext.DiscardActor(LevelActor);
					}
					else if (LevelActor == Level->GetWorldSettings())
					{
						InContext.DiscardActor(LevelActor);
					}
					else
					{
						InContext.ClusterLevelActor(LevelActor);
					}
				}
			}
		}
	}
}

void FPackedLevelActorRecursiveBuilder::PackActors(FPackedLevelActorBuilderContext& InContext, const FPackedLevelActorBuilderClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const
{
	check(InClusterID.GetBuilderID() == GetID());
	FPackedLevelActorRecursiveBuilderCluster* LevelInstanceCluster = (FPackedLevelActorRecursiveBuilderCluster*)InClusterID.GetData();
	check(LevelInstanceCluster);

	if (APackedLevelActor* PackedLevelActor = Cast<APackedLevelActor>(LevelInstanceCluster->LevelInstance))
	{
		if (UBlueprint* GeneratedBy = PackedLevelActor->GetRootBlueprint())
		{
			InContext.GetPackedLevelActor()->PackedBPDependencies.AddUnique(GeneratedBy);
		}
	}
}

FPackedLevelActorRecursiveBuilderCluster::FPackedLevelActorRecursiveBuilderCluster(FPackedLevelActorBuilderID InBuilderID, ILevelInstanceInterface* InLevelInstance)
	: FPackedLevelActorBuilderCluster(InBuilderID), LevelInstance(InLevelInstance)
{
}

bool FPackedLevelActorRecursiveBuilderCluster::Equals(const FPackedLevelActorBuilderCluster& InOther) const
{
	if (!FPackedLevelActorBuilderCluster::Equals(InOther))
	{
		return false;
	}

	const FPackedLevelActorRecursiveBuilderCluster& LevelInstanceCluster = (const FPackedLevelActorRecursiveBuilderCluster&)InOther;
	return LevelInstance == LevelInstanceCluster.LevelInstance;
}
	
uint32 FPackedLevelActorRecursiveBuilderCluster::ComputeHash() const
{
	return FCrc::TypeCrc32(LevelInstance->GetLevelInstanceID(), FPackedLevelActorBuilderCluster::ComputeHash());
}

#endif
