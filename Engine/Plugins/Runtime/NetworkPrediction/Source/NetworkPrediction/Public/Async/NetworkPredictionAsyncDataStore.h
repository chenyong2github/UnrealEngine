// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/StaticArray.h"
#include "NetworkPredictionAsyncDefines.h"
#include "NetworkPredictionCheck.h"

namespace UE_NP {

struct FAsyncDataStoreCollection
{
public:

	template<typename DataStoreType, typename AsyncModelDef>
	DataStoreType* GetDataStore()
	{
		return GetDataStore_Internal<DataStoreType, AsyncModelDef>();
	};

	template<typename DataStoreType, typename AsyncModelDef>
	const DataStoreType* GetDataStore() const
	{
		return const_cast<FAsyncDataStoreCollection*>(this)->GetDataStore_Internal<DataStoreType, AsyncModelDef>();
	}

	void Reset()
	{
		for (TUniquePtr<IDataStore>& Ptr : DataStoreArray)
		{
			if (Ptr.IsValid())
			{
				Ptr->Reset();
			}
		}
	}

private:

	struct IDataStore
	{
		virtual ~IDataStore() = default;
		virtual void Reset() = 0;
	};

	// This needs to be static for thread safety. We can't resize the array on the GT once the PT is running
	// FIXME: this may not be true anymore. PT could have its own data store
	TStaticArray<TUniquePtr<IDataStore>, MaxAsyncModelDefs> DataStoreArray;


	template<typename DataStoreType, typename AsyncModelDef>
	DataStoreType* GetDataStore_Internal()
	{
		npEnsureMsgf(AsyncModelDef::ID >= 0, TEXT("AsyncModelDef %s has invalid ID assigned. Could be missing NP_ASYNC_MODEL_REGISTER."), AsyncModelDef::GetName());

		struct FThisDataStore : IDataStore
		{
			DataStoreType Self;
			void Reset() final override { Self.Reset(); }
		};

		TUniquePtr<IDataStore>& Item = DataStoreArray[AsyncModelDef::ID];
		if (Item.IsValid() == false)
		{
			Item = MakeUnique<FThisDataStore>();
		}

		return &((FThisDataStore*)Item.Get())->Self;
	};
};

} // namespace UE_NP