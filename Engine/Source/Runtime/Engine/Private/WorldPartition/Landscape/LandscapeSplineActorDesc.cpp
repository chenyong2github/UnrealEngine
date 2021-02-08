// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/Landscape/LandscapeSplineActorDesc.h"

#if WITH_EDITOR

#include "LandscapeSplineActor.h"
#include "LandscapeInfo.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "UObject/UE5MainStreamObjectVersion.h"

void FLandscapeSplineActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const ALandscapeSplineActor* LandscapeSplineActor = CastChecked<ALandscapeSplineActor>(InActor);
	check(LandscapeSplineActor);
	LandscapeGuid = LandscapeSplineActor->GetLandscapeGuid();
}

void FLandscapeSplineActorDesc::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	FWorldPartitionActorDesc::Serialize(Ar);

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::AddedLandscapeSplineActorDesc)
	{
		Ar << LandscapeGuid;
	}
}

void FLandscapeSplineActorDesc::OnRegister()
{
	FWorldPartitionActorDesc::OnRegister();

	if (ULandscapeInfo* LandscapeInfo = ULandscapeInfo::FindOrCreate(Container->GetWorld(), LandscapeGuid))
	{
		FWorldPartitionHandle Handle(Container, GetGuid());
		check(Handle.IsValid());
		LandscapeInfo->SplineHandles.Add(MoveTemp(Handle));
	}
}

void FLandscapeSplineActorDesc::OnUnregister()
{
	FWorldPartitionActorDesc::OnUnregister();

	if (ULandscapeInfo* LandscapeInfo = ULandscapeInfo::FindOrCreate(Container->GetWorld(), LandscapeGuid))
	{
		FWorldPartitionHandle Handle(Container, GetGuid());
		check(Handle.IsValid());
		LandscapeInfo->SplineHandles.Remove(Handle);
	}
}

#endif
