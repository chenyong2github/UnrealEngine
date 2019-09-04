// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeInfoMap.h"
#include "Engine/World.h"
#include "LandscapeInfo.h"

ULandscapeInfoMap::ULandscapeInfoMap(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, World(nullptr)
{
}

void ULandscapeInfoMap::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	check(Map.Num() == 0);
}

void ULandscapeInfoMap::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsTransacting() || Ar.IsObjectReferenceCollector())
	{
		Ar << Map;
	}
}

void ULandscapeInfoMap::BeginDestroy()
{
	if (World != nullptr)
	{
		World->PerModuleDataObjects.Remove(this);
	}

	Super::BeginDestroy();
}

void ULandscapeInfoMap::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ULandscapeInfoMap* This = CastChecked<ULandscapeInfoMap>(InThis);
	Collector.AddReferencedObjects(This->Map, This);
}

ULandscapeInfoMap& ULandscapeInfoMap::GetLandscapeInfoMap(const UWorld* World)
{
	ULandscapeInfoMap *FoundObject = nullptr;
	World->PerModuleDataObjects.FindItemByClass(&FoundObject);

	checkf(FoundObject, TEXT("ULandscapInfoMap object was not created for this UWorld."));

	return *FoundObject;
}
