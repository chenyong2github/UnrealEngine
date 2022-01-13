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
	LandscapeGuid = LandscapeSplineActor->GetLandscapeGuid();
}

bool FLandscapeSplineActorDesc::Equals(const FWorldPartitionActorDesc* Other) const
{
	if (FWorldPartitionActorDesc::Equals(Other))
	{
		const FLandscapeSplineActorDesc* LandscapeSplineActorDesc = (FLandscapeSplineActorDesc*)Other;
		return LandscapeGuid == LandscapeSplineActorDesc->LandscapeGuid;
	}

	return false;
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
#endif
