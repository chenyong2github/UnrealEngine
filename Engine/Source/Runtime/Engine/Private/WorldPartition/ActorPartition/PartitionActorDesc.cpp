// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#include "ActorPartition/PartitionActor.h"

#if WITH_EDITOR
#include "Engine/Level.h"

bool FPartitionActorDesc::Init(const AActor* InActor)
{
	if (FWorldPartitionActorDesc::Init(InActor))
	{
		const APartitionActor* PartitionActor = CastChecked<APartitionActor>(InActor);

		GridSize = PartitionActor->GridSize;

		const FVector ActorLocation = InActor->GetActorLocation();
		GridIndexX = FMath::FloorToInt(ActorLocation.X / GridSize);
		GridIndexY = FMath::FloorToInt(ActorLocation.Y / GridSize);
		GridIndexZ = FMath::FloorToInt(ActorLocation.Z / GridSize);

		UpdateHash();
		return true;
	}

	return false;
}

void FPartitionActorDesc::SerializeMetaData(FActorMetaDataSerializer* Serializer)
{
	FWorldPartitionActorDesc::SerializeMetaData(Serializer);

	Serializer->Serialize(TEXT("GridSize"), (int32&)GridSize);
	Serializer->Serialize(TEXT("GridIndexX"), GridIndexX);
	Serializer->Serialize(TEXT("GridIndexY"), GridIndexY);
	Serializer->Serialize(TEXT("GridIndexZ"), GridIndexZ);
}

void FPartitionActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	FWorldPartitionActorDesc::BuildHash(HashBuilder);
	HashBuilder << GridSize << GridIndexX << GridIndexY << GridIndexZ;
}
#endif
