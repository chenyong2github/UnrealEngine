// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_EDITOR
#include "Misc/HashBuilder.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/UObjectHash.h"
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Editor/GroupActor.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Engine/Public/ExternalActorsUtils.h"
#endif

#if WITH_EDITOR
uint32 FWorldPartitionActorDesc::GlobalTag = 0;

FWorldPartitionActorDesc::FWorldPartitionActorDesc()
	: LoadedRefCount(0)
	, Tag(0)
{}

void FWorldPartitionActorDesc::Init(const AActor* InActor)
{	
	check(InActor->IsPackageExternal());

	Guid = InActor->GetActorGuid();
	check(Guid.IsValid());

	// Get the first native class in the hierarchy
	ActorClass = GetParentNativeClass(InActor->GetClass());
	Class = ActorClass->GetFName();

	FVector NewBoundsLocation;
	FVector NewBoundsExtent;
	InActor->GetActorLocationBounds(/*bOnlyCollidingComponents*/false, NewBoundsLocation, NewBoundsExtent, /*bIncludeFromChildActors*/true);

	BoundsLocation = NewBoundsLocation;
	BoundsExtent = NewBoundsExtent;

	const EActorGridPlacement DefaultGridPlacement = InActor->GetDefaultGridPlacement();
	if (DefaultGridPlacement != EActorGridPlacement::None)
	{
		GridPlacement = DefaultGridPlacement;
	}
	else
	{
		GridPlacement = InActor->GridPlacement;
	}

	RuntimeGrid = InActor->RuntimeGrid;
	bActorIsEditorOnly = InActor->IsEditorOnly();
	bLevelBoundsRelevant = InActor->IsLevelBoundsRelevant();
	DataLayers = InActor->GetDataLayerNames();
	ActorPackage = InActor->GetPackage()->GetFName();
	ActorPath = *InActor->GetPathName();
	
	TArray<AActor*> ActorReferences = ExternalActorsUtils::GetExternalActorReferences((AActor*)InActor);

	if (ActorReferences.Num())
	{
		References.Empty(ActorReferences.Num());
		for(AActor* ActorReference: ActorReferences)
		{
			References.Add(ActorReference->GetActorGuid());
		}
	}
}

void FWorldPartitionActorDesc::Init(const FWorldPartitionActorDescInitData& DescData)
{
	ActorPackage = DescData.PackageName;
	ActorPath = DescData.ActorPath;
	ActorClass = DescData.NativeClass;
	Class = DescData.NativeClass->GetFName();

	// Serialize actor metadata
	FMemoryReader MetadataAr(DescData.SerializedData);

	// Serialize metadata custom versions
	FCustomVersionContainer CustomVersions;
	CustomVersions.Serialize(MetadataAr);
	MetadataAr.SetCustomVersions(CustomVersions);
	
	// Serialize metadata payload
	Serialize(MetadataAr);

	// Override grid placement by default class value
	const EActorGridPlacement DefaultGridPlacement = ActorClass->GetDefaultObject<AActor>()->GetDefaultGridPlacement();
	if (DefaultGridPlacement != EActorGridPlacement::None)
	{
		GridPlacement = DefaultGridPlacement;
	}
}

void FWorldPartitionActorDesc::SerializeTo(TArray<uint8>& OutData)
{
	// Serialize to archive and gather custom versions
	TArray<uint8> PayloadData;
	FMemoryWriter PayloadAr(PayloadData);
	Serialize(PayloadAr);

	// Serialize custom versions
	TArray<uint8> HeaderData;
	FMemoryWriter HeaderAr(HeaderData);
	FCustomVersionContainer CustomVersions = PayloadAr.GetCustomVersions();
	CustomVersions.Serialize(HeaderAr);

	// Append data
	OutData = MoveTemp(HeaderData);
	OutData.Append(PayloadData);
}

void FWorldPartitionActorDesc::TransformInstance(const FString& From, const FString& To, const FTransform& InstanceTransform)
{
	check(!LoadedRefCount);

	ActorPath = *ActorPath.ToString().Replace(*From, *To);

	// Transform BoundsLocation and BoundsExtent if necessary
	if (!InstanceTransform.Equals(FTransform::Identity))
	{
		//@todo_ow: This will result in a new BoundsExtent that is larger than it should. To fix this, we would need the Object Oriented BoundingBox of the actor (the BV of the actor without rotation)
		const FVector BoundsMin = BoundsLocation - BoundsExtent;
		const FVector BoundsMax = BoundsLocation + BoundsExtent;
		const FBox NewBounds = FBox(BoundsMin, BoundsMax).TransformBy(InstanceTransform);
		NewBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
	}
}

FString FWorldPartitionActorDesc::ToString() const
{
	return FString::Printf(TEXT("Guid:%s Class:%s Name:%s"), *Guid.ToString(), *Class.ToString(), *FPaths::GetExtension(ActorPath.ToString()));
}

void FWorldPartitionActorDesc::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	Ar << Class << Guid << BoundsLocation << BoundsExtent << GridPlacement << RuntimeGrid << bActorIsEditorOnly << bLevelBoundsRelevant;
	
	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeDataLayers)
	{
		TArray<FName> Deprecated_Layers;
		Ar << Deprecated_Layers;
	}

	Ar << References;

	if (!Ar.IsPersistent())
	{
		Ar << ActorPackage << ActorPath;
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeDataLayers)
	{
		Ar << DataLayers;
	}
}

FBox FWorldPartitionActorDesc::GetBounds() const
{
	return FBox(BoundsLocation - BoundsExtent, BoundsLocation + BoundsExtent);
}

AActor* FWorldPartitionActorDesc::GetActor() const
{
	return FindObject<AActor>(nullptr, *ActorPath.ToString());
}

AActor* FWorldPartitionActorDesc::Load(const FLinkerInstancingContext* InstancingContext) const
{
	UPackage* Package = nullptr;

	if (InstancingContext)
	{
		FName RemappedPackageName = InstancingContext->Remap(ActorPackage);
		check(RemappedPackageName != ActorPath);

		Package = CreatePackage(*RemappedPackageName.ToString());
	}

	if (LoadPackage(Package, *ActorPackage.ToString(), LOAD_None, nullptr, InstancingContext))
	{
		return GetActor();
	}

	return nullptr;
}

void FWorldPartitionActorDesc::Unload()
{
	AActor* Actor = GetActor();
	if (Actor && Actor->IsPackageExternal())
	{
		ForEachObjectWithPackage(Actor->GetPackage(), [](UObject* Object)
		{
			Object->ClearFlags(RF_Public | RF_Standalone);
			return true;
		}, false);
	}
}
#endif