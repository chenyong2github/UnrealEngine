// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/Landscape/LandscapeActorDesc.h"

#if WITH_EDITOR
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"

void FLandscapeActorDesc::Init(const AActor* InActor)
{
	FPartitionActorDesc::Init(InActor);

	const ALandscapeProxy* LandscapeProxy = CastChecked<ALandscapeProxy>(InActor);
	check(LandscapeProxy);
	GridIndexX = LandscapeProxy->LandscapeSectionOffset.X / LandscapeProxy->GridSize;
	GridIndexY = LandscapeProxy->LandscapeSectionOffset.Y / LandscapeProxy->GridSize;
	GridIndexZ = 0;
}

void FLandscapeActorDesc::Unload()
{
	if (ALandscapeStreamingProxy* LandscapeStreamingProxy = Cast<ALandscapeStreamingProxy>(GetActor()))
	{
		LandscapeStreamingProxy->ActorDescReferences.Empty();
	}

	FPartitionActorDesc::Unload();
}

void FLandscapeActorDesc::OnRegister()
{
	FPartitionActorDesc::OnRegister();

	if (ULandscapeInfo* LandscapeInfo = ULandscapeInfo::Find(WorldPartition->GetWorld(), GridGuid))
	{
		FWorldPartitionHandle Handle(WorldPartition, GetGuid());
		check(Handle.IsValid());
		LandscapeInfo->ProxyHandles.Add(MoveTemp(Handle));
	}
}

void FLandscapeActorDesc::OnUnregister()
{
	FPartitionActorDesc::OnUnregister();

	if (ULandscapeInfo* LandscapeInfo = ULandscapeInfo::Find(WorldPartition->GetWorld(), GridGuid))
	{
		FWorldPartitionHandle Handle(WorldPartition, GetGuid());
		check(Handle.IsValid());
		LandscapeInfo->ProxyHandles.Remove(Handle);
	}
}

#endif
