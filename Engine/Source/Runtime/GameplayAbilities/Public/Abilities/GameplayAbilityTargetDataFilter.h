// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayAbilityTargetDataFilter.generated.h"

UENUM(BlueprintType)
namespace ETargetDataFilterSelf
{
	enum Type
	{
		TDFS_Any 			UMETA(DisplayName = "Allow self or others"),
		TDFS_NoSelf 		UMETA(DisplayName = "Filter self out"),
		TDFS_NoOthers		UMETA(DisplayName = "Filter others out")
	};
}

USTRUCT(BlueprintType)
struct GAMEPLAYABILITIES_API FGameplayTargetDataFilter
{
	GENERATED_USTRUCT_BODY()

	virtual ~FGameplayTargetDataFilter()
	{
	}

	virtual bool FilterPassesForActor(const AActor* ActorToBeFiltered) const
	{
		switch (SelfFilter.GetValue())
		{
		case ETargetDataFilterSelf::Type::TDFS_NoOthers:
			if (ActorToBeFiltered != SelfActor)
			{
				return false;
			}
			break;
		case ETargetDataFilterSelf::Type::TDFS_NoSelf:
			if (ActorToBeFiltered == SelfActor)
			{
				return false;
			}
			break;
		case ETargetDataFilterSelf::Type::TDFS_Any:
		default:
			break;
		}
		return true;
	}

	void InitializeFilterContext(AActor* FilterActor);

	/** Filled out while running */
	UPROPERTY()
	AActor* SelfActor;

	/** Our actual filter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ExposeOnSpawn = true), Category = Filter)
	TEnumAsByte<ETargetDataFilterSelf::Type> SelfFilter;
};


USTRUCT(BlueprintType)
struct GAMEPLAYABILITIES_API FGameplayTargetDataFilterHandle
{
	GENERATED_USTRUCT_BODY()

	TSharedPtr<FGameplayTargetDataFilter> Filter;

	bool FilterPassesForActor(const AActor* ActorToBeFiltered) const
	{
		if (!ActorToBeFiltered)
		{
			return false;
		}
		//Eventually, this might iterate through multiple filters. We'll need to decide how to designate OR versus AND functionality.
		if (Filter.IsValid())
		{
			if (!Filter.Get()->FilterPassesForActor(ActorToBeFiltered))
			{
				return false;
			}
		}
		return true;
	}

	bool operator()(const TWeakObjectPtr<AActor> ActorToBeFiltered) const
	{
		return FilterPassesForActor(ActorToBeFiltered.Get());
	}

	bool operator()(const AActor* ActorToBeFiltered) const
	{
		return FilterPassesForActor(ActorToBeFiltered);
	}
};
