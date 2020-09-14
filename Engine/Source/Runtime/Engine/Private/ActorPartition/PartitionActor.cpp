// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorPartition/PartitionActor.h"

#if WITH_EDITOR
#include "ActorRegistry.h"
#include "Components/BoxComponent.h"
#endif

APartitionActor::APartitionActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, GridSize(0)
#endif
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;
}

#if WITH_EDITOR
void APartitionActor::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

	static const FName NAME_GridSize(TEXT("GridSize"));
	FActorRegistry::SaveActorMetaData(NAME_GridSize, (int32)GridSize, OutTags);

	const FVector ActorLocation = GetActorLocation();
	const int64 GridIndexX = FMath::FloorToInt(ActorLocation.X / GridSize);
	const int64 GridIndexY = FMath::FloorToInt(ActorLocation.Y / GridSize);
	const int64 GridIndexZ = FMath::FloorToInt(ActorLocation.Z / GridSize);

	static const FName NAME_GridIndexX(TEXT("GridIndexX"));
	FActorRegistry::SaveActorMetaData(NAME_GridIndexX, GridIndexX, OutTags);

	static const FName NAME_GridIndexY(TEXT("GridIndexY"));
	FActorRegistry::SaveActorMetaData(NAME_GridIndexY, GridIndexY, OutTags);

	static const FName NAME_GridIndexZ(TEXT("GridIndexZ"));
	FActorRegistry::SaveActorMetaData(NAME_GridIndexZ, GridIndexZ, OutTags);
}
#endif