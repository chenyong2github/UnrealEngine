// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "Templates/UniquePtr.h"
#include "GenericPlatform/GenericPlatformCriticalSection.h"

class  UDataflow;

namespace Dataflow
{
	struct FContextCacheBase 
	{
		FContextCacheBase(FProperty* InProperty = nullptr, uint64 InTimestamp = 0)
			: Property(InProperty)
			, Timestamp(InTimestamp)
		{}
		virtual ~FContextCacheBase() {}

		template<typename T>
		const T& GetTypedData(const FProperty* PropertyIn) const;
		
		FProperty* Property = nullptr;
		uint64 Timestamp = 0;
	};

	template<class T>
	struct FContextCache : public FContextCacheBase 
	{
		FContextCache(FProperty* InProperty, const T& InData, uint64 Timestamp)
			: FContextCacheBase(InProperty, Timestamp)
			, Data(InData)
		{}

		FContextCache(FProperty* InProperty, T&& InData, uint64 Timestamp)
			: FContextCacheBase(InProperty, Timestamp)
			, Data(InData)
		{}
		
		const T Data;
	};

	template<class T>
	const T& FContextCacheBase::GetTypedData(const FProperty* PropertyIn) const
	{
		check(PropertyIn);
		// check(PropertyIn->IsA<T>()); // @todo(dataflow) compile error for non-class T; find alternatives
		check(Property->SameType(PropertyIn));
		return static_cast<const FContextCache<T>&>(*this).Data;
	}
	
	class DATAFLOWCORE_API FContext
	{
		TMap<int64, TUniquePtr<FContextCacheBase>> DataStore;
		TSharedPtr<FCriticalSection> CacheLock;
		
		FContext(FContext&&) = default;
		FContext& operator=(FContext&&) = default;
		
		FContext(const FContext&) = delete;
		FContext& operator=(const FContext&) = delete;

	public:

		FContext(float InTime, FString InType = FString(""))
			: Timestamp(InTime)
			, Type(StaticType().Append(InType))
		{
			CacheLock = MakeShared<FCriticalSection>();
		}

		float Timestamp = 0.f;
		FString Type;
		static FString StaticType() { return "FContext"; }

		uint32 GetTypeHash() const
		{
			return ::GetTypeHash(Timestamp);
		}

		template<class T>
		const T* AsType() const
		{
			if (Type.Contains(T::StaticType()))
			{
				return (T*)this;
			}
			return nullptr;
		}

		template<typename T>
		void SetData(size_t Key, FProperty* Property, const T& Value)
		{
			CacheLock->Lock(); ON_SCOPE_EXIT { CacheLock->Unlock(); };
			
			int64 IntKey = (int64)Key;
			TUniquePtr<FContextCache<T>> DataStoreEntry = MakeUnique<FContextCache<T>>(Property, Value, FPlatformTime::Cycles64());
			DataStore.Emplace(IntKey, MoveTemp(DataStoreEntry));
		}

		template<typename T>
		void SetData(size_t Key, FProperty* Property, T&& Value)
		{
			CacheLock->Lock(); ON_SCOPE_EXIT { CacheLock->Unlock(); };
			
			int64 IntKey = (int64)Key;
			TUniquePtr<FContextCache<T>> DataStoreEntry = MakeUnique<FContextCache<T>>(Property, Forward<T>(Value), FPlatformTime::Cycles64());
			DataStore.Emplace(IntKey, MoveTemp(DataStoreEntry));
		}
		
		template<class T>
		const T& GetData(size_t Key, FProperty* Property, const T& Default = T())
		{
			CacheLock->Lock(); ON_SCOPE_EXIT { CacheLock->Unlock(); };
			
			if (TUniquePtr<FContextCacheBase>* Cache = DataStore.Find(Key))
			{
				return (*Cache)->GetTypedData<T>(Property);
			}
			return Default;
		}

		bool HasData(size_t Key, uint64 StoredAfter = 0)
		{
			CacheLock->Lock(); ON_SCOPE_EXIT { CacheLock->Unlock(); };
			
			int64 IntKey = (int64)Key;
			return DataStore.Contains(IntKey) && DataStore[Key]->Timestamp >= StoredAfter;
		}
	};


	struct TCacheValue {
		TCacheValue() {}
	};
}

FORCEINLINE uint32 GetTypeHash(const Dataflow::FContext& Context)
{
	return ::GetTypeHash(Context.Timestamp);
}
