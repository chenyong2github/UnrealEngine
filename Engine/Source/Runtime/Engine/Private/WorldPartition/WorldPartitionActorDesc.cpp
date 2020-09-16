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
#include "WorldPartition/WorldPartition.h"
#endif

#if WITH_EDITOR
uint32 FWorldPartitionActorDesc::GlobalTag = 0;

FWorldPartitionActorDesc::FWorldPartitionActorDesc()
	: LoadedRefCount(0)
	, Tag(0)
{}

bool FWorldPartitionActorDesc::Init(const AActor* InActor)
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
	FArchiveGetActorRefs GetActorRefsAr((AActor*)InActor, ActorReferences);
	((AActor*)InActor)->Serialize(GetActorRefsAr);

	if (ActorReferences.Num())
	{
		References.Empty(ActorReferences.Num());
		for(AActor* ActorReference: ActorReferences)
		{
			References.Add(ActorReference->GetActorGuid());
		}
	}

	UpdateHash();
	return true;
}

bool FWorldPartitionActorDesc::Init(const FWorldPartitionActorDescInitData& DescData)
{
	ActorPackage = DescData.PackageName;
	ActorPath = DescData.ActorPath;
	ActorClass = DescData.NativeClass;
	Class = DescData.NativeClass->GetFName();

	// Serialize actor metadata
	SerializeMetaData(DescData.Serializer);

	if (!DescData.Serializer->GetHasErrors())
	{	
		// Override grid placement by default class value
		const EActorGridPlacement DefaultGridPlacement = ActorClass->GetDefaultObject<AActor>()->GetDefaultGridPlacement();
		if (DefaultGridPlacement != EActorGridPlacement::None)
		{
			GridPlacement = DefaultGridPlacement;
		}

		// Transform BoundsLocation and BoundsExtent if necessary
		if (!DescData.Transform.Equals(FTransform::Identity))
		{
			//@todo_ow: This will result in a new BoundsExtent that is larger than it should. To fix this, we would need the Object Oriented BoundingBox of the actor (the BV of the actor without rotation)
			const FVector BoundsMin = BoundsLocation - BoundsExtent;
			const FVector BoundsMax = BoundsLocation + BoundsExtent;
			const FBox NewBounds = FBox(BoundsMin, BoundsMax).TransformBy(DescData.Transform);
			NewBounds.GetCenterAndExtents(BoundsLocation, BoundsExtent);
		}

		UpdateHash();
		return true;
	}

	return false;
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
	HashBuilder << Guid << Class << ActorPackage << ActorPath << BoundsLocation << BoundsExtent << GridPlacement << RuntimeGrid << bActorIsEditorOnly << bLevelBoundsRelevant << Layers << References;
}

void FWorldPartitionActorDesc::SerializeMetaData(FActorMetaDataSerializer* Serializer)
{
	bool bValidSerialization = true;

	bValidSerialization &= Serializer->Serialize(TEXT("ActorClass"), Class);
	bValidSerialization &= Serializer->Serialize(TEXT("ActorGuid"), Guid);
	bValidSerialization &= Serializer->Serialize(TEXT("BoundsLocation"), BoundsLocation);
	bValidSerialization &= Serializer->Serialize(TEXT("BoundsExtent"), BoundsExtent);
	bValidSerialization &= Serializer->Serialize(TEXT("GridPlacement"), (int8&)GridPlacement);
	bValidSerialization &= Serializer->Serialize(TEXT("RuntimeGrid"), RuntimeGrid);
	bValidSerialization &= Serializer->Serialize(TEXT("IsEditorOnly"), bActorIsEditorOnly);
	bValidSerialization &= Serializer->Serialize(TEXT("IsLevelBoundsRelevant"), bLevelBoundsRelevant);

	if (!bValidSerialization)
	{
		Serializer->SetHasErrors();
	}

	FString LayersStr;
	FString ActorsRefsStr;

	if (Serializer->IsWriting())
	{
		if (Layers.Num())
		{
			for(FName Layer: Layers)
			{
				LayersStr += Layer.ToString() + TEXT(";");
			}
			LayersStr.RemoveFromEnd(TEXT(";"));
		}

		if (References.Num())
		{
			for(const FGuid& ActoGuid: References)
			{
				ActorsRefsStr += ActoGuid.ToString() + TEXT(";");
			}
			ActorsRefsStr.RemoveFromEnd(TEXT(";"));
		}
	}

	Serializer->Serialize(TEXT("Layers"), LayersStr);
	Serializer->Serialize(TEXT("ActorReferences"), ActorsRefsStr);

	if (Serializer->IsReading())
	{
		TArray<FString> NewLayers;
		if (LayersStr.ParseIntoArray(NewLayers, TEXT(";")))
		{
			Algo::Transform(NewLayers, Layers, [&](const FString& Layer) { return FName(*Layer); });
		}

		TArray<FString> ActorsRefs;
		if (ActorsRefsStr.ParseIntoArray(ActorsRefs, TEXT(";")))
		{
			Algo::Transform(ActorsRefs, References, [](const FString& ActorGuidStr)
			{
				FGuid ActorGuid;
				if (!FGuid::Parse(ActorGuidStr, ActorGuid))
				{
					ActorGuid.Invalidate();
				}
				return ActorGuid;
			});

			Algo::RemoveIf(References, [](const FGuid& InGuid)
			{
				return !InGuid.IsValid();
			});
		}
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