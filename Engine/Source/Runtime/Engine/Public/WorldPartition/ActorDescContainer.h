// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescType.h"
#include "ActorDescContainer.generated.h"

UCLASS()
class ENGINE_API UActorDescContainer : public UObject
{
	GENERATED_UCLASS_BODY()

	friend struct FWorldPartitionHandleUtils;

public:
	void Initialize(FName InPackageName, bool bRegisterDelegates);
	virtual void Uninitialize();
	
#if WITH_EDITOR
	// Asset registry events
	virtual void OnAssetAdded(const FAssetData& InAssetData);
	virtual void OnAssetRemoved(const FAssetData& InAssetData);
	virtual void OnAssetUpdated(const FAssetData& InAssetData);

	FWorldPartitionActorDesc* GetActorDesc(const FGuid& Guid);
	const FWorldPartitionActorDesc* GetActorDesc(const FGuid& Guid) const;
#endif

protected:
#if WITH_EDITOR
	TUniquePtr<FWorldPartitionActorDesc> GetActorDescriptor(const FAssetData& InAssetData);
	
	virtual void RegisterDelegates();
	virtual void UnregisterDelegates();

	virtual void OnActorDescAdded(const TUniquePtr<FWorldPartitionActorDesc>& NewActorDesc) {}
	virtual void OnActorDescRemoved(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}
	virtual void OnActorDescUpdating(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}
	virtual void OnActorDescUpdated(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}

	bool ShouldHandleAssetEvent(const FAssetData& InAssetData);

	bool bContainerInitialized;
	bool bIgnoreAssetRegistryEvents;

	TChunkedArray<TUniquePtr<FWorldPartitionActorDesc>> ActorDescList;

	FName ContainerPackageName;

private:
	TMap<FGuid, TUniquePtr<FWorldPartitionActorDesc>*> Actors;

public:
	template<bool bConst, class ActorType>
	class TBaseIterator
	{
		static_assert(TIsDerivedFrom<ActorType, AActor>::IsDerived, "Type is not derived from AActor.");

	protected:
		typedef TMap<FGuid, TUniquePtr<FWorldPartitionActorDesc>*> MapType;
		typedef typename FWorldPartitionActorDescType<ActorType>::Type ValueType;
		typedef typename TChooseClass<bConst, MapType::TConstIterator, MapType::TIterator>::Result IteratorType;
		typedef typename TChooseClass<bConst, const UActorDescContainer*, UActorDescContainer*>::Result ContainerType;		
		typedef typename TChooseClass<bConst, const ValueType*, ValueType*>::Result ReturnType;

	public:
		TBaseIterator(ContainerType InActorDescContainer)
			: ActorsIterator(InActorDescContainer->Actors)
		{
			if (ShouldSkip())
			{
				operator++();
			}
		}

		/**
		 * Iterates to next suitable actor desc.
		 */
		void operator++()
		{
			do
			{
				++ActorsIterator;
			} while (ShouldSkip());
		}

		/**
		 * Returns the current suitable actor desc pointed at by the Iterator
		 *
		 * @return	Current suitable actor desc
		 */
		FORCEINLINE ReturnType operator*() const
		{
			return StaticCast<ReturnType>(ActorsIterator->Value->Get());
		}

		/**
		 * Returns the current suitable actor desc pointed at by the Iterator
		 *
		 * @return	Current suitable actor desc
		 */
		FORCEINLINE ReturnType operator->() const
		{
			return StaticCast<ReturnType>(ActorsIterator->Value->Get());
		}
		/**
		 * Returns whether the iterator has reached the end and no longer points
		 * to a suitable actor desc.
		 *
		 * @return true if iterator points to a suitable actor desc, false if it has reached the end
		 */
		FORCEINLINE explicit operator bool() const
		{
			return (bool)ActorsIterator;
		}

	protected:
		/**
		 * Determines whether the iterator currently points to a valid actor desc or not.
		 * @return true if we should skip the actor desc
		 */
		FORCEINLINE bool ShouldSkip() const
		{
			if (!ActorsIterator)
			{
				return false;
			}

			return !ActorsIterator->Value->Get()->GetActorClass()->IsChildOf(ActorType::StaticClass());
		}

		IteratorType ActorsIterator;
	};

	template <class ActorType = AActor>
	class TIterator : public TBaseIterator<false, ActorType>
	{
		typedef TBaseIterator<false, ActorType> BaseType;

	public:
		TIterator(BaseType::ContainerType InActorDescContainer)
			: BaseType(InActorDescContainer)
		{}
	};

	template <class ActorType = AActor>
	class TConstIterator : public TBaseIterator<true, ActorType>
	{
		typedef TBaseIterator<true, ActorType> BaseType;

	public:
		TConstIterator(BaseType::ContainerType InActorDescContainer)
			: BaseType(InActorDescContainer)
		{}
	};
#endif
};