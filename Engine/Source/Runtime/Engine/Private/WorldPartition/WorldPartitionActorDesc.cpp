// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_EDITOR
#include "Misc/HashBuilder.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/UObjectHash.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Editor/GroupActor.h"
#include "WorldPartition/WorldPartition.h"
#endif

#if WITH_EDITOR
uint32 FWorldPartitionActorDesc::GlobalTag = 0;

FWorldPartitionActorDesc::FWorldPartitionActorDesc(AActor* InActor)
	: LoadedRefCount(0)
	, Tag(0)
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
	Layers = InActor->Layers;
	ActorPackage = InActor->GetPackage()->GetFName();
	ActorPath = *InActor->GetPathName();
	
	TSet<AActor*> ActorReferences;
	FArchiveGetActorRefs GetActorRefsAr(InActor, ActorReferences);
	InActor->Serialize(GetActorRefsAr);

	if (ActorReferences.Num())
	{
		References.Empty(ActorReferences.Num());
		for(AActor* ActorReference: ActorReferences)
		{
			References.Add(ActorReference->GetActorGuid());
		}
	}
}

FWorldPartitionActorDesc::FWorldPartitionActorDesc(const FWorldPartitionActorDescData& DescData)
	: LoadedRefCount(0)
	, Tag(0)
{
	Guid = DescData.Guid;
	Class = DescData.Class;
	BoundsLocation = DescData.BoundsLocation;
	BoundsExtent = DescData.BoundsExtent;
	GridPlacement = DescData.GridPlacement;
	RuntimeGrid = DescData.RuntimeGrid;
	bActorIsEditorOnly = DescData.bActorIsEditorOnly;
	bLevelBoundsRelevant = DescData.bLevelBoundsRelevant;
	Layers = DescData.Layers;
	References = DescData.References;
	ActorPackage = DescData.ActorPackage;
	ActorPath = DescData.ActorPath;

	ActorClass = FindObjectChecked<UClass>(ANY_PACKAGE, *Class.ToString(), true);
}

FString FWorldPartitionActorDesc::ToString() const
{
	return FString::Printf(TEXT("Guid:%s Class:%s Name:%s"), *Guid.ToString(), *Class.ToString(), *FPaths::GetExtension(ActorPath.ToString()));
}

void FWorldPartitionActorDesc::UpdateHash()
{
	FHashBuilder HashBuilder;
	BuildHash(HashBuilder);
	Hash = HashBuilder.GetHash();
}

void FWorldPartitionActorDesc::BuildHash(FHashBuilder& HashBuilder)
{
	HashBuilder << Guid << Class << BoundsLocation << BoundsExtent << GridPlacement << RuntimeGrid << bActorIsEditorOnly << bLevelBoundsRelevant << Layers << References << ActorPackage << ActorPath;
}

FBox FWorldPartitionActorDesc::GetBounds() const
{
	return FBox(BoundsLocation - BoundsExtent, BoundsLocation + BoundsExtent);
}

AActor* FWorldPartitionActorDesc::GetActor() const
{
	return FindObject<AActor>(nullptr, *ActorPath.ToString());
}

AActor* FWorldPartitionActorDesc::Load(const FLinkerInstancingContext* InstancingContext)
{
	UPackage* Package = nullptr;

	if (InstancingContext)
	{
		FName RemappedPackageName = InstancingContext->Remap(ActorPackage);
		check(RemappedPackageName != ActorPath);

		Package = CreatePackage(nullptr, *RemappedPackageName.ToString());
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