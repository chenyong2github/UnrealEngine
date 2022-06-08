// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Templates/IntegralConstant.h"
#include "Templates/UnrealTypeTraits.h"
#include "WorldPartition/WorldPartitionActorDescView.h"

class ENGINE_API FActorDescViewMap
{
	friend class FWorldPartitionStreamingGenerator;

private:
    template<typename F, typename V>bool DoCall(F Func, V Val, std::true_type) { return Func(*Val); }
	template<typename F, typename V> bool DoCall(F Func, V Val, std::false_type) { Func(*Val); return true; }

	template <class T>
	void ForEachActorDescView(T Func)
	{
		for (TUniquePtr<FWorldPartitionActorDescView>& ActorDescView : ActorDescViewList)
		{
			if (!DoCall(Func, ActorDescView.Get(), std::is_same<bool, decltype(Func(*ActorDescView))>()))
			{
				return;
			}
		}
	}

	FWorldPartitionActorDescView* FindByGuid(const FGuid& InGuid)
	{
		if (FWorldPartitionActorDescView** ActorDescViewPtr = ActorDescViewsByGuid.Find(InGuid))
		{
			return *ActorDescViewPtr;
		}
		return nullptr;
	}

public:
	FActorDescViewMap();

	// Non-copyable but movable
	FActorDescViewMap(const FActorDescViewMap&) = delete;
	FActorDescViewMap(FActorDescViewMap&&) = default;
	FActorDescViewMap& operator=(const FActorDescViewMap&) = delete;
	FActorDescViewMap& operator=(FActorDescViewMap&&) = default;

	FWorldPartitionActorDescView* Emplace(const FGuid& InActorGuid, const FWorldPartitionActorDescView& InActorDescView);

	template <class T>
	void ForEachActorDescView(T Func) const
	{
		for (const TUniquePtr<FWorldPartitionActorDescView>& ActorDescView : ActorDescViewList)
		{
			if (!DoCall(Func, ActorDescView.Get(), std::is_same<bool, decltype(Func(*ActorDescView))>()))
			{
				return;
			}
		}
	}

	const FWorldPartitionActorDescView* FindByGuid(const FGuid& InGuid) const
	{
		if (const FWorldPartitionActorDescView* const* ActorDescViewPtr = ActorDescViewsByGuid.Find(InGuid))
		{
			return *ActorDescViewPtr;
		}
		return nullptr;
	}

	template <class T>
	TArray<const FWorldPartitionActorDescView*> FindByExactNativeClass() const
	{
		return FindByExactNativeClass(T::StaticClass());
	}

	TArray<const FWorldPartitionActorDescView*> FindByExactNativeClass(UClass* InExactNativeClass) const;

	const TMap<FGuid, FWorldPartitionActorDescView*>& GetActorDescViewsByGuid() const { return ActorDescViewsByGuid; }

protected:
	TArray<TUniquePtr<FWorldPartitionActorDescView>> ActorDescViewList;

	TMap<FGuid, FWorldPartitionActorDescView*> ActorDescViewsByGuid;
	TMultiMap<FName, const FWorldPartitionActorDescView*> ActorDescViewsByClass;
};

#endif // WITH_EDITOR