// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_EDITOR
#include "LevelUtils.h"
#include "Misc/HashBuilder.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/UObjectHash.h"
#include "UObject/MetaData.h"
#include "Algo/Transform.h"
#include "Algo/RemoveIf.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Editor/GroupActor.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "Engine/Public/ActorReferencesUtils.h"
#endif

#if WITH_EDITOR
uint32 FWorldPartitionActorDesc::GlobalTag = 0;

FWorldPartitionActorDesc::FWorldPartitionActorDesc()
	: SoftRefCount(0)
	, HardRefCount(0)
	, Container(nullptr)
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

	ActorPtr = (AActor*)InActor;

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
		GridPlacement = InActor->GetGridPlacement();
	}

	RuntimeGrid = InActor->GetRuntimeGrid();
	bActorIsEditorOnly = InActor->IsEditorOnly();
	bLevelBoundsRelevant = InActor->IsLevelBoundsRelevant();
	bActorIsHLODRelevant = InActor->IsHLODRelevant();
	HLODLayer = InActor->GetHLODLayer() ? FName(InActor->GetHLODLayer()->GetPathName()) : FName();
	DataLayers = InActor->GetDataLayerNames();
	ActorPackage = InActor->GetPackage()->GetFName();
	ActorPath = *InActor->GetPathName();
	
	TArray<AActor*> ActorReferences = ActorsReferencesUtils::GetExternalActorReferences((AActor*)InActor);

	if (ActorReferences.Num())
	{
		References.Empty(ActorReferences.Num());
		for(AActor* ActorReference: ActorReferences)
		{
			References.Add(ActorReference->GetActorGuid());
		}
	}

	ActorLabel = *InActor->GetActorLabel(false);

	Container = InActor->GetLevel()->GetWorldPartition();
	check(Container);
}

void FWorldPartitionActorDesc::Init(UActorDescContainer* InContainer, const FWorldPartitionActorDescInitData& DescData)
{
	ActorPackage = DescData.PackageName;
	ActorPath = DescData.ActorPath;
	ActorClass = DescData.NativeClass;
	Class = DescData.NativeClass->GetFName();

	// Serialize actor metadata
	FMemoryReader MetadataAr(DescData.SerializedData, true);

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

	Container = InContainer;

	ActorPtr = FindObject<AActor>(nullptr, *ActorPath.ToString());
}

void FWorldPartitionActorDesc::SerializeTo(TArray<uint8>& OutData)
{
	// Serialize to archive and gather custom versions
	TArray<uint8> PayloadData;
	FMemoryWriter PayloadAr(PayloadData, true);
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
	check(!HardRefCount);

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

void FWorldPartitionActorDesc::ApplyActorTransform(const FTransform& InTransform)
{
	AActor* Actor = GetActor();
	check(Actor);

	if (!InTransform.Equals(FTransform::Identity))
	{
		FLevelUtils::FApplyLevelTransformParams TransformParams(Actor->GetLevel(), InTransform);
		TransformParams.Actor = Actor;
		TransformParams.bDoPostEditMove = true;
		FLevelUtils::ApplyLevelTransform(TransformParams);
	}
}


FString FWorldPartitionActorDesc::ToString() const
{
	return FString::Printf(TEXT("Guid:%s Class:%s Name:%s"), *Guid.ToString(), *Class.ToString(), *FPaths::GetExtension(ActorPath.ToString()));
}

void FWorldPartitionActorDesc::Serialize(FArchive& Ar)
{
	check(Ar.IsPersistent());

	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	Ar << Class << Guid << BoundsLocation << BoundsExtent << GridPlacement << RuntimeGrid << bActorIsEditorOnly << bLevelBoundsRelevant;
	
	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeDataLayers)
	{
		TArray<FName> Deprecated_Layers;
		Ar << Deprecated_Layers;
	}

	Ar << References;

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeArchivePersistent)
	{
		Ar << ActorPackage << ActorPath;
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeDataLayers)
	{
		Ar << DataLayers;
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeActorLabel)
	{
		Ar << ActorLabel;
	}

	if ((Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::WorldPartitionActorDescSerializeHLODInfo) ||
		(Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::WorldPartitionActorDescSerializeHLODInfo))
	{
		Ar << bActorIsHLODRelevant;
		Ar << HLODLayer;
	}
	else
	{
		bActorIsHLODRelevant = true;
		HLODLayer = FName();
	}
}

FBox FWorldPartitionActorDesc::GetBounds() const
{
	return FBox(BoundsLocation - BoundsExtent, BoundsLocation + BoundsExtent);
}

UHLODLayer* FWorldPartitionActorDesc::GetHLODLayer() const
{
	return HLODLayer.IsNone() ? nullptr : Cast<UHLODLayer>(FSoftObjectPath(HLODLayer).TryLoad());
}

bool FWorldPartitionActorDesc::IsLoaded() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GIsAutomationTesting)
	{
		return HardRefCount > 0;
	}
#endif

	return ActorPtr.IsValid();
}

AActor* FWorldPartitionActorDesc::GetActor() const
{
	return ActorPtr.Get(true);
}

AActor* FWorldPartitionActorDesc::Load() const
{
	if (ActorPtr.IsExplicitlyNull())
	{
		// First, try to find the existing actor which could have been loaded by another actor (through standard serialization)
		ActorPtr = FindObject<AActor>(nullptr, *ActorPath.ToString());
	}

	// The, if the actor isn't loaded, load it
	if (ActorPtr.IsExplicitlyNull())
	{
		// For now we assume that an ActorDesc that gets loaded is owned by an actual WorldPartition
		UWorldPartition* WorldPartition = Container ? CastChecked<UWorldPartition>(Container) : nullptr;
		check(WorldPartition || GIsAutomationTesting);

		const FLinkerInstancingContext* InstancingContext = nullptr;
		if (WorldPartition && WorldPartition->InstancingContext.IsInstanced())
		{
			InstancingContext = &WorldPartition->InstancingContext;
		}

		UPackage* Package = nullptr;

		if (InstancingContext)
		{
			FName RemappedPackageName = InstancingContext->Remap(ActorPackage);
			check(RemappedPackageName != ActorPath);

			Package = CreatePackage(*RemappedPackageName.ToString());
		}

		Package = LoadPackage(Package, *ActorPackage.ToString(), LOAD_None, nullptr, InstancingContext);

		if (Package)
		{
			ActorPtr = FindObject<AActor>(nullptr, *ActorPath.ToString());
			check(ActorPtr.IsValid());
		}
	}

	return ActorPtr.Get();
}

void FWorldPartitionActorDesc::Unload()
{
	if (AActor* Actor = GetActor())
	{
		check(Actor->IsPackageExternal());

		ForEachObjectWithPackage(Actor->GetPackage(), [](UObject* Object)
		{
			if (Object->HasAnyFlags(RF_Public | RF_Standalone))
			{
				CastChecked<UMetaData>(Object)->ClearFlags(RF_Public | RF_Standalone);
			}
			return true;
		}, false);

		ActorPtr = nullptr;
	}
}

void FWorldPartitionActorDesc::RegisterActor()
{
	if (AActor* Actor = GetActor())
	{
		ApplyActorTransform(CastChecked<UWorldPartition>(Container)->InstanceTransform);
		Actor->GetLevel()->AddLoadedActor(Actor);
	}
}

void FWorldPartitionActorDesc::UnregisterActor()
{
	if (AActor* Actor = GetActor())
	{
		if (!Actor->IsPendingKill())
		{
			Actor->GetLevel()->RemoveLoadedActor(Actor);
			ApplyActorTransform(CastChecked<UWorldPartition>(Container)->InstanceTransform.Inverse());
		}
	}
}
#endif