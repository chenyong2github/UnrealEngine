// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActorDesc.h"

#if WITH_EDITOR
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"
#include "Hash/CityHashHelpers.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

void FHLODActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	const AWorldPartitionHLOD* HLODActor = CastChecked<AWorldPartitionHLOD>(InActor);

	HLODSubActors.Reserve(HLODActor->GetSubActors().Num());
	Algo::Transform(HLODActor->GetSubActors(), HLODSubActors, [](const FHLODSubActor& SubActor) { return FHLODSubActorDesc(SubActor.ActorGuid, SubActor.ContainerID); });
	
	CellHash = 0;
	if (const UHLODLayer* SubActorsHLODLayer = HLODActor->GetSubActorsHLODLayer())
	{
		uint64 GridIndexX;
		uint64 GridIndexY;
		uint64 GridIndexZ;
		HLODActor->GetGridIndices(GridIndexX, GridIndexY, GridIndexZ);

		FDataLayersID DataLayersID(HLODActor->GetDataLayerObjects());

		CellHash = ComputeCellHash(SubActorsHLODLayer->GetName(), GridIndexX, GridIndexY, GridIndexZ, DataLayersID);
	}
}

void FHLODActorDesc::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	FWorldPartitionActorDesc::Serialize(Ar);

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::WorldPartitionHLODActorDescSerializeHLODSubActors)
	{
		TArray<FGuid> SubActors;
		Ar << SubActors;
	}
	else
	{
		Ar << HLODSubActors;
	}
	
	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionHLODActorDescSerializeHLODLayer)
	{
		FString HLODLayer_Deprecated;
		Ar << HLODLayer_Deprecated;
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionHLODActorDescSerializeCellHash)
	{
		Ar << CellHash;
	}
}

bool FHLODActorDesc::Equals(const FWorldPartitionActorDesc* Other) const
{
	if (FWorldPartitionActorDesc::Equals(Other))
	{
		const FHLODActorDesc* HLODActorDesc = (FHLODActorDesc*)Other;

		if (CellHash == HLODActorDesc->CellHash && HLODSubActors.Num() == HLODActorDesc->HLODSubActors.Num())
		{
			TArray<FHLODSubActorDesc> SortedSubActors(HLODSubActors);
			TArray<FHLODSubActorDesc> SortedSubActorsOther(HLODActorDesc->HLODSubActors);
			SortedSubActors.Sort();
			SortedSubActorsOther.Sort();
			return SortedSubActors == SortedSubActorsOther;
		}
	}

	return false;
}

uint64 FHLODActorDesc::ComputeCellHash(const FString HLODLayerName, uint64 GridIndexX, uint64 GridIndexY, uint64 GridIndexZ, FDataLayersID DataLayersID)
{
	uint64 CellHash = FCrc::StrCrc32(*HLODLayerName);
	CellHash = AppendCityHash(GridIndexX, CellHash);
	CellHash = AppendCityHash(GridIndexY, CellHash);
	CellHash = AppendCityHash(GridIndexZ, CellHash);
	return HashCombine(DataLayersID.GetHash(), CellHash);
}
#endif
