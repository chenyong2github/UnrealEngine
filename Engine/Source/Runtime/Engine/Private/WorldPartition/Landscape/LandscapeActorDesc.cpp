// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/Landscape/LandscapeActorDesc.h"

#if WITH_EDITOR
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "UObject/UE5MainStreamObjectVersion.h"

void FLandscapeActorDesc::Init(const AActor* InActor)
{
	FPartitionActorDesc::Init(InActor);

	const ALandscapeProxy* LandscapeProxy = CastChecked<ALandscapeProxy>(InActor);
	check(LandscapeProxy);
	GridIndexX = LandscapeProxy->LandscapeSectionOffset.X / (int32)LandscapeProxy->GridSize;
	GridIndexY = LandscapeProxy->LandscapeSectionOffset.Y / (int32)LandscapeProxy->GridSize;
	GridIndexZ = 0;
}

void FLandscapeActorDesc::Serialize(FArchive& Ar)
{
	FPartitionActorDesc::Serialize(Ar);

	if (Ar.IsLoading() && Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::FLandscapeActorDescFixupGridIndices)
	{
		GridIndexX = (int32)(GridIndexX * GridSize) / (int32)GridSize;
		GridIndexY = (int32)(GridIndexY * GridSize) / (int32)GridSize;
	}
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

	if (ULandscapeInfo* LandscapeInfo = ULandscapeInfo::FindOrCreate(Container->GetWorld(), GridGuid))
	{
		FWorldPartitionHandle Handle(Container, GetGuid());
		check(Handle.IsValid());
		LandscapeInfo->ProxyHandles.Add(MoveTemp(Handle));
	}
}

void FLandscapeActorDesc::OnUnregister()
{
	FPartitionActorDesc::OnUnregister();

	if (ULandscapeInfo* LandscapeInfo = ULandscapeInfo::FindOrCreate(Container->GetWorld(), GridGuid))
	{
		FWorldPartitionHandle Handle(Container, GetGuid());
		check(Handle.IsValid());
		LandscapeInfo->ProxyHandles.Remove(Handle);
	}
}

#endif
