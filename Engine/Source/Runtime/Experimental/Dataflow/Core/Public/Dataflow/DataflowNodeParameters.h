// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class  UDataflow;

namespace Dataflow
{
	struct ContextCacheBase 
	{
		ContextCacheBase(FProperty* InProperty = nullptr, void* InData = nullptr)
			: Property(InProperty)
			, Data(InData)
		{}
		virtual ~ContextCacheBase() {}

		FProperty* Property = nullptr;
		void* Data = nullptr;
	};

	template<class T>
	struct ContextCache : public ContextCacheBase 
	{
		ContextCache(FProperty* InProperty = nullptr, void* InData = nullptr)
			: ContextCacheBase(InProperty,InData)
		{}
		virtual ~ContextCache() 
		{ 
			delete (T*)Data; 
		}
	};

	class DATAFLOWCORE_API FContext
	{
		TMap<int64, ContextCacheBase*> DataStore;

	public:



		FContext(float InTime, FString InType = FString(""))
			: Timestamp(InTime)
			, Type(StaticType().Append(InType))
		{}

		~FContext()
		{
			for (TTuple<int64, ContextCacheBase*> Elem : DataStore)
			{
				delete Elem.Value;
			}
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

		void SetData(size_t Key, ContextCacheBase* InData)
		{
			int64 IntKey = (int64)Key;
			if (!DataStore.Contains(IntKey))
			{
				DataStore.Add(IntKey, InData);
			}
			else
			{
				DataStore[IntKey] = InData;
			}
		}

		template<class T>
		bool GetData(size_t Key, T& OutData)
		{
			int64 IntKey = (int64)Key;

			if (DataStore.Contains(IntKey))
			{
				// @todo(dataflow) : type check
				OutData = *(T*)DataStore[IntKey]->Data;
				return true;
			}
			return false;
		}

		template<class T>
		const T& GetDataReference(size_t Key, const T& Default)
		{
			int64 IntKey = (int64)Key;

			if (DataStore.Contains(Key))
			{
				// @todo(dataflow) : type check
				return *(T*)DataStore[Key]->Data;
			}
			return Default;
		}

		bool HasData(size_t Key)
		{
			int64 IntKey = (int64)Key;

			return DataStore.Contains(IntKey);
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
