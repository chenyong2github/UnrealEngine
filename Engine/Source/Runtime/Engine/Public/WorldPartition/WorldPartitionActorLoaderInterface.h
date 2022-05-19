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
public:
	/** Base class for actor loaders */
	class ENGINE_API ILoaderAdapter
	{
	public:
		ILoaderAdapter(UWorld* InWorld);
		virtual ~ILoaderAdapter();

		bool Load();
		bool Unload();
		bool IsLoaded() const;

		UWorld* GetWorld() const { return World; }

		bool GetUserCreated() const { return bUserCreated; }
		void SetUserCreated(bool bValue) { bUserCreated = bValue; }

		// Public interface
		virtual TOptional<FBox> GetBoundingBox() const { return TOptional<FBox>(); }
		virtual TOptional<FString> GetLabel() const { return TOptional<FString>(); }
		virtual TOptional<FColor> GetColor() const { return TOptional<FColor>(); }

	protected:
		// Private interface
		virtual bool Intersect(const FBox& Box) const =0;
		virtual void ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const =0;
		virtual bool ShouldActorBeLoaded(const FWorldPartitionHandle& Actor) const;

		void RegisterDelegates();
		void UnregisterDelegates();

		/**
		 * Refresh the loaded state of this loader adatper.
		 *
		 * @return true if the operation was successful, false otherwise (operation was cancelled by the user
		 *         if actors needed resaving, etc.
		 */
		bool RefreshLoadedState();

		void PostLoadedStateChanged(bool bUnloadedActors);
		bool AllowUnloadingActors(const TArray<FWorldPartitionHandle>& ActorsToUnload) const;
		void AddReferenceToActor(FWorldPartitionHandle& Actor);
		void RemoveReferenceToActor(FWorldPartitionHandle& Actor);
		void OnActorDataLayersEditorLoadingStateChanged(bool bFromUserOperation);		

	private:
		UWorld* World;

		uint8 bLoaded : 1;
		uint8 bUserCreated : 1;

		TMap<FGuid, TMap<FGuid, FWorldPartitionReference>> ActorReferences;
	};

	/** Base class for actor loaders that contains a specific list of actors */
	class ENGINE_API ILoaderAdapterList : public ILoaderAdapter
	{
	public:
		ILoaderAdapterList(UWorld* InWorld);
		virtual ~ILoaderAdapterList() {}

	protected:
		//~ Begin ILoaderAdapter interface
		virtual bool Intersect(const FBox& Box) const { return true; }
		virtual void ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const;
		//~ End ILoaderAdapter interface

		TSet<FWorldPartitionHandle> Actors;
	};

	/** Base class for actor loaders that requires spatial queries */
	class ENGINE_API ILoaderAdapterSpatial : public ILoaderAdapter
	{
	public:
		ILoaderAdapterSpatial(UWorld* InWorld);
		virtual ~ILoaderAdapterSpatial() {}

	protected:
		//~ Begin ILoaderAdapter interface
		virtual void ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const;
		//~ End ILoaderAdapter interface

		// Interface
		virtual bool Intersect(const FBox& Box) const =0;

		bool bIncludeSpatiallyLoadedActors;
		bool bIncludeNonSpatiallyLoadedActors;
	};

	virtual ILoaderAdapter* GetLoaderAdapter() =0;
#endif
};