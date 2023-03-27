// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Misc/Optional.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartitionActorLoaderInterface.generated.h"

UINTERFACE()
class ENGINE_API UWorldPartitionActorLoaderInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class ENGINE_API IWorldPartitionActorLoaderInterface
{
	GENERATED_IINTERFACE_BODY()

#if WITH_EDITOR
	using FReferenceMap = TMap<FGuid, FWorldPartitionReference>;
	using FActorReferenceMap = TMap<FGuid, FReferenceMap>;
	using FContainerReferenceMap = TMap<TWeakObjectPtr<UActorDescContainer>, FActorReferenceMap>;

public:
	/** Base class for actor loaders */
	class ENGINE_API ILoaderAdapter
	{
	public:
		ILoaderAdapter(UWorld* InWorld);
		virtual ~ILoaderAdapter();

		void Load();
		void Unload();
		bool IsLoaded() const;

		UWorld* GetWorld() const { return World; }

		bool GetUserCreated() const { return bUserCreated; }
		void SetUserCreated(bool bValue) { bUserCreated = bValue; }

		// Public interface
		virtual TOptional<FBox> GetBoundingBox() const { return TOptional<FBox>(); }
		virtual TOptional<FString> GetLabel() const { return TOptional<FString>(); }
		virtual TOptional<FColor> GetColor() const { return TOptional<FColor>(); }

		void OnActorDescContainerInitialize(UActorDescContainer* Container);
		void OnActorDescContainerUninitialize(UActorDescContainer* Container);

	protected:
		// Private interface
		virtual void ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const =0;

		bool ShouldActorBeLoaded(const FWorldPartitionHandle& Actor) const;

		void RegisterDelegates();
		void UnregisterDelegates();

		// Actors filtering
		virtual bool PassActorDescFilter(const FWorldPartitionHandle& Actor) const;
		void RefreshLoadedState();

		void PostLoadedStateChanged(int32 NumLoads, int32 NumUnloads, bool bClearTransactions);
		void AddReferenceToActor(FWorldPartitionHandle& Actor);
		void RemoveReferenceToActor(FWorldPartitionHandle& Actor);
		void OnRefreshLoadedState(bool bFromUserOperation);

		// Helpers
		FActorReferenceMap& GetContainerReferences(UActorDescContainer* InContainer);
		const FActorReferenceMap* GetContainerReferencesConst(UActorDescContainer* InContainer) const;

	private:
		UWorld* World;

		uint8 bLoaded : 1;
		uint8 bUserCreated : 1;

		FContainerReferenceMap ContainerActorReferences;
	};

	virtual ILoaderAdapter* GetLoaderAdapter() =0;

	class FActorDescFilter
	{
	public:
		virtual ~FActorDescFilter() {}
		virtual bool PassFilter(class UWorld*, const FWorldPartitionHandle&) = 0;

		// Higher priority filters are called first
		virtual uint32 GetFilterPriority() const = 0;
		virtual FText* GetFilterReason() const = 0;
	};
		
	static void RegisterActorDescFilter(const TSharedRef<FActorDescFilter>& InActorDescFilter);

	static void RefreshLoadedState(bool bIsFromUserChange);

private:
	DECLARE_EVENT_OneParam(IWorldPartitionActorLoaderInterface, FOnActorLoaderInterfaceRefreshState, bool /*bIsFromUserChange*/);
	static FOnActorLoaderInterfaceRefreshState ActorLoaderInterfaceRefreshState;

	static TArray<TSharedRef<FActorDescFilter>> ActorDescFilters;
	friend class ILoaderAdapter;
#endif
};