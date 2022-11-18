// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SubclassOf.h"

#include "PCGActorSelector.generated.h"

class AActor;
class UWorld;

UENUM()
enum class EPCGActorSelection : uint8
{
	ByTag,
	ByName,
	ByClass
};

UENUM()
enum class EPCGActorFilter : uint8
{
	Self,
	Parent,
	Root,
	AllWorldActors
	// TODO
	// TrackedActors
};

USTRUCT(BlueprintType)
struct FPCGActorSelectorSettings
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGActorSelection ActorSelection = EPCGActorSelection::ByTag;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "ActorSelection==EPCGActorSelection::ByTag", EditConditionHides))
	FName ActorSelectionTag;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "ActorSelection==EPCGActorSelection::ByName", EditConditionHides))
	FName ActorSelectionName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "ActorSelection==EPCGActorSelection::ByClass", EditConditionHides))
	TSubclassOf<AActor> ActorSelectionClass;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGActorFilter ActorFilter = EPCGActorFilter::Self;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "ActorFilter!=EPCGActorFilter::AllWorldActors", EditConditionHides))
	bool bIncludeChildren = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "ActorSelection!=EPCGActorSelection::ByName"))
	bool bSelectMultiple = false;
};

namespace PCGActorSelector
{
	TArray<AActor*> FindActors(const FPCGActorSelectorSettings& Settings, UWorld* World, AActor* Self);
	AActor* FindActor(const FPCGActorSelectorSettings& Settings, UWorld* World, AActor* Self);
}